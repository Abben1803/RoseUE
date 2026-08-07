"""
ue5_fix_appearance_materials_zsc.py — ZSC-faithful blend state for the Modular
APPEARANCE parts (hair / face / faceitem, both genders).

These import via Interchange with M_GLTF MIs; hair GLBs carry alpha so the MIs
come in MASKED — and the Substrate M_GLTF masked path renders BLACK (the
foliage bug), which is the black/dithered patch behind the character's head.
Per the ZSC, 97% of hair materials are OPAQUE (alpha in the DDS is shine, not
coverage).

Slot mapping: /Game/Characters/Modular/<G>/<slot>_<id> was built from
LIST_<g><SLOT>.ZSC.objects[id] parts in order (STB row id == ZSC object id).

Run headless, editor CLOSED, -nullrhi fine.
"""
import os
import sys
import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(TOOLS), "mapforge"))
from rose_zsc import read_zsc                                   # noqa: E402

ARUA = r"C:\QQ-iROSE Online\extracted\3DDATA"
MOD_ROOT = "/Game/Characters/Modular"

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary

PACKS = {
    ("Male", "hair"): "AVATAR/LIST_MHAIR.ZSC",
    ("Male", "face"): "AVATAR/LIST_MFACE.ZSC",
    ("Female", "hair"): "AVATAR/LIST_WHAIR.ZSC",
    ("Female", "face"): "AVATAR/LIST_WFACE.ZSC",
    ("Male", "faceitem"): "AVATAR/LIST_FACEIEM.ZSC",
    ("Female", "faceitem"): "AVATAR/LIST_FACEIEM.ZSC",
}


def log(m):
    unreal.log(f"[appzsc] {m}")


def resolve(rel):
    parts = rel.split("/")
    cur = ARUA
    for p in parts:
        m = [e for e in os.listdir(cur) if e.lower() == p.lower()]
        if not m:
            return None
        cur = os.path.join(cur, m[0])
    return cur


def apply_flags(mi, f):
    ov = mi.get_editor_property("base_property_overrides")
    ov.set_editor_property("override_two_sided", True)
    ov.set_editor_property("two_sided", f["two_sided"])
    if f["alpha"] and f["alpha_test"]:
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        ov.set_editor_property("override_opacity_mask_clip_value", True)
        ov.set_editor_property("opacity_mask_clip_value", f["alpha_ref"] / 255.0)
        ME.set_material_instance_scalar_parameter_value(mi, "AlphaMode", 1.0)
    elif f["alpha"]:
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        ov.set_editor_property("override_opacity_mask_clip_value", True)
        ov.set_editor_property("opacity_mask_clip_value", 0.33)
        ME.set_material_instance_scalar_parameter_value(mi, "AlphaMode", 1.0)
    else:
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        ov.set_editor_property("override_opacity_mask_clip_value", False)
        ME.set_material_instance_scalar_parameter_value(mi, "AlphaMode", 0.0)
    mi.set_editor_property("base_property_overrides", ov)
    ME.update_material_instance(mi)


packs = {}
for key, rel in PACKS.items():
    p = resolve(rel)
    if p:
        packs[key] = read_zsc(p)
    else:
        log(f"missing pack {rel}")

n_set = n_skip = 0
seen = set()
for gender in ("Male", "Female"):
    for d in EAL.list_assets(f"{MOD_ROOT}/{gender}", recursive=False, include_folder=True):
        name = d.rstrip("/").rsplit("/", 1)[-1]
        slot = name.split("_", 1)[0].lower()
        if (gender, slot) not in packs:
            continue
        try:
            oid = int(name.split("_", 1)[1])
        except (ValueError, IndexError):
            continue
        zsc = packs[(gender, slot)]
        if not (0 <= oid < len(zsc.models)):
            continue
        parts = zsc.models[oid].parts
        smp = f"{MOD_ROOT}/{gender}/{name}/{name}/SkeletalMeshes/{name}"
        sm = EAL.load_asset(smp) if EAL.does_asset_exist(smp) else None
        if not sm or not isinstance(sm, unreal.SkeletalMesh):
            continue
        mats = sm.get_editor_property("materials")
        if len(mats) != len(parts):
            # GLB builds may split primitives (slot count differs). When every
            # ZSC part of the item shares IDENTICAL flags, the mapping doesn't
            # matter — apply the uniform flags to every slot.
            uniq = set()
            for part in parts:
                m0 = zsc.materials[part.mat_idx] if 0 <= part.mat_idx < len(zsc.materials) else None
                if m0:
                    uniq.add((bool(m0.is_alpha), bool(m0.alpha_test),
                              int(m0.alpha_ref), bool(m0.is_two_side)))
            if len(uniq) != 1:
                n_skip += 1
                continue
            a, at, ar, two = next(iter(uniq))
            for s in mats:
                mi = s.get_editor_property("material_interface")
                if not isinstance(mi, unreal.MaterialInstanceConstant):
                    continue
                p = mi.get_path_name().split(".")[0]
                if p in seen:
                    continue
                seen.add(p)
                apply_flags(mi, {"alpha": a, "alpha_test": at,
                                 "alpha_ref": ar, "two_sided": two})
                EAL.save_asset(p)
                n_set += 1
            continue
        for s, part in zip(mats, parts):
            mat = zsc.materials[part.mat_idx] if 0 <= part.mat_idx < len(zsc.materials) else None
            if mat is None:
                continue
            mi = s.get_editor_property("material_interface")
            if not isinstance(mi, unreal.MaterialInstanceConstant):
                continue
            p = mi.get_path_name().split(".")[0]
            if p in seen:
                continue
            seen.add(p)
            apply_flags(mi, {
                "alpha": bool(mat.is_alpha), "alpha_test": bool(mat.alpha_test),
                "alpha_ref": int(mat.alpha_ref), "two_sided": bool(mat.is_two_side)})
            EAL.save_asset(p)
            n_set += 1

log(f"DONE: {n_set} appearance MIs set, {n_skip} slot-mismatch skips")
