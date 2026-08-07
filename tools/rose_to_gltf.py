#!/usr/bin/env python3
"""
rose_to_gltf.py  —  Convert ROSE ZMD skeleton + ZMS mesh(es) to glTF 2.0 GLB.

Usage:
    python rose_to_gltf.py FEMALE.ZMD body.ZMS -o female_body.glb
    python rose_to_gltf.py FEMALE.ZMD mesh1.ZMS mesh2.ZMS -o multi.glb
    python rose_to_gltf.py FEMALE.ZMD -o skeleton_only.glb

Coordinate mapping  ROSE → glTF (Y-up right-handed):
    Position : (x, y, z)_rose → (x, z, -y)_gltf   [ROSE Z=up→Y, ROSE Y=fwd→-Z]
    Quaternion: rose(w,x,y,z)  → gltf(x,z,-y,w)    [stored as x,y,z,w per spec]
    Scale    : ZMD translations already in metres; ZMS positions × 0.01
"""
import argparse
import json
import math
import os
import struct
import sys
from typing import List, Optional, Tuple

# ── path setup ────────────────────────────────────────────────────────────────
_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from rose_parser.formats.zmd import parse as parse_zmd, ZMD, Bone
from rose_parser.formats.zms import parse as parse_zms, ZMS
from rose_parser.formats.zms import VF_NORMAL, VF_BLEND_WEIGHT, VF_BLEND_INDEX, VF_UV0

# ── constants ─────────────────────────────────────────────────────────────────
SCALE = 1.0 / 100.0          # ZMS positions: ROSE cm → metres

# glTF component types
COMP_FLOAT  = 5126
COMP_U16    = 5123
COMP_U8     = 5121

# glTF buffer-view targets
TARGET_ARRAY         = 34962
TARGET_ELEMENT_ARRAY = 34963

# ── linear-algebra helpers ────────────────────────────────────────────────────

def _mat4_from_tq(tx: float, ty: float, tz: float,
                  qx: float, qy: float, qz: float, qw: float) -> List[float]:
    """Column-major 4×4 rigid-body matrix from translation + quaternion."""
    x, y, z, w = qx, qy, qz, qw
    r00 = 1 - 2*(y*y + z*z);  r01 = 2*(x*y - w*z);  r02 = 2*(x*z + w*y)
    r10 = 2*(x*y + w*z);       r11 = 1 - 2*(x*x + z*z); r12 = 2*(y*z - w*x)
    r20 = 2*(x*z - w*y);       r21 = 2*(y*z + w*x);  r22 = 1 - 2*(x*x + y*y)
    # Stored column-major: [col0 | col1 | col2 | col3]
    return [
        r00, r10, r20, 0.0,
        r01, r11, r21, 0.0,
        r02, r12, r22, 0.0,
        tx,  ty,  tz,  1.0,
    ]


def _mat4_mul(A: List[float], B: List[float]) -> List[float]:
    """Column-major 4×4 matrix product  C = A × B."""
    R = [0.0] * 16
    for c in range(4):
        for r in range(4):
            for k in range(4):
                R[c*4+r] += A[k*4+r] * B[c*4+k]
    return R


def _mat4_rigid_inv(A: List[float]) -> List[float]:
    """Inverse of a column-major rigid-body (rotation + translation) matrix."""
    tx, ty, tz = A[12], A[13], A[14]
    # Inverse rotation = transpose; inverse translation = -R^T * t
    return [
        A[0],  A[4],  A[8],  0.0,
        A[1],  A[5],  A[9],  0.0,
        A[2],  A[6],  A[10], 0.0,
        -(A[0]*tx + A[1]*ty + A[2]*tz),
        -(A[4]*tx + A[5]*ty + A[6]*tz),
        -(A[8]*tx + A[9]*ty + A[10]*tz),
        1.0,
    ]

