"""
ue5_fix_monster_materials_zsc.py — ZSC-FAITHFUL material state for imported
monsters/NPCs (replaces the blanket "everything opaque + two-sided" pass of
ue5_fix_monster_materials.py).

Per zz_material.cpp::apply (the engine authority):
  alpha=0                  -> OPAQUE   (alpha_test ignored — this is what the
                                        blanket fix approximated: NPC DDS alpha
                                        holds specular, and ROSE never reads it
                                        unless alpha blending is on)
  alpha=1 & alpha_test=1   -> MASKED @ alpha_ref/255
  alpha=1                  -> TRANSLUCENT
  blend_type=3 (Lighten)   -> ADDITIVE
  two-sided                -> ONLY per the ZSC flag

Slot mapping: build_monsters.py emits one GLB primitive per ZSC part in
LIST_NPC.CHR/PART_NPC.ZSC order, so skeletal-mesh material slot i == part i.
NPCs whose slot count no longer matches the ZSC part list are skipped loudly.

Env:  ROSE_ONLY="npc_1,npc_2" to limit.
      ROSE_NPC_ROOT — 3DDATA root for CHR/ZSC (default: the SourceAssets root
      build_monsters.py used, so flags match the meshes actually imported).
Run headless, editor CLOSED, -nullrhi fine.
"""
import os
import sys
import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
from rose_parser.formats.chr_ import parse as parse_chr   # noqa: E402
from rose_parser.formats.zsc import parse as parse_zsc    # noqa: E402

# Asset source: Arua (CLAUDE.md).  Must match whatever build_monsters.py used
# to build the GLBs, or materials get applied from a different monster's ZSC:
# LIST_NPC is 4206 rows in Arua vs 3033 in classic and the ids do not line up.
NPC_DIR = os.path.join(
    os.environ.get("ROSE_NPC_ROOT",
                   os.environ.get("ROSE_ASSET_ROOT",
                                  r"C:\QQ-iROSE Online\extracted\3DDATA")),
    "NPC")
ROOT = "/Game/Monsters"

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary

ONLY = {x.strip() for x in os.environ.get("ROSE_ONLY", "").split(",") if x.strip()}


def log(m):
    unreal.log(f"[mobzsc] {m}")


chr_ = parse_chr(os.path.join(NPC_DIR, "LIST_NPC.CHR"))
zsc = parse_zsc(os.path.join(NPC_DIR, "PART_NPC.ZSC"))


def npc_part_flags(idx):
    """Ordered per-part ZSC material flags for CHR model idx (mirrors
    build_monsters.resolve_model: parts whose mesh is missing were skipped at
    build time — missing files can't be checked here, so we keep all; slot
    count mismatch is detected by the caller)."""
    m = chr_.models[idx]
    if not m.is_valid:
        return None
    out = []
    for bp in m.body_part_indices:
        if bp >= len(zsc.models):
            continue
        for zp in zsc.models[bp].parts:
            if zp.mesh_id >= len(zsc.mesh_files):
                continue
            mat = zsc.materials[zp.material_id] if zp.material_id < len(zsc.materials) else None
            if mat is None:
                out.append(None)
                continue
            out.append({
                "alpha": bool(mat.is_alpha),
                "alpha_test": bool(mat.alpha_test),
                "alpha_ref": int(mat.alpha_ref),
                "two_sided": bool(mat.is_2side),
                "blend_type": int(mat.blend_type),
            })
    return out


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
        ME.set_material_instance_scalar_parameter_value(mi, "AlphaMode", 1.0)
    elif f["alpha"]:
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        ME.set_material_instance_scalar_parameter_value(mi, "AlphaMode", 2.0)
    else:
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        ov.set_editor_property("override_opacity_mask_clip_value", False)
        ME.set_material_instance_scalar_parameter_value(mi, "AlphaMode", 0.0)
    mi.set_editor_property("base_property_overrides", ov)
    ME.update_material_instance(mi)


n_npc = n_set = n_skip = 0
conflicts = 0
mi_flags = {}   # MI path -> first flags applied (conflict detection)
for d in EAL.list_assets(ROOT, recursive=False, include_folder=True):
    name = d.rstrip("/").rsplit("/", 1)[-1]
    if not name.startswith("npc_") or (ONLY and name not in ONLY):
        continue
    try:
        idx = int(name.split("_", 1)[1])
    except ValueError:
        continue
    if not (0 <= idx < len(chr_.models)):
        continue
    flags = npc_part_flags(idx)
    if not flags:
        continue
    sm = EAL.load_asset(f"{ROOT}/{name}/{name}/SkeletalMeshes/{name}")
    if not sm:
        continue
    mats = sm.get_editor_property("materials")
    if len(mats) != len(flags):
        log(f"{name}: slot count {len(mats)} != zsc parts {len(flags)} — skipped")
        n_skip += 1
        continue
    for slot, f in zip(mats, flags):
        if f is None:
            continue
        mi = slot.get_editor_property("material_interface")
        if not isinstance(mi, unreal.MaterialInstanceConstant):
            continue
        path = mi.get_path_name().split(".")[0]
        prev = mi_flags.get(path)
        if prev is not None:
            if prev != f:
                conflicts += 1
            continue
        mi_flags[path] = f
        apply_flags(mi, f)
        EAL.save_asset(path)
        n_set += 1
    n_npc += 1

log(f"DONE: {n_npc} monsters, {n_set} MIs set 1:1 with ZSC, "
    f"{n_skip} slot-mismatch skips, {conflicts} shared-MI flag conflicts (first won)")
