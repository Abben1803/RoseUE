"""
ue5_fix_item_materials_zsc.py — ZSC-faithful blend state for ITEM assets:

  1. Baked gear MIs (/Game/Characters/Gear/mesh_<id>/MI_gear_<id>, parented to
     M_RoseGear): per-MI BasePropertyOverrides from the gear manifest's part
     flags (first part that uses the mesh wins — same rule the MI's default
     atlas cell already uses).
  2. Held weapons (/Game/Characters/Modular/WeaponsStatic/weapon_<id>): slot i
     of the static mesh == part i of LIST_WEAPON.ZSC.objects[id]; apply flags
     to the Interchange MIs (skip on slot-count mismatch).

zz_material rule everywhere: masked only when alpha & alpha_test (@alpha_ref),
translucent when alpha only, else OPAQUE (DDS alpha = specular); two-sided per
ZSC flag; blend_type 3 = additive.

Env: ROSE_SKIP_GEAR=1 / ROSE_SKIP_WEAPONS=1 to limit.
Run headless, editor CLOSED, -nullrhi fine.
"""
import json
import os
import sys
import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
sys.path.insert(0, os.path.join(os.path.dirname(TOOLS), "mapforge"))
from rose_zsc import read_zsc                                   # noqa: E402

ARUA = r"C:\QQ-iROSE Online\extracted\3DDATA"
MANIFEST = os.path.join(TOOLS, "_tmp", "gear_manifest.json")
GEAR_ROOT = "/Game/Characters/Gear"
WPN_ROOT = "/Game/Characters/Modular/WeaponsStatic"
SWPN_ROOT = "/Game/Characters/Modular/SubWpnStatic"
BACK_ROOT = "/Game/Characters/Modular/BackStatic"
GEAR_ROOT = "/Game/Characters/Gear"

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary


def log(m):
    unreal.log(f"[itemzsc] {m}")



def apply_flags(mi, f):
    ov = mi.get_editor_property("base_property_overrides")
    ov.set_editor_property("override_two_sided", True)
    ov.set_editor_property("two_sided", f["two_sided"])
    if f["blend_type"] == 3:
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    elif f["alpha"] and f["alpha_test"]:
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        ov.set_editor_property("override_opacity_mask_clip_value", True)
        ov.set_editor_property("opacity_mask_clip_value", f["alpha_ref"] / 255.0)
    elif f["alpha"]:
        # Map-decal policy: Substrate TRANSLUCENT renders black/dithered —
        # masked at a low clip keeps the cutout shape and sorts correctly.
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        ov.set_editor_property("override_opacity_mask_clip_value", True)
        ov.set_editor_property("opacity_mask_clip_value", 0.33)
    else:
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        ov.set_editor_property("override_opacity_mask_clip_value", False)
    mi.set_editor_property("base_property_overrides", ov)
    ME.update_material_instance(mi)




if os.environ.get("ROSE_SKIP_BACK", "") != "1":
    manifest_path = os.path.join(TOOLS, "_tmp", "back_manifest.json")

    if not os.path.exists(manifest_path):
        raise FileNotFoundError(
            f"Missing BACK manifest: {manifest_path}"
        )

    with open(manifest_path) as f:
        back_manifest = json.load(f)

    back = back_manifest.get("back", {})

    n = skip = 0
    seen_mi = set()

    for d in EAL.list_assets(BACK_ROOT, recursive=False, include_folder=True):
        name = d.rstrip("/").rsplit("/", 1)[-1]

        if not name.startswith("back_"):
            continue

        try:
            wid = int(name.split("_", 1)[1])
        except ValueError:
            continue

        parts = back.get(str(wid))
        if not parts:
            continue

        smp = f"{BACK_ROOT}/{name}/{name}/StaticMeshes/{name}"

        sm = EAL.load_asset(smp) if EAL.does_asset_exist(smp) else None
        if not isinstance(sm, unreal.StaticMesh):
            continue

        mats = sm.get_editor_property("static_materials")

        if len(mats) != len(parts):
            skip += 1
            continue

        for slot, part in zip(mats, parts):
            flags = part.get("flags")
            if not flags:
                continue

            mi = slot.get_editor_property("material_interface")

            if not isinstance(mi, unreal.MaterialInstanceConstant):
                continue

            p = mi.get_path_name().split(".")[0]

            if p in seen_mi:
                continue

            seen_mi.add(p)

            apply_flags(mi, {
                "alpha": bool(flags.get("alpha")),
                "alpha_test": bool(flags.get("alpha_test")),
                "alpha_ref": int(flags.get("alpha_ref", 128)),
                "two_sided": bool(flags.get("two_sided")),
                "blend_type": int(flags.get("blend_type", 0)),
            })

            EAL.save_asset(p)
            n += 1

    log(f"weapons done: {n} MIs set, {skip} slot-mismatch skips")

# if os.environ.get("ROSE_SKIP_SUBWEAPONS", "") != "1":
#     manifest_path = os.path.join(TOOLS, "_tmp", "subwpn_manifest.json")