# ── coordinate conversion ─────────────────────────────────────────────────────
#
# ROSE uses a LEFT-HANDED, Z-up coordinate system (DirectX).  glTF is RIGHT-HANDED
# Y-up.  Converting between them requires a HANDEDNESS FLIP (a reflection,
# determinant −1) — not just an axis permutation.  The earlier (x, z, −y) map was
# a pure rotation (det +1), so it silently mirrored the data; that's invisible at
# bind pose (skin = identity) but makes animations explode once bones move.
#
# BASIS maps a ROSE vector to a glTF vector: v_gltf = BASIS · v_rose.  Rows are
# the glTF axes expressed in ROSE coordinates.  Default swaps Y/Z (Z-up→Y-up),
# which has determinant −1 → correct handedness flip.
#
# If the character ends up facing the wrong way or mirrored after testing, negate
# one row of BASIS (e.g. flip the sign of the X or Z row) — the math below
# (matrix conjugation for quaternions, winding flip for faces) stays correct for
# any orthogonal BASIS.
BASIS = (
    (1.0, 0.0, 0.0),   # gltf X =  rose X
    (0.0, 0.0, 1.0),   # gltf Y =  rose Z   (up)
    (0.0, 1.0, 0.0),   # gltf Z =  rose Y
)


def _basis_det() -> float:
    m = BASIS
    return (m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])
            - m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
            + m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]))


BASIS_FLIPS_WINDING = _basis_det() < 0


def _apply_basis(x: float, y: float, z: float) -> Tuple[float, float, float]:
    m = BASIS
    return (m[0][0]*x + m[0][1]*y + m[0][2]*z,
            m[1][0]*x + m[1][1]*y + m[1][2]*z,
            m[2][0]*x + m[2][1]*y + m[2][2]*z)


def _quat_to_mat3(w, x, y, z):
    return (
        (1-2*(y*y+z*z), 2*(x*y-w*z),   2*(x*z+w*y)),
        (2*(x*y+w*z),   1-2*(x*x+z*z), 2*(y*z-w*x)),
        (2*(x*z-w*y),   2*(y*z+w*x),   1-2*(x*x+y*y)),
    )


def _mat3_mul(A, B):
    return tuple(
        tuple(sum(A[i][k]*B[k][j] for k in range(3)) for j in range(3))
        for i in range(3)
    )


def _mat3_to_quat(m):
    """Convert a 3×3 rotation matrix to a quaternion (x, y, z, w) for glTF."""
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0.0:
        s = math.sqrt(t + 1.0) * 2.0
        w = 0.25 * s
        x = (m[2][1] - m[1][2]) / s
        y = (m[0][2] - m[2][0]) / s
        z = (m[1][0] - m[0][1]) / s
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0
        w = (m[2][1] - m[1][2]) / s
        x = 0.25 * s
        y = (m[0][1] + m[1][0]) / s
        z = (m[0][2] + m[2][0]) / s
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0
        w = (m[0][2] - m[2][0]) / s
        x = (m[0][1] + m[1][0]) / s
        y = 0.25 * s
        z = (m[1][2] + m[2][1]) / s
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0
        w = (m[1][0] - m[0][1]) / s
        x = (m[0][2] + m[2][0]) / s
        y = (m[1][2] + m[2][1]) / s
        z = 0.25 * s
    return (x, y, z, w)


# BASIS is orthogonal, so its inverse is its transpose.
_BASIS_T = tuple(tuple(BASIS[j][i] for j in range(3)) for i in range(3))


def _pos_r2g(x: float, y: float, z: float) -> Tuple[float, float, float]:
    """ROSE position → glTF position (no scaling; ZMD already in metres)."""
    return _apply_basis(x, y, z)


def _pos_r2g_cm(x: float, y: float, z: float) -> Tuple[float, float, float]:
    """ROSE position in cm → glTF position in metres."""
    gx, gy, gz = _apply_basis(x, y, z)
    return (gx * SCALE, gy * SCALE, gz * SCALE)


def _quat_r2g(w: float, x: float, y: float, z: float) -> Tuple[float, float, float, float]:
    """ROSE quaternion (w,x,y,z) → glTF (x,y,z,w).

    Correct for any orthogonal BASIS including reflections: convert the rotation
    to a matrix, change basis (R' = BASIS · R · BASISᵀ), and read back a quaternion.
    The result is always a proper rotation even when BASIS reflects.
    """
    R = _quat_to_mat3(w, x, y, z)
    Rp = _mat3_mul(_mat3_mul(BASIS, R), _BASIS_T)
    return _mat3_to_quat(Rp)


