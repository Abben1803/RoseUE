"""Parser for CHR (ROSE character/NPC definition format).

From src/client/cmodelchar.cpp (CCharModelDATA::Load + CCharMODEL::Load_MOBorNPC):

File structure:
  int16 nSkelFileCNT
  per skeleton: null-terminated string (ZMD path)
  int16 nMotionFileCNT
  per motion: null-terminated string (ZMO path)
  int16 nEffectFileCNT
  per effect: null-terminated string (EFT path)
  int16 nModelCNT
  per model (CCharMODEL):
    uint8 is_valid    (0 = skip this entry, nonzero = read data below)
    int16 skel_index  (into skeleton file list)
    null-str name
    int16 nBodyPartCNT
    per body part: int16 part_model_idx  (into PART_NPC.ZSC model list)
    int16 nAniCNT
    per animation: int16 ani_type_idx, int16 motion_file_idx
    int16 nBoneEffectCNT
    per bone effect: int16 bone_idx, int16 effect_file_idx
"""
from dataclasses import dataclass, field
from typing import Dict, List, Optional
from ..reader import BinaryReader


@dataclass
class BoneEffect:
    bone_idx: int
    effect_file_idx: int


@dataclass
class CHRModel:
    is_valid: bool
    skel_index: int
    name: str
    body_part_indices: List[int] = field(default_factory=list)
    animations: Dict[int, int] = field(default_factory=dict)
    bone_effects: List[BoneEffect] = field(default_factory=list)


@dataclass
class CHR:
    skeleton_files: List[str] = field(default_factory=list)
    motion_files: List[str] = field(default_factory=list)
    effect_files: List[str] = field(default_factory=list)
    models: List[CHRModel] = field(default_factory=list)

    def skeleton_path(self, model_idx: int) -> Optional[str]:
        m = self.models[model_idx] if model_idx < len(self.models) else None
        if m and m.is_valid and m.skel_index < len(self.skeleton_files):
            return self.skeleton_files[m.skel_index]
        return None

    def motion_path(self, motion_idx: int) -> Optional[str]:
        if motion_idx < len(self.motion_files):
            return self.motion_files[motion_idx]
        return None


def parse(path: str) -> CHR:
    r = BinaryReader.from_file(path)
    chr_ = CHR()

    n_skel = r.i16()
    for _ in range(n_skel):
        chr_.skeleton_files.append(r.chr_str())

    n_motion = r.i16()
    for _ in range(n_motion):
        chr_.motion_files.append(r.chr_str())

    n_eft = r.i16()
    for _ in range(n_eft):
        chr_.effect_files.append(r.chr_str())

    n_models = r.i16()
    for _ in range(n_models):
        is_valid = r.u8()
        if is_valid == 0:
            chr_.models.append(CHRModel(is_valid=False, skel_index=-1, name=""))
            continue

        skel_idx = r.i16()
        name     = r.chr_str()

        n_parts = r.i16()
        body_parts = [r.i16() for _ in range(n_parts)]

        n_ani = r.i16()
        animations: Dict[int, int] = {}
        for _ in range(n_ani):
            ani_type   = r.i16()
            motion_idx = r.i16()
            if ani_type >= 0:
                animations[ani_type] = motion_idx

        n_bone_eft = r.i16()
        bone_effects: List[BoneEffect] = []
        for _ in range(n_bone_eft):
            bidx = r.i16()
            eidx = r.i16()
            bone_effects.append(BoneEffect(bone_idx=bidx, effect_file_idx=eidx))

        chr_.models.append(CHRModel(
            is_valid=True,
            skel_index=skel_idx,
            name=name,
            body_part_indices=body_parts,
            animations=animations,
            bone_effects=bone_effects,
        ))

    return chr_
