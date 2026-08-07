"""
ue5_apply_map_materials.py — Re-point the map scene's static meshes at
M_RoseMaster MIs + the map atlas pages.

Mapping: each Scene static mesh slot currently holds an Interchange material
whose NAME is the mapforge manifest name (longest-prefix match handles the
"_1" duplicate suffixes) → manifest gives texture_src + mode → atlas manifest
gives page + UV rect → deduped MI under /Game/Atlas/MI.

Skipped: water (kind='water', special master by policy) and untextured color
materials — those slots keep their current materials.

Env: ROSE_ZONE=JPT01.  Run headless, editor CLOSED (nullrhi fine).
"""
import json
import os
import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
ZONE = os.environ.get("ROSE_ZONE", "JPT01").upper()
SCENE = f"/Game/Maps/{ZONE}/Scene"
GAME_ROOT = "/Game/Atlas"
MI_ROOT = f"{GAME_ROOT}/MI"
MASTER_PATH = f"{GAME_ROOT}/M_RoseMaster"
TERRAIN_MASTER_PATH = f"{GAME_ROOT}/M_RoseTerrain"
TERRAIN_PREFIX = "MI_TERRAIN_"

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary

with open(os.path.join(TOOLS, "_tmp", f"map_texture_manifest_{ZONE}.json"), encoding="utf-8") as f:
    map_man = json.load(f)
with open(os.path.join(TOOLS, "_tmp", f"map_atlas_manifest_{ZONE}.json"), encoding="utf-8") as f:
    ATLAS = json.load(f)["map"]
BY_NAME = {m["name"]: m for m in map_man["materials"]}

master = EAL.load_asset(MASTER_PATH)
if not master:
    raise RuntimeError("M_RoseMaster missing")
terrain_master = EAL.load_asset(TERRAIN_MASTER_PATH)
if not terrain_master:
    raise RuntimeError("M_RoseTerrain missing — run tools/ue5_terrain_master.py first")


def entry_for(asset_name):
    if asset_name in BY_NAME:
        return BY_NAME[asset_name]
    best = None
    for name in BY_NAME:
        if asset_name.startswith(name) and (best is None or len(name) > len(best)):
            best = name
    return BY_NAME.get(best)


_page_cache = {}
_mi_cache = {}


def page_asset(page):
    if page not in _page_cache:
        _page_cache[page] = EAL.load_asset(f"{GAME_ROOT}/{page}")
    return _page_cache[page]


def rect_tag(rec):
    return (f"{rec['page'].replace('/', '_')}_"
            f"{int(round(rec['offset'][0] * 2048))}_"
            f"{int(round(rec['offset'][1] * 2048))}")


def uv_transform(rec):
    return unreal.LinearColor(rec["scale"][0], rec["scale"][1],
                              rec["offset"][0], rec["offset"][1])