def _normal_r2g(x: float, y: float, z: float) -> Tuple[float, float, float]:
    return _apply_basis(x, y, z)

# ── world-transform computation ───────────────────────────────────────────────

def _bone_world_mats(zmd: ZMD) -> List[List[float]]:
    """Compute world-space 4×4 matrices for every bone (in glTF coordinates)."""
    mats: List[Optional[List[float]]] = [None] * len(zmd.bones)

    def compute(i: int) -> List[float]:
        if mats[i] is not None:
            return mats[i]
        bone = zmd.bones[i]
        tx, ty, tz = _pos_r2g(*bone.translation)       # already in metres
        gx, gy, gz, gw = _quat_r2g(*bone.rotation)
        local = _mat4_from_tq(tx, ty, tz, gx, gy, gz, gw)
        if bone.parent_id == i:                         # root (self-referential)
            mats[i] = local
        else:
            mats[i] = _mat4_mul(compute(bone.parent_id), local)
        return mats[i]

    for i in range(len(zmd.bones)):
        compute(i)
    return mats  # type: ignore[return-value]

# ── binary packing helpers ────────────────────────────────────────────────────

def _pack_f32(values) -> bytes:
    flat = []
    for v in values:
        flat.extend(v) if hasattr(v, '__iter__') else flat.append(v)
    return struct.pack(f'<{len(flat)}f', *flat)


def _pack_u16(values) -> bytes:
    flat = []
    for v in values:
        flat.extend(v) if hasattr(v, '__iter__') else flat.append(v)
    return struct.pack(f'<{len(flat)}H', *flat)


def _pad4(data: bytes, pad_byte: int = 0) -> bytes:
    rem = len(data) % 4
    if rem:
        data += bytes([pad_byte] * (4 - rem))
    return data

# ── GLB builder ───────────────────────────────────────────────────────────────

