"""Parser for ZMD (ROSE skeleton/bone hierarchy format).

From src/engine/src/zz_skeleton.cpp:
  null-terminated magic: "ZMD0002" or "ZMD0003"
  uint32 num_bones
  per bone:
    uint32   parent_id   (0 = root for bone 0; others reference by index)
    null-str name
    float3   translation (x,y,z)  -- multiply by ZZ_SCALE_IN (1/100) for meters
    float4   rotation    (w,x,y,z) quaternion
  uint32 num_dummies
  per dummy:
    null-str name
    uint32   parent_id
    float3   translation
    [v3 only] float4 rotation (w,x,y,z)
"""
from dataclasses import dataclass, field
from typing import List, Optional, Tuple
from ..reader import BinaryReader

Vec3 = Tuple[float, float, float]
Vec4 = Tuple[float, float, float, float]

SCALE_IN = 1.0 / 100.0


@dataclass
class Bone:
    index: int
    parent_id: int
    name: str
    translation: Vec3
    rotation: Vec4  # (w, x, y, z)


@dataclass
class Dummy:
    index: int
    parent_id: int
    name: str
    translation: Vec3
    rotation: Optional[Vec4] = None  # only in ZMD0003


@dataclass
class ZMD:
    version: int  # 2 or 3
    bones: List[Bone] = field(default_factory=list)
    dummies: List[Dummy] = field(default_factory=list)

    def bone_by_name(self, name: str) -> Optional[Bone]:
        for b in self.bones:
            if b.name == name:
                return b
        return None


def parse(path: str) -> ZMD:
    r = BinaryReader.from_file(path)

    magic = r.fixed_str(7)  # exactly 7 bytes, no null in file
    if magic == "ZMD0002":
        version = 2
    elif magic == "ZMD0003":
        version = 3
    else:
        raise ValueError(f"Unknown ZMD version: '{magic}' in {path}")

    zmd = ZMD(version=version)

    num_bones = r.u32()
    for i in range(num_bones):
        parent_id = r.u32()
        name = r.null_str()
        tx, ty, tz = r.vec3()
        w, x, y, z = r.quat()
        tx *= SCALE_IN
        ty *= SCALE_IN
        tz *= SCALE_IN
        zmd.bones.append(Bone(
            index=i,
            parent_id=parent_id,
            name=name,
            translation=(tx, ty, tz),
            rotation=(w, x, y, z),
        ))

    num_dummies = r.u32()
    for i in range(num_dummies):
        name = r.null_str()
        parent_id = r.u32()
        tx, ty, tz = r.vec3()
        tx *= SCALE_IN
        ty *= SCALE_IN
        tz *= SCALE_IN
        rot = None
        if version >= 3:
            rot = r.quat()
        zmd.dummies.append(Dummy(
            index=i,
            parent_id=parent_id,
            name=name,
            translation=(tx, ty, tz),
            rotation=rot,
        ))

    return zmd
