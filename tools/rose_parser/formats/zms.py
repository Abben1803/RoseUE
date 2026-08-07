"""Parser for ZMS (ROSE mesh geometry format).

Two loader variants identified in src/engine/src/zz_mesh_tool.cpp:

v5/v6 (load_mesh_6):
  null-str magic "ZMS0005" or "ZMS0006"
  uint32 vertex_format
  float3 pmin, float3 pmax  (ZZ_XFORM_IN applied for v5/v6 in engine, stored raw)
  uint32 num_bones; per: uint32 idx, uint32 bone_index
  uint32 num_verts
  per vert: uint32 idx, float3 position
  [VF_NORMAL]  per vert: uint32 idx, float3 normal
  [VF_COLOR]   per vert: uint32 idx, float4 rgba (0..1)
  [VF_SKIN]    per vert: uint32 idx, float4 blend_weights, uint32×4 blend_indices
  [VF_TANGENT] per vert: uint32 idx, float3 tangent
  per UV chan: per vert: uint32 idx, float2 uv
  uint32 num_faces; per: uint32 idx, uint32×3 vertex_indices
  [v6] uint32 num_matids; per: uint32 mat_idx, uint32 matid_numfaces

v7/v8 (load_mesh_8):
  null-str magic "ZMS0007" or "ZMS0008"
  uint32 vertex_format
  float3 pmin, float3 pmax
  uint16 num_bones; per: uint16 bone_index  (no idx prefix)
  uint16 num_verts
  per vert: float3 position               (no idx prefix)
  [VF_NORMAL]  per vert: float3 normal
  [VF_COLOR]   per vert: float4 rgba
  [VF_SKIN]    per vert: float4 blend_weights, uint16×4 blend_indices
  [VF_TANGENT] per vert: float3 tangent
  per UV chan:  per vert: float2 uv
  uint16 num_faces; per: uint16×3 vertex_indices (no idx prefix)
  uint16 num_matids; per: uint16 matid_numfaces
  uint16 num_strip_indices; strip_indices[]
  [v8] uint16 pool_type
"""
from dataclasses import dataclass, field
from typing import List, Optional, Tuple
from ..reader import BinaryReader

Vec2 = Tuple[float, float]
Vec3 = Tuple[float, float, float]
Vec4 = Tuple[float, float, float, float]

# Vertex format flags (from src/engine/include/zz_vertex_format.h)
VF_NONE         = 1 << 0
VF_POSITION     = 1 << 1
VF_NORMAL       = 1 << 2
VF_COLOR        = 1 << 3
VF_BLEND_WEIGHT = 1 << 4
VF_BLEND_INDEX  = 1 << 5
VF_TANGENT      = 1 << 6
VF_UV0          = 1 << 7
VF_UV1          = 1 << 8
VF_UV2          = 1 << 9
VF_UV3          = 1 << 10


def _uv_channel_count(vf: int) -> int:
    count = 0
    for flag in (VF_UV0, VF_UV1, VF_UV2, VF_UV3):
        if vf & flag:
            count += 1
    return count


@dataclass
class ZMSVertex:
    position: Vec3
    normal: Optional[Vec3] = None
    color: Optional[Vec4] = None
    blend_weights: Optional[Vec4] = None
    blend_indices: Optional[Tuple[int, int, int, int]] = None
    tangent: Optional[Vec3] = None
    uvs: List[Vec2] = field(default_factory=list)


@dataclass
class MatID:
    mat_index: int
    num_faces: int


@dataclass
class ZMS:
    version: int
    vertex_format: int
    bounds_min: Vec3
    bounds_max: Vec3
    bone_indices: List[int] = field(default_factory=list)
    vertices: List[ZMSVertex] = field(default_factory=list)
    faces: List[Tuple[int, int, int]] = field(default_factory=list)
    mat_ids: List[MatID] = field(default_factory=list)
    pool_type: int = 0

    def has_normals(self) -> bool:
        return bool(self.vertex_format & VF_NORMAL)

    def has_skin(self) -> bool:
        return bool(self.vertex_format & VF_BLEND_WEIGHT)

    def uv_channel_count(self) -> int:
        return _uv_channel_count(self.vertex_format)