class _GLBBuilder:
    """Accumulates glTF structures and emits a GLB binary."""

    # glTF magic / chunk types
    _MAGIC_GLTF = 0x46546C67
    _CHUNK_JSON = 0x4E4F534A
    _CHUNK_BIN  = 0x004E4942

    def __init__(self):
        self._buf      = bytearray()
        self._bviews   = []
        self._accs     = []
        self.nodes     = []
        self.meshes    = []
        self.skins     = []
        self.animations = []
        self.images    = []
        self.textures  = []
        self.samplers  = []
        self.materials_gltf = []
        self.scene_nodes: List[int] = []

    # ── buffer helpers ────────────────────────────────────────────────────────

    def _add_bview(self, data: bytes, target: int = 0) -> int:
        while len(self._buf) % 4:
            self._buf.append(0)
        offset = len(self._buf)
        self._buf.extend(data)
        # "buffer" is REQUIRED by glTF 2.0 spec — omitting it crashes UE5 GLTFCore
        bv = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target:
            bv["target"] = target
        self._bviews.append(bv)
        return len(self._bviews) - 1

    def _add_acc(self, bv: int, comp: int, count: int, typ: str,
                 min_v=None, max_v=None) -> int:
        acc = {"bufferView": bv, "componentType": comp,
               "count": count, "type": typ}
        if min_v is not None:
            acc["min"] = min_v
            acc["max"] = max_v
        self._accs.append(acc)
        return len(self._accs) - 1

    def add_f32_vec3(self, triples, target=0) -> int:
        data = _pack_f32(triples)
        xs = [t[0] for t in triples]
        ys = [t[1] for t in triples]
        zs = [t[2] for t in triples]
        bv = self._add_bview(data, target)
        return self._add_acc(bv, COMP_FLOAT, len(triples), "VEC3",
                             [min(xs), min(ys), min(zs)],
                             [max(xs), max(ys), max(zs)])

    def add_f32_vec4(self, quads) -> int:
        data = _pack_f32(quads)
        bv = self._add_bview(data, TARGET_ARRAY)   # WEIGHTS_0 is vertex attribute data
        return self._add_acc(bv, COMP_FLOAT, len(quads), "VEC4")

    def add_f32_mat4(self, matrices) -> int:
        data = _pack_f32(matrices)
        bv = self._add_bview(data)
        return self._add_acc(bv, COMP_FLOAT, len(matrices), "MAT4")

    def add_u16_vec4(self, quads) -> int:
        data = _pack_u16(quads)
        bv = self._add_bview(data, TARGET_ARRAY)
        return self._add_acc(bv, COMP_U16, len(quads), "VEC4")

    def add_u16_indices(self, triples) -> int:
        data = _pack_u16(triples)
        bv = self._add_bview(data, TARGET_ELEMENT_ARRAY)
        return self._add_acc(bv, COMP_U16, len(triples) * 3, "SCALAR")

    def add_f32_vec2(self, pairs, target=0) -> int:
        data = _pack_f32(pairs)
        bv = self._add_bview(data, target)
        return self._add_acc(bv, COMP_FLOAT, len(pairs), "VEC2")

    # ── texture / material helpers ──────────────────────────────────────────────

    def add_png_material(self, png_bytes: bytes, name: str = "",
                         alpha_mask: bool = False, alpha_cutoff: float = 0.5,
                         double_sided: bool = False) -> int:
        """Embed a PNG, wrap it in a texture, and create a material referencing it.
        Returns the material index (for primitive['material'])."""
        if not self.samplers:
            self.samplers.append({"magFilter": 9729, "minFilter": 9987,
                                  "wrapS": 10497, "wrapT": 10497})
        bv = self._add_bview(png_bytes)
        img_idx = len(self.images)
        self.images.append({"bufferView": bv, "mimeType": "image/png"})
        tex_idx = len(self.textures)
        self.textures.append({"sampler": 0, "source": img_idx})
        mat = {
            "name": name or f"mat_{len(self.materials_gltf)}",
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": tex_idx},
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0,
            },
            "alphaMode": "MASK" if alpha_mask else "OPAQUE",
            "doubleSided": bool(double_sided),
        }
        if alpha_mask:
            mat["alphaCutoff"] = alpha_cutoff
        mat_idx = len(self.materials_gltf)
        self.materials_gltf.append(mat)
        return mat_idx

    # ── assembly ──────────────────────────────────────────────────────────────

    def build_glb(self) -> bytes:
        gltf = {
            "asset": {"version": "2.0", "generator": "rose_to_gltf"},
            "scene": 0,
            "scenes": [{"name": "Scene", "nodes": self.scene_nodes}],
            "nodes": self.nodes,
            "accessors": self._accs,
            "bufferViews": self._bviews,
            "buffers": [{"byteLength": len(self._buf)}],
        }
        if self.meshes:
            gltf["meshes"] = self.meshes
        if self.skins:
            gltf["skins"] = self.skins
        if self.animations:
            gltf["animations"] = self.animations
        if self.materials_gltf:
            gltf["materials"] = self.materials_gltf
        if self.textures:
            gltf["textures"] = self.textures
        if self.images:
            gltf["images"] = self.images
        if self.samplers:
            gltf["samplers"] = self.samplers

        json_bytes = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
        json_padded = _pad4(json_bytes, 0x20)   # spaces
        bin_padded  = _pad4(bytes(self._buf), 0) # zeros

        chunks = (
            struct.pack('<II', len(json_padded), self._CHUNK_JSON) + json_padded +
            struct.pack('<II', len(bin_padded),  self._CHUNK_BIN)  + bin_padded
        )
        total = 12 + len(chunks)
        header = struct.pack('<III', self._MAGIC_GLTF, 2, total)
        return header + chunks

# ── main conversion ───────────────────────────────────────────────────────────