#     if not os.path.exists(manifest_path):
#         raise FileNotFoundError(
#             f"Missing subweapon manifest: {manifest_path}"
#         )

#     with open(manifest_path) as f:
#         subwpn_manifest = json.load(f)

#     subweapons = subwpn_manifest.get("subweapons", {})

#     n = skip = 0
#     seen_mi = set()

#     for d in EAL.list_assets(SWPN_ROOT, recursive=False, include_folder=True):
#         name = d.rstrip("/").rsplit("/", 1)[-1]

#         if not name.startswith("subwpn_"):
#             continue

#         try:
#             wid = int(name.split("_", 1)[1])
#         except ValueError:
#             continue

#         parts = subweapons.get(str(wid))
#         if not parts:
#             continue

#         smp = f"{SWPN_ROOT}/{name}/{name}/StaticMeshes/{name}"

#         sm = EAL.load_asset(smp) if EAL.does_asset_exist(smp) else None
#         if not isinstance(sm, unreal.StaticMesh):
#             continue

#         mats = sm.get_editor_property("static_materials")

#         if len(mats) != len(parts):
#             skip += 1
#             continue

#         for slot, part in zip(mats, parts):
#             flags = part.get("flags")
#             if not flags:
#                 continue

#             mi = slot.get_editor_property("material_interface")

#             if not isinstance(mi, unreal.MaterialInstanceConstant):
#                 continue

#             p = mi.get_path_name().split(".")[0]

#             if p in seen_mi:
#                 continue

#             seen_mi.add(p)

#             apply_flags(mi, {
#                 "alpha": bool(flags.get("alpha")),
#                 "alpha_test": bool(flags.get("alpha_test")),
#                 "alpha_ref": int(flags.get("alpha_ref", 128)),
#                 "two_sided": bool(flags.get("two_sided")),
#                 "blend_type": int(flags.get("blend_type", 0)),
#             })

#             EAL.save_asset(p)
#             n += 1

#     log(f"weapons done: {n} MIs set, {skip} slot-mismatch skips")

# # ---- 1. baked gear MIs ------------------------------------------------------
# if os.environ.get("ROSE_SKIP_GEAR", "") != "1":
#     with open(MANIFEST) as f:
#         man = json.load(f)
#     mesh_flags = {}          # mesh id -> flags (first item-part wins)
#     for key, parts in man["items"].items():
#         for p in parts:
#             mid = p.get("mesh")
#             if mid is None or mid in mesh_flags:
#                 continue
#             mesh_flags[mid] = {
#                 "alpha": bool(p.get("alpha")),
#                 "alpha_test": bool(p.get("alpha_test")),
#                 "alpha_ref": int(p.get("alpha_ref", 128) or 128),
#                 "two_sided": bool(p.get("two_side")),
#                 "blend_type": int(p.get("blend_type", 0) or 0),
#             }
#     n = 0
#     miss = 0
#     for mid, f in sorted(mesh_flags.items()):
#         p = f"{GEAR_ROOT}/mesh_{mid}/MI_gear_{mid}"
#         mi = EAL.load_asset(p) if EAL.does_asset_exist(p) else None
#         if not isinstance(mi, unreal.MaterialInstanceConstant):
#             miss += 1
#             continue
#         apply_flags(mi, f)
#         EAL.save_asset(p)
#         n += 1
#         if n % 500 == 0:
#             log(f"gear MIs: {n} ...")
#     log(f"gear MIs done: {n} set, {miss} missing")

# ---- 2. held weapons --------------------------------------------------------
# if os.environ.get("ROSE_SKIP_WEAPONS", "") != "1":
#     zsc = read_zsc(os.path.join(ARUA, "WEAPON", "LIST_WEAPON.ZSC"))
#     n = skip = 0
#     seen_mi = set()
#     for d in EAL.list_assets(WPN_ROOT, recursive=False, include_folder=True):
#         name = d.rstrip("/").rsplit("/", 1)[-1]
#         if not name.startswith("weapon_"):
#             continue
#         try:
#             wid = int(name.split("_", 1)[1])
#         except ValueError:
#             continue
#         if not (0 <= wid < len(zsc.models)):
#             continue
#         parts = zsc.models[wid].parts
#         smp = f"{WPN_ROOT}/{name}/{name}/StaticMeshes/{name}"
#         sm = EAL.load_asset(smp) if EAL.does_asset_exist(smp) else None
#         if not isinstance(sm, unreal.StaticMesh):
#             continue
#         mats = sm.get_editor_property("static_materials")
#         if len(mats) != len(parts):
#             skip += 1
#             continue
#         for slot, part in zip(mats, parts):
#             mat = zsc.materials[part.mat_idx] if 0 <= part.mat_idx < len(zsc.materials) else None
#             if mat is None:
#                 continue
#             mi = slot.get_editor_property("material_interface")
#             if not isinstance(mi, unreal.MaterialInstanceConstant):
#                 continue
#             p = mi.get_path_name().split(".")[0]
#             if p in seen_mi:
#                 continue
#             seen_mi.add(p)
#             apply_flags(mi, {
#                 "alpha": bool(mat.is_alpha), "alpha_test": bool(mat.alpha_test),
#                 "alpha_ref": int(mat.alpha_ref), "two_sided": bool(mat.is_two_side),
#                 "blend_type": int(mat.blend_type)})
#             EAL.save_asset(p)
#             n += 1
#     log(f"weapons done: {n} MIs set, {skip} slot-mismatch skips")

