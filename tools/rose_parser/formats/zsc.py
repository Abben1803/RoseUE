"""Parser for ZSC (ROSE scene container — mesh+material+effect scene definition).

From src/client/io_model.h (CModelDATA<T>::Load) and io_model.cpp (CFixedPART::Load):

File structure:
  int16 nMeshFileCNT
  per mesh: null-terminated string (ZMS file path)
  int16 nMatFileCNT
  per material:
    null-str texture_path
    int16 is_skin, is_alpha, is_2side, alpha_test, alpha_ref, z_test, z_write, blend_type, specular
    float  alpha_value
    int16  glow_type
    float3 glow_color
  int16 nEftFileCNT
  per effect: null-str path
  int16 nModelCNT
  per model (CMODEL):
    int32 cylinder_radius, cylinder_x, cylinder_y
    int16 nPartCNT
    per part:
      int16 mesh_id
      int16 material_id
      tag-value stream (see SWITCH_* constants below)
    int16 nDummyPointCNT
    per dummy point:
      int16 effect_list_idx
      int16 effect_type
      tag-value stream (subset)
    float3 bbox_min
    float3 bbox_max

Tag constants (from io_model.cpp):
  SWITCH_NULL=0  SWITCH_POS=1  SWITCH_ROT=2  SWITCH_SCALE=3
  SWITCH_ROTAXIS=4  SWITCH_BONEIDX=5  SWITCH_DUMMYIDX=6  SWITCH_PARENT=7
  SWITCH_ANI=8 .. 28 (animation file paths, one per animation type)
  SWITCH_COLLISION=29  SWITCH_CNST_ANI=30  SWITCH_RANGE_SET=31
  SWITCH_BUSE_LIGHTMAP=32
"""
import struct
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple
from ..reader import BinaryReader

Vec3 = Tuple[float, float, float]
Vec4 = Tuple[float, float, float, float]

SWITCH_NULL         = 0
SWITCH_POS          = 1
SWITCH_ROT          = 2
SWITCH_SCALE        = 3
SWITCH_ROTAXIS      = 4
SWITCH_BONEIDX      = 5
SWITCH_DUMMYIDX     = 6
SWITCH_PARENT       = 7
SWITCH_ANI          = 8
MAX_MESH_ANI_TYPE   = 21
SWITCH_COLLISION    = SWITCH_ANI + MAX_MESH_ANI_TYPE   # 29
SWITCH_CNST_ANI     = SWITCH_COLLISION + 1              # 30
SWITCH_RANGE_SET    = SWITCH_COLLISION + 2              # 31
SWITCH_BUSE_LIGHTMAP= SWITCH_COLLISION + 3              # 32

ANI_NAMES = [
    "mob_stop", "mob_move", "mob_attack", "mob_hit", "mob_die", "mob_etc",
    "avt_stop1", "avt_stop2", "avt_walk", "avt_run", "avt_sitting", "avt_sit",
    "avt_standup", "avt_stop3", "avt_attack", "avt_hit", "avt_fall",
    "avt_die", "avt_raise", "avt_jump1", "avt_jump2",
]


@dataclass
class ZSCMaterial:
    texture_path: str
    is_skin: bool = False
    is_alpha: bool = False
    is_2side: bool = False
    alpha_test: int = 0
    alpha_ref: int = 0
    z_test: int = 1
    z_write: int = 1
    blend_type: int = 0
    specular: int = 0
    alpha_value: float = 1.0
    glow_type: int = 0
    glow_color: Vec3 = (0.0, 0.0, 0.0)


@dataclass
class ZSCPart:
    mesh_id: int
    material_id: int
    position: Vec3 = (0.0, 0.0, 0.0)
    rotation: Vec4 = (1.0, 0.0, 0.0, 0.0)
    scale: Vec3 = (1.0, 1.0, 1.0)
    rot_axis: Vec4 = (1.0, 0.0, 0.0, 0.0)
    bone_idx: int = -1
    dummy_idx: int = -1
    parent_idx: int = -1
    animations: Dict[str, str] = field(default_factory=dict)
    collision_level: int = 0
    cnst_anim_file: str = ""
    range_set: int = 0
    use_lightmap: bool = False


@dataclass
class ZSCDummyPoint:
    effect_idx: int
    effect_type: int
    parent_idx: int = -1
    position: Vec3 = (0.0, 0.0, 0.0)
    rotation: Vec4 = (1.0, 0.0, 0.0, 0.0)
    scale: Vec3 = (1.0, 1.0, 1.0)


@dataclass
class ZSCModel:
    cylinder_radius: int
    cylinder_x: int
    cylinder_y: int
    parts: List[ZSCPart] = field(default_factory=list)
    dummy_points: List[ZSCDummyPoint] = field(default_factory=list)
    bbox_min: Vec3 = (0.0, 0.0, 0.0)
    bbox_max: Vec3 = (0.0, 0.0, 0.0)


@dataclass
class ZSC:
    mesh_files: List[str] = field(default_factory=list)
    materials: List[ZSCMaterial] = field(default_factory=list)
    effect_files: List[str] = field(default_factory=list)
    models: List[ZSCModel] = field(default_factory=list)