def convert(zmd_path: str, zms_paths: List[str], output_path: str) -> None:
    """
    Convert one ZMD + zero-or-more ZMS files into a single GLB file.

    Each ZMS becomes one mesh section (primitive).  All ZMD bones are embedded
    as a skin with pre-computed inverse-bind matrices so UE5 Interchange can
    create a proper SkeletalMesh + Skeleton asset pair.
    """
    zmd = parse_zmd(zmd_path)
    g   = _GLBBuilder()

    # ── 1. Build bone nodes ───────────────────────────────────────────────────
    world_mats = _bone_world_mats(zmd)

    children_of: dict = {i: [] for i in range(len(zmd.bones))}
    roots = []
    for bone in zmd.bones:
        if bone.parent_id == bone.index:
            roots.append(bone.index)
        else:
            children_of[bone.parent_id].append(bone.index)

    bone_node_start = 0   # bone i → glTF node (bone_node_start + i)

    for bone in zmd.bones:
        tx, ty, tz = _pos_r2g(*bone.translation)
        gx, gy, gz, gw = _quat_r2g(*bone.rotation)
        node = {
            "name": bone.name,
            "translation": [tx, ty, tz],
            "rotation":    [gx, gy, gz, gw],
        }
        kids = children_of[bone.index]
        if kids:
            node["children"] = [bone_node_start + k for k in kids]
        g.nodes.append(node)

    # ── 2. Inverse-bind matrices ──────────────────────────────────────────────
    ibm_mats = [_mat4_rigid_inv(m) for m in world_mats]
    ibm_acc  = g.add_f32_mat4(ibm_mats)

    # ── 3. Skin ───────────────────────────────────────────────────────────────
    skin = {
        "name": os.path.basename(zmd_path),
        "skeleton": bone_node_start + roots[0],
        "joints":   list(range(bone_node_start, bone_node_start + len(zmd.bones))),
        "inverseBindMatrices": ibm_acc,
    }
    g.skins.append(skin)

    # ── 4. Scene nodes: bone roots ────────────────────────────────────────────
    g.scene_nodes = [bone_node_start + r for r in roots]

    # ── 5. Mesh primitives from ZMS files ─────────────────────────────────────
    if zms_paths:
        primitives = []
        for zms_path in zms_paths:
            zms = parse_zms(zms_path)
            prim = _build_primitive(g, zms)
            if prim is not None:
                primitives.append(prim)

        if primitives:
            mesh_idx  = len(g.meshes)
            mesh_node = len(g.nodes)
            g.meshes.append({"name": "Mesh", "primitives": primitives})
            g.nodes.append({"name": "Mesh", "mesh": mesh_idx, "skin": 0})
            g.scene_nodes.append(mesh_node)

    # ── 6. Write GLB ──────────────────────────────────────────────────────────
    glb = g.build_glb()
    with open(output_path, 'wb') as f:
        f.write(glb)
    n_bones = len(zmd.bones)
    n_verts = sum(len(parse_zms(p).vertices) for p in zms_paths) if zms_paths else 0
    print(f"  → {output_path}  ({n_bones} bones, {len(zms_paths)} mesh(es), {n_verts} verts)")


def _mat_apply_point(M, p):
    """Apply column-major 4×4 M to a point (with translation)."""
    x, y, z = p
    return (M[0]*x + M[4]*y + M[8]*z + M[12],
            M[1]*x + M[5]*y + M[9]*z + M[13],
            M[2]*x + M[6]*y + M[10]*z + M[14])


def _mat_apply_dir(M, p):
    """Apply column-major 4×4 M to a direction (rotation only)."""
    x, y, z = p
    return (M[0]*x + M[4]*y + M[8]*z,
            M[1]*x + M[5]*y + M[9]*z,
            M[2]*x + M[6]*y + M[10]*z)