# log("DONE")


# ── AVATAR body parts (native ZSC import) ────────────────────────────────────
#
# The sections above only cover GEAR and held items.  The avatar itself —
# body/arms/foot/cap/face/hair/faceitem — is imported natively by RoseEditor to
# /Game/Rose/Characters/<G>/<SLOT>/SK_<G>_<SLOT>_<id>, and never went through
# any of this.
#
# Same rule as the weapons block: slot i of the mesh == part i of
# ZSC.models[id], flags applied with apply_flags (so masked only when
# alpha & alpha_test, alpha-only -> masked 0.33, else OPAQUE).
#
# No manifest needed — the ZSCs are read directly.
#
# Env:
#   ROSE_SKIP_AVATAR=1     skip this block
#   ROSE_AVATAR_ITEM=<id>  only that object id (fast iteration)
#   ROSE_AVATAR_DRYRUN=1   report differences, write nothing
if os.environ.get("ROSE_SKIP_AVATAR", "") != "1":
    AVATAR_ROOT = "/Game/Rose/Characters"

    # (UE gender key, ZSC filename prefix) — the ZSC files use M and W (woman).
    AV_GENDERS = [("F", "W"), ("M", "M")]

    # (UE slot folder, ZSC basename after the gender prefix)
    AV_SLOTS = [
        ("BODY", "BODY"), ("ARMS", "ARMS"), ("FOOT", "FOOT"),
        ("CAP", "CAP"), ("FACE", "FACE"), ("HAIR", "HAIR"),
    ]

    only_item = os.environ.get("ROSE_AVATAR_ITEM", "")
    only_item = int(only_item) if only_item.strip() else None
    dry = os.environ.get("ROSE_AVATAR_DRYRUN", "") == "1"

    n = skip = missing = 0
    seen_mi = set()

    for gkey, zprefix in AV_GENDERS:
        for slot, zname in AV_SLOTS:
            zpath = os.path.join(ARUA, "AVATAR", f"LIST_{zprefix}{zname}.ZSC")
            if not os.path.exists(zpath):
                log(f"avatar: no ZSC {zpath}")
                continue

            zsc = read_zsc(zpath)

            for oid, model in enumerate(zsc.models):
                if only_item is not None and oid != only_item:
                    continue
                if not model.parts:
                    continue

                asset = f"SK_{gkey}_{slot}_{oid}"
                path = f"{AVATAR_ROOT}/{gkey}/{slot}/{asset}"
                if not EAL.does_asset_exist(path):
                    missing += 1
                    continue

                sm = EAL.load_asset(path)
                if not isinstance(sm, unreal.SkeletalMesh):
                    continue

                mats = sm.get_editor_property("materials")

                # Slot count must match the ZSC part count, or the i-th slot is
                # not the i-th part and every flag would land on the wrong
                # material.  Skipping loudly beats writing nonsense.
                if len(mats) != len(model.parts):
                    skip += 1
                    log(f"avatar: {asset} has {len(mats)} slots vs "
                        f"{len(model.parts)} ZSC parts — skipped")
                    continue

                for slot_entry, part in zip(mats, model.parts):
                    if not (0 <= part.mat_idx < len(zsc.materials)):
                        continue
                    mat = zsc.materials[part.mat_idx]

                    mi = slot_entry.get_editor_property("material_interface")
                    if not isinstance(mi, unreal.MaterialInstanceConstant):
                        continue

                    p = mi.get_path_name().split(".")[0]
                    if p in seen_mi:
                        continue
                    seen_mi.add(p)

                    f = {
                        "alpha": bool(mat.is_alpha),
                        "alpha_test": bool(mat.alpha_test),
                        "alpha_ref": int(mat.alpha_ref),
                        "two_sided": bool(mat.is_two_side),
                        "blend_type": int(mat.blend_type),
                    }

                    if dry:
                        ov = mi.get_editor_property("base_property_overrides")
                        log(f"avatar DRY {asset} {mi.get_name()} "
                            f"alpha={f['alpha']:d} atest={f['alpha_test']:d} "
                            f"aref={f['alpha_ref']} 2s={f['two_sided']:d} "
                            f"blend={f['blend_type']} "
                            f"| current={ov.get_editor_property('blend_mode')}")
                        n += 1
                        continue

                    apply_flags(mi, f)
                    EAL.save_asset(p)
                    n += 1

    log(f"avatar done: {n} MIs {'inspected' if dry else 'set'}, "
        f"{skip} slot-mismatch skips, {missing} assets absent")

log("DONE")