def get_terrain_mi(bot_rec, top_rec):
    """One OPAQUE MI per (bottom rect, top rect) pair, on M_RoseTerrain.

    The material lerps bottom->top by the top map's alpha, exactly as
    terrain.psh does. Deliberately NOT masked/translucent: the blend is
    continuous and there is only one mesh, so nothing needs sorting.
    """
    name = f"{TERRAIN_PREFIX}{rect_tag(bot_rec)}__{rect_tag(top_rec)}"
    if name in _mi_cache:
        return _mi_cache[name]
    path = f"{MI_ROOT}/{name}"
    mi = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
    if not mi:
        mi = AT.create_asset(name, MI_ROOT, unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
        if not mi:
            return None
    ME.set_material_instance_parent(mi, terrain_master)
    bp, tp = page_asset(bot_rec["page"]), page_asset(top_rec["page"])
    if bp:
        ME.set_material_instance_texture_parameter_value(mi, "BaseColor", bp)
    if tp:
        ME.set_material_instance_texture_parameter_value(mi, "TopColor", tp)
    ME.set_material_instance_vector_parameter_value(mi, "UVTransform", uv_transform(bot_rec))
    ME.set_material_instance_vector_parameter_value(mi, "TopUVTransform", uv_transform(top_rec))
    ov = mi.get_editor_property("base_property_overrides")
    ov.set_editor_property("override_two_sided", True)
    ov.set_editor_property("two_sided", True)
    ov.set_editor_property("override_blend_mode", True)
    ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mi.set_editor_property("base_property_overrides", ov)
    ME.update_material_instance(mi)
    EAL.save_asset(path)
    _mi_cache[name] = mi
    return mi


def get_mi(rec, entry):
    """One MI per (atlas rect, ROSE render state) — settings mirror the ZSC
    material 1:1 (zz_material.cpp::apply): OPAQUE<->opaque, MASK<->masked at
    alpha_ref, BLEND<->translucent, blend_type 3 (Lighten)<->additive, and
    two-sided ONLY where the ZSC says so."""
    mode = entry.get("mode", "OPAQUE")
    two = bool(entry.get("twosided"))
    additive = entry.get("blend_type", 0) == 3
    page = rec["page"]
    px = int(round(rec["offset"][0] * 2048))
    py = int(round(rec["offset"][1] * 2048))
    name = f"MI_{page.replace('/', '_')}_{px}_{py}"
    if additive:
        name += "_A"
    elif mode == "MASK":
        name += "_M"
    elif mode == "BLEND":
        name += "_T"
    if not two:
        name += "_1S"
    if name in _mi_cache:
        return _mi_cache[name]
    path = f"{MI_ROOT}/{name}"
    mi = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
    if not mi:
        mi = AT.create_asset(name, MI_ROOT, unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
        if not mi:
            return None
    ME.set_material_instance_parent(mi, master)
    if page_asset(page):
        ME.set_material_instance_texture_parameter_value(mi, "BaseColor", page_asset(page))
    ME.set_material_instance_vector_parameter_value(
        mi, "UVTransform",
        unreal.LinearColor(rec["scale"][0], rec["scale"][1],
                           rec["offset"][0], rec["offset"][1]))
    ov = mi.get_editor_property("base_property_overrides")
    ov.set_editor_property("override_two_sided", True)
    ov.set_editor_property("two_sided", two)
    if additive:
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    elif mode == "MASK":
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        ov.set_editor_property("override_opacity_mask_clip_value", True)
        ov.set_editor_property("opacity_mask_clip_value",
                               (entry.get("alpha_ref", 128) or 128) / 255.0)
    elif mode == "BLEND":
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mi.set_editor_property("base_property_overrides", ov)
    ME.update_material_instance(mi)
    EAL.save_asset(path)
    _mi_cache[name] = mi
    return mi


changed = kept = terrain_slots = 0
n = 0
for path in EAL.list_assets(SCENE, recursive=True):
    obj = EAL.load_asset(path)
    if not isinstance(obj, unreal.StaticMesh):
        continue
    mats = list(obj.get_editor_property("static_materials"))
    dirty = False
    for slot in mats:
        cur = slot.get_editor_property("material_interface")
        if not cur:
            continue
        if cur.get_path_name().startswith(MI_ROOT):
            continue                       # already converted (resumable)
        e = entry_for(cur.get_name())
        if not e or e["texture_src"] not in ATLAS:
            kept += 1                      # water / color — leave as-is
            continue
        if e.get("kind") == "terrain":
            # ONE opaque mesh carrying both tile layers (UV0 bottom / UV1 top)
            src2 = e.get("texture_src2") or e["texture_src"]
            if src2 not in ATLAS:
                src2 = e["texture_src"]    # 2nd map unresolved -> lerp is identity
            mi = get_terrain_mi(ATLAS[e["texture_src"]], ATLAS[src2])
            if mi:
                slot.set_editor_property("material_interface", mi)
                dirty = True
                terrain_slots += 1
            continue
        mi = get_mi(ATLAS[e["texture_src"]], e)
        if mi:
            slot.set_editor_property("material_interface", mi)
            dirty = True
    if dirty:
        obj.set_editor_property("static_materials", mats)
        EAL.save_asset(path.split(".")[0])
        changed += 1
    n += 1
    if n % 200 == 0:
        print(f"[mapapply] {n} meshes scanned, {changed} changed ...")
        unreal.SystemLibrary.collect_garbage()

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
print(f"[mapapply] done: {changed} meshes converted, {terrain_slots} terrain slots "
      f"(M_RoseTerrain), {kept} slots kept (water/color), {len(_mi_cache)} map MIs")