def _build_primitive(g: _GLBBuilder, zms: ZMS, pin_bone: int = 0,
                     pin_matrix: Optional[List[float]] = None,
                     material_index: Optional[int] = None,
                     face_range: Optional[tuple] = None) -> Optional[dict]:
    """Build one glTF primitive from a ZMS mesh.  Returns None if mesh is empty.

    material_index: optional glTF material index to assign to this primitive.
    face_range:     optional (start_face, num_faces) to emit only a subset of the
                    mesh's triangles (used to split the face mesh into its
                    material groups — closed-eye / main / open-eye — so each is a
                    separate, individually toggleable UE section for blinking).
                    Vertices are shared; unreferenced ones are harmless in glTF.

    pin_bone:   for non-skinned (rigid) meshes, the bone to attach every vertex to.
    pin_matrix: for non-skinned meshes authored in bone-LOCAL space (face/hair/cap),
                the attachment bone's bind-world matrix (glTF space).  Pre-applying
                it makes glTF's inverse-bind cancel so the part lands on, and
                follows, that bone instead of collapsing to the origin.
    """
    verts = zms.vertices
    faces = zms.faces
    if face_range is not None:
        s, n = face_range
        faces = faces[s:s + n]
    if not verts or not faces:
        return None

    is_skinned = zms.has_skin() and verts[0].blend_indices is not None
    use_pin_matrix = (not is_skinned) and (pin_matrix is not None)

    # ── positions ─────────────────────────────────────────────────────────────
    # ZMS mesh vertices are already authored in METRES (the engine applies no
    # scale to them — only the ZMD skeleton and ZMO motion are cm→m scaled).
    positions = [_pos_r2g(*v.position) for v in verts]
    if use_pin_matrix:
        positions = [_mat_apply_point(pin_matrix, p) for p in positions]
    pos_acc   = g.add_f32_vec3(positions, TARGET_ARRAY)
    attrs     = {"POSITION": pos_acc}

    # ── normals ───────────────────────────────────────────────────────────────
    if zms.has_normals() and verts[0].normal is not None:
        normals = [_normal_r2g(*v.normal) for v in verts]
        if use_pin_matrix:
            normals = [_mat_apply_dir(pin_matrix, n) for n in normals]
        attrs["NORMAL"] = g.add_f32_vec3(normals, TARGET_ARRAY)

    # ── UV0 ───────────────────────────────────────────────────────────────────
    # ROSE (DirectX) and glTF both use a top-left UV origin, so UVs are used
    # as-is — flipping V vertically mirrors the texture and scrambles it.
    if verts[0].uvs:
        uvs = [(v.uvs[0][0], v.uvs[0][1]) for v in verts]
        attrs["TEXCOORD_0"] = g.add_f32_vec2(uvs, TARGET_ARRAY)

    # ── skinning ──────────────────────────────────────────────────────────────
    # Every mesh must have JOINTS_0 / WEIGHTS_0 so that:
    #   • mesh node can keep "skin": 0  (required for SkeletalMesh import)
    #   • GLTFCore never encounters a skin whose associated mesh has no joint data
    # For ZMS files without blend weights (rigid accessories), fake-skin every
    # vertex to bone 0 (root) with weight 1.0.  Bone 0 is a valid placeholder —
    # the real attachment bone can be wired up later via ZSC dummy-point data.
    if is_skinned:
        joints_data  = []
        weights_data = []
        for v in verts:
            bi = v.blend_indices
            bw = v.blend_weights if v.blend_weights else (1.0, 0.0, 0.0, 0.0)
            global_bi = tuple(zms.bone_indices[idx] if idx < len(zms.bone_indices) else 0
                              for idx in bi)
            joints_data.append(global_bi)
            weights_data.append(bw)
    else:
        # No skinning in ZMS (rigid accessory: face/hair/cap) — pin entire mesh
        # to its attachment bone (pin_bone).  Pinning to the wrong bone (e.g. 0)
        # makes the part follow the root instead of, say, the head, so it drifts
        # off under animation.
        joints_data  = [(pin_bone, 0, 0, 0)] * len(verts)
        weights_data = [(1.0, 0.0, 0.0, 0.0)] * len(verts)

    attrs["JOINTS_0"]  = g.add_u16_vec4(joints_data)
    attrs["WEIGHTS_0"] = g.add_f32_vec4(weights_data)

    # ── indices ───────────────────────────────────────────────────────────────
    # A reflecting BASIS (det < 0) reverses triangle winding, which would flip
    # normals inward — swap two indices per face to restore outward winding.
    if BASIS_FLIPS_WINDING:
        faces = [(a, c, b) for (a, b, c) in faces]
    idx_acc = g.add_u16_indices(faces)
    prim = {"attributes": attrs, "indices": idx_acc}
    if material_index is not None:
        prim["material"] = material_index
    return prim

# ── CLI ───────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Convert ROSE ZMD+ZMS to glTF 2.0 GLB")
    ap.add_argument("zmd", help="Path to .ZMD skeleton file")
    ap.add_argument("zms", nargs="*", help="Path(s) to .ZMS mesh file(s)")
    ap.add_argument("-o", "--output", required=True, help="Output .glb path")
    args = ap.parse_args()
    convert(args.zmd, args.zms, args.output)


if __name__ == "__main__":
    main()