def _parse_v56(r: BinaryReader, version: int) -> ZMS:
    vf = r.u32()
    pmin = r.vec3()
    pmax = r.vec3()

    num_bones = r.u32()
    bone_indices = []
    for _ in range(num_bones):
        _idx = r.u32()
        bone_indices.append(r.u32())

    num_verts = r.u32()
    positions: List[Vec3] = []
    for _ in range(num_verts):
        _idx = r.u32()
        positions.append(r.vec3())

    normals: List[Optional[Vec3]] = [None] * num_verts
    if vf & VF_NORMAL:
        for i in range(num_verts):
            _idx = r.u32()
            normals[i] = r.vec3()

    colors: List[Optional[Vec4]] = [None] * num_verts
    if vf & VF_COLOR:
        for i in range(num_verts):
            _idx = r.u32()
            colors[i] = r.vec4()

    blend_weights: List[Optional[Vec4]] = [None] * num_verts
    blend_indices: List[Optional[Tuple[int, int, int, int]]] = [None] * num_verts
    if (vf & VF_BLEND_WEIGHT) and (vf & VF_BLEND_INDEX):
        for i in range(num_verts):
            _idx = r.u32()
            blend_weights[i] = r.vec4()
            blend_indices[i] = r.uvec4_u32()

    tangents: List[Optional[Vec3]] = [None] * num_verts
    if vf & VF_TANGENT:
        for i in range(num_verts):
            _idx = r.u32()
            tangents[i] = r.vec3()

    num_uv = _uv_channel_count(vf)
    uv_data: List[List[Vec2]] = [[] for _ in range(num_verts)]
    for _ch in range(num_uv):
        for i in range(num_verts):
            _idx = r.u32()
            uv_data[i].append(r.vec2())

    num_faces = r.u32()
    faces: List[Tuple[int, int, int]] = []
    for _ in range(num_faces):
        _fidx = r.u32()
        a, b, c = r.uvec3_u32()
        faces.append((a, b, c))

    mat_ids: List[MatID] = []
    if version >= 6:
        num_matids = r.u32()
        for i in range(num_matids):
            mat_idx = r.u32()
            mf = r.u32()
            mat_ids.append(MatID(mat_index=mat_idx, num_faces=mf))

    vertices = [
        ZMSVertex(
            position=positions[i],
            normal=normals[i],
            color=colors[i],
            blend_weights=blend_weights[i],
            blend_indices=blend_indices[i],
            tangent=tangents[i],
            uvs=uv_data[i],
        )
        for i in range(num_verts)
    ]

    return ZMS(
        version=version,
        vertex_format=vf,
        bounds_min=pmin,
        bounds_max=pmax,
        bone_indices=bone_indices,
        vertices=vertices,
        faces=faces,
        mat_ids=mat_ids,
    )


def _parse_v78(r: BinaryReader, version: int) -> ZMS:
    vf = r.u32()
    pmin = r.vec3()
    pmax = r.vec3()

    num_bones = r.u16()
    bone_indices = [r.u16() for _ in range(num_bones)]

    num_verts = r.u16()
    positions: List[Vec3] = [r.vec3() for _ in range(num_verts)]

    normals: List[Optional[Vec3]] = [None] * num_verts
    if vf & VF_NORMAL:
        for i in range(num_verts):
            normals[i] = r.vec3()

    colors: List[Optional[Vec4]] = [None] * num_verts
    if vf & VF_COLOR:
        for i in range(num_verts):
            colors[i] = r.vec4()

    blend_weights: List[Optional[Vec4]] = [None] * num_verts
    blend_indices: List[Optional[Tuple[int, int, int, int]]] = [None] * num_verts
    if (vf & VF_BLEND_WEIGHT) and (vf & VF_BLEND_INDEX):
        for i in range(num_verts):
            blend_weights[i] = r.vec4()
            blend_indices[i] = r.uvec4_u16()

    tangents: List[Optional[Vec3]] = [None] * num_verts
    if vf & VF_TANGENT:
        for i in range(num_verts):
            tangents[i] = r.vec3()

    num_uv = _uv_channel_count(vf)
    uv_data: List[List[Vec2]] = [[] for _ in range(num_verts)]
    for _ch in range(num_uv):
        for i in range(num_verts):
            uv_data[i].append(r.vec2())

    num_faces = r.u16()
    faces: List[Tuple[int, int, int]] = []
    for _ in range(num_faces):
        faces.append(r.uvec3_u16())

    mat_ids: List[MatID] = []
    num_matids = r.u16()
    for i in range(num_matids):
        mf = r.u16()
        mat_ids.append(MatID(mat_index=i, num_faces=mf))

    # strip indices
    num_strip = r.u16()
    if num_strip > 0:
        r.skip(num_strip * 2)

    pool_type = 0
    if version >= 8 and not r.at_end():
        pool_type = r.u16()

    vertices = [
        ZMSVertex(
            position=positions[i],
            normal=normals[i],
            color=colors[i],
            blend_weights=blend_weights[i],
            blend_indices=blend_indices[i],
            tangent=tangents[i],
            uvs=uv_data[i],
        )
        for i in range(num_verts)
    ]

    return ZMS(
        version=version,
        vertex_format=vf,
        bounds_min=pmin,
        bounds_max=pmax,
        bone_indices=bone_indices,
        vertices=vertices,
        faces=faces,
        mat_ids=mat_ids,
        pool_type=pool_type,
    )


def parse(path: str) -> ZMS:
    r = BinaryReader.from_file(path)
    magic = r.null_str()

    version_map = {
        "ZMS0005": (5, _parse_v56),
        "ZMS0006": (6, _parse_v56),
        "ZMS0007": (7, _parse_v78),
        "ZMS0008": (8, _parse_v78),
    }

    if magic not in version_map:
        raise ValueError(f"Unknown ZMS version: '{magic}' in {path}")

    version, loader = version_map[magic]
    return loader(r, version)
