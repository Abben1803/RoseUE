"""
ue5_fix_weapon_materials.py — Re-link weapon materials/textures after the weapons
were moved out of the gendered Female folder into the shared
/Game/Characters/Modular/Weapons folder by a *filesystem* move (which left every
internal cross-reference dangling at the old /Female/weapon_* paths).

For each weapon_<id> it rebuilds the links from the CO-LOCATED assets, ignoring the
broken old references:
  - mesh material slot  -> the material asset in this weapon's Materials/ folder
                           (matched by the slot's imported name; index fallback)
  - that material's M_GLTF `BaseColorTexture` -> this weapon's Textures/
                           weapon_<id>_texture_<slotIndex>

Weapon materials are Interchange M_GLTF instances; the base-color parameter is
`BaseColorTexture` (verified by probe).  No shader work → safe under -nullrhi, no
TDR.  Set ROSE_WPN="1,400" to limit to specific ids (default: all).

Editor must be CLOSED.
"""
import os
import unreal

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
ROOT = "/Game/Characters/Modular/Weapons"
PARAM = "BaseColorTexture"   # M_GLTF base-color texture parameter


def _idx(path):
    """Trailing integer of an asset path, ignoring any .objectname suffix."""
    stem = path.split(".")[0]
    n = stem.rsplit("_", 1)[-1]
    return int(n) if n.isdigit() else 0


def _stem(path):
    return path.rsplit("/", 1)[-1].split(".")[0]


def textures_for(it):
    d = f"{ROOT}/{it}/{it}/Textures"
    paths = EAL.list_assets(d, recursive=False) if EAL.does_directory_exist(d) else []
    return sorted(paths, key=_idx)


def materials_for(it):
    d = f"{ROOT}/{it}/{it}/Materials"
    paths = EAL.list_assets(d, recursive=False) if EAL.does_directory_exist(d) else []
    return {_stem(p): p for p in paths}, sorted(paths)


def weapon_ids():
    env = os.environ.get("ROSE_WPN", "").strip()
    if env:
        return [f"weapon_{x.strip()}" for x in env.split(",") if x.strip()]
    out = []
    for d in EAL.list_assets(ROOT, recursive=False, include_folder=True):
        nm = d.rstrip("/").rsplit("/", 1)[-1]
        if nm.startswith("weapon_"):
            out.append(nm)
    out.sort(key=lambda s: int(s.split("_")[1]) if s.split("_")[1].isdigit() else 0)
    return out


def run():
    items = weapon_ids()
    print(f"[wfix] {len(items)} weapons; param={PARAM}")
    fixed = tex_set = slot_set = noskel = 0
    problems = []
    for k, it in enumerate(items):
        sm_path = f"{ROOT}/{it}/{it}/SkeletalMeshes/{it}"
        sm = EAL.load_asset(sm_path)
        if not sm:
            problems.append(f"{it}: mesh missing"); continue
        # NOTE: the filesystem move also nulled each mesh's skeleton ref, but the
        # USkeletalMesh `skeleton` property is READ-ONLY from Python (cannot be
        # re-linked here).  The runtime merge targets the shared base skeleton and
        # reads bone data from the mesh itself, so a null part-skeleton does not stop
        # the weapon merging.  Just count them so we can report it.
        if sm.get_editor_property("skeleton") is None:
            noskel += 1

        texs = textures_for(it)
        by_name, by_sort = materials_for(it)

        slots = sm.get_editor_property("materials")
        out = []
        for i, slot in enumerate(slots):
            sname = str(slot.get_editor_property("imported_material_slot_name"))
            mpath = by_name.get(sname) or (by_sort[i] if i < len(by_sort) else None)
            mi = EAL.load_asset(mpath) if mpath else None
            if not mi:
                problems.append(f"{it}[{i}]: material '{sname}' not found")
                out.append(slot); continue
            # base-color texture := co-located texture for this slot index
            tpath = texs[i] if i < len(texs) else (texs[0] if texs else None)
            tex = EAL.load_asset(tpath) if tpath else None
            if tex:
                ME.set_material_instance_texture_parameter_value(mi, PARAM, tex)
                EAL.save_asset(mpath)
                tex_set += 1
            else:
                problems.append(f"{it}[{i}]: no texture for slot")
            slot.set_editor_property("material_interface", mi)
            slot_set += 1
            out.append(slot)
        sm.set_editor_property("materials", out)
        EAL.save_asset(sm_path)
        fixed += 1
        if (k + 1) % 50 == 0:
            print(f"[wfix] ...{k + 1}/{len(items)} (tex_set={tex_set})")

    print(f"[wfix] DONE meshes={fixed} slots_linked={slot_set} textures_set={tex_set} "
          f"null_part_skeletons={noskel} problems={len(problems)}")
    for p in problems[:40]:
        print("   ", p)


run()