def _read_tags_fixed_part(r: BinaryReader) -> ZSCPart:
    part = ZSCPart(mesh_id=-1, material_id=-1)
    while not r.at_end():
        tag = r.u8()
        if tag == SWITCH_NULL:
            break
        length = r.u8()
        end_pos = r.tell() + length

        if tag == SWITCH_POS:
            part.position = r.vec3()
        elif tag == SWITCH_ROT:
            part.rotation = r.quat()
        elif tag == SWITCH_SCALE:
            part.scale = r.vec3()
        elif tag == SWITCH_ROTAXIS:
            part.rot_axis = r.quat()
        elif tag == SWITCH_BONEIDX:
            part.bone_idx = r.i16()
        elif tag == SWITCH_DUMMYIDX:
            part.dummy_idx = r.i16()
        elif tag == SWITCH_PARENT:
            raw = r.i16()
            part.parent_idx = raw - 1
        elif SWITCH_ANI <= tag < SWITCH_ANI + MAX_MESH_ANI_TYPE:
            ani_type = tag - SWITCH_ANI
            ani_name = ANI_NAMES[ani_type] if ani_type < len(ANI_NAMES) else f"ani_{ani_type}"
            raw = r.read_bytes(length)
            part.animations[ani_name] = raw.decode("latin-1", errors="replace").rstrip("\x00")
            r.seek(end_pos)
            continue
        elif tag == SWITCH_COLLISION:
            part.collision_level = r.i16()
        elif tag == SWITCH_CNST_ANI:
            raw = r.read_bytes(length)
            part.cnst_anim_file = raw.decode("latin-1", errors="replace").rstrip("\x00")
            r.seek(end_pos)
            continue
        elif tag == SWITCH_RANGE_SET:
            part.range_set = r.i16()
        elif tag == SWITCH_BUSE_LIGHTMAP:
            part.use_lightmap = bool(r.i16())
        else:
            r.seek(end_pos)
            continue

        r.seek(end_pos)

    return part


def _read_tags_dummy_point(r: BinaryReader) -> ZSCDummyPoint:
    dp = ZSCDummyPoint(effect_idx=-1, effect_type=0)
    while not r.at_end():
        tag = r.u8()
        if tag == SWITCH_NULL:
            break
        length = r.u8()
        end_pos = r.tell() + length

        if tag == SWITCH_PARENT:
            dp.parent_idx = r.i16() - 1
        elif tag == SWITCH_POS:
            dp.position = r.vec3()
        elif tag == SWITCH_ROT:
            dp.rotation = r.quat()
        elif tag == SWITCH_SCALE:
            dp.scale = r.vec3()

        r.seek(end_pos)

    return dp


def parse(path: str) -> ZSC:
    r = BinaryReader.from_file(path)
    zsc = ZSC()

    n_mesh = r.u16()
    for _ in range(n_mesh):
        zsc.mesh_files.append(r.null_str())

    n_mat = r.u16()
    for _ in range(n_mat):
        tex = r.null_str()
        is_skin  = bool(r.i16())
        is_alpha = bool(r.i16())
        is_2side = bool(r.i16())
        alpha_test  = r.i16()
        alpha_ref   = r.i16()
        z_write     = r.i16()   # order per rose-lib zsc.rs: z_write BEFORE z_test
        z_test      = r.i16()
        blend_type  = r.i16()
        specular    = r.i16()
        alpha_value = r.f32()
        glow_type   = r.i16()
        glow_color  = r.vec3()
        zsc.materials.append(ZSCMaterial(
            texture_path=tex,
            is_skin=is_skin, is_alpha=is_alpha, is_2side=is_2side,
            alpha_test=alpha_test, alpha_ref=alpha_ref,
            z_test=z_test, z_write=z_write,
            blend_type=blend_type, specular=specular,
            alpha_value=alpha_value,
            glow_type=glow_type, glow_color=glow_color,
        ))

    n_eft = r.u16()
    for _ in range(n_eft):
        zsc.effect_files.append(r.null_str())

    n_models = r.u16()
    for _ in range(n_models):
        cyl_r = r.u32()
        cyl_x, cyl_y = r.i32(), r.i32()
        n_parts = r.u16()

        model = ZSCModel(
            cylinder_radius=cyl_r,
            cylinder_x=cyl_x,
            cylinder_y=cyl_y,
        )

        # Empty objects have no parts, no effects, no bbox (Rust reference skips all)
        if n_parts == 0:
            zsc.models.append(model)
            continue

        for _ in range(n_parts):
            mesh_id = r.u16()
            mat_id  = r.u16()
            part = _read_tags_fixed_part(r)
            part.mesh_id = mesh_id
            part.material_id = mat_id
            model.parts.append(part)

        n_dummy = r.u16()
        for _ in range(n_dummy):
            eft_idx  = r.u16()
            eft_type = r.u16()
            dp = _read_tags_dummy_point(r)
            dp.effect_idx = eft_idx
            dp.effect_type = eft_type
            model.dummy_points.append(dp)

        model.bbox_min = r.vec3()
        model.bbox_max = r.vec3()
        zsc.models.append(model)

    return zsc
