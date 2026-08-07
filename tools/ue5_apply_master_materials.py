"""
ue5_apply_master_materials.py — Re-point every catalog mesh (wardrobe, static
weapons, monsters) at MaterialInstanceConstants of M_RoseMaster + the atlas
pages, per the manifests:

  tools/_tmp/texture_manifest.json  (mesh asset -> slot -> source texture)
  tools/_tmp/atlas_manifest.json    (source texture -> page + UV rect)

MIs are DEDUPED by (page, rect, mode): items sharing a texture share one MI —
that's what lets merged character sections batch.  Exception: FACE slots get
per-slot MIs so the eye-blink sections can't be collapsed by the skeletal
merge.  MIs live at /Game/Atlas/MI/.  Existing MIs are updated in place (never
delete+create — see the materials saga in CHARACTER_SYSTEM.md).

Resumable: a mesh whose every textured slot already points into /Game/Atlas/MI
is skipped.  Env:
  ROSE_CATS="female,monsters"   category filter (default: all)
  ROSE_LIMIT=N                  stop after N changed meshes
Run headless (RHI not required — no texture builds), editor CLOSED.
"""
import json
import os
import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
GAME_ROOT = "/Game/Atlas"
MI_ROOT = f"{GAME_ROOT}/MI"
MASTER_PATH = f"{GAME_ROOT}/M_RoseMaster"

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary

with open(os.path.join(TOOLS, "_tmp", "texture_manifest.json"), encoding="utf-8") as f:
    tex_man = json.load(f)
with open(os.path.join(TOOLS, "_tmp", "atlas_manifest.json"), encoding="utf-8") as f:
    atlas_man = json.load(f)
ATLAS = atlas_man["map"]

CATS = {c.strip() for c in os.environ.get("ROSE_CATS", "").split(",") if c.strip()}
LIMIT = int(os.environ.get("ROSE_LIMIT", "0") or 0)

master = EAL.load_asset(MASTER_PATH)
if not master:
    raise RuntimeError("M_RoseMaster missing — run ue5_import_atlases.py first")
if not EAL.does_directory_exist(MI_ROOT):
    EAL.make_directory(MI_ROOT)

_page_cache = {}
def page_texture(page):
    if page not in _page_cache:
        _page_cache[page] = EAL.load_asset(f"{GAME_ROOT}/{page}")
    return _page_cache[page]


_mi_cache = {}
def get_mi(page, scale, offset, mode, uniq=""):
    """Get-or-create the deduped MI for an atlas rect + render mode."""
    px, py = int(round(offset[0] * 2048)), int(round(offset[1] * 2048))
    base = page.replace("/", "_")
    name = f"MI_{base}_{px}_{py}"
    if mode == "MASK":
        name += "_M"
    if uniq:
        name += f"_{uniq}"
    key = name
    if key in _mi_cache:
        return _mi_cache[key]
    path = f"{MI_ROOT}/{name}"
    mi = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
    if not mi:
        mi = AT.create_asset(name, MI_ROOT, unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
        if not mi:
            return None
    ME.set_material_instance_parent(mi, master)
    tex = page_texture(page)
    if tex:
        ME.set_material_instance_texture_parameter_value(mi, "BaseColor", tex)
    ME.set_material_instance_vector_parameter_value(
        mi, "UVTransform",
        unreal.LinearColor(scale[0], scale[1], offset[0], offset[1]))
    if mode == "MASK":
        ov = mi.get_editor_property("base_property_overrides")
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mi.set_editor_property("base_property_overrides", ov)
    ME.update_material_instance(mi)
    EAL.save_asset(path)
    _mi_cache[key] = mi
    return mi


def apply_mesh(entry):
    """Returns 'changed' | 'ok' | 'missing' | 'notex'."""
    asset = entry["asset"]
    name = asset.rsplit("/", 1)[-1]
    full = f"{asset}.{name}"
    if not EAL.does_asset_exist(asset):
        return "missing"
    obj = unreal.load_object(None, full)
    if not obj:
        return "missing"

    is_skel = entry["class"] == "SkeletalMesh"
    prop = "materials" if is_skel else "static_materials"
    mats = list(obj.get_editor_property(prop))
    slots = entry["slots"]
    changed = False
    any_tex = False
    for i, s in enumerate(slots):
        if i >= len(mats):
            break
        tex = s["tex"]
        if not tex or tex not in ATLAS:
            continue
        any_tex = True
        rec = ATLAS[tex]
        # face slots: unique MI per slot so the blink sections stay distinct
        uniq = f"f{i}_{name}" if "/face_" in asset or "face_" == name[:5] else ""
        mi = get_mi(rec["page"], rec["scale"], rec["offset"], s["mode"], uniq)
        if not mi:
            continue
        cur = mats[i].get_editor_property("material_interface")
        if cur and cur.get_path_name().startswith(MI_ROOT) and \
                cur.get_name() == mi.get_name():
            continue
        mats[i].set_editor_property("material_interface", mi)
        changed = True
    if not any_tex:
        return "notex"
    if changed:
        obj.set_editor_property(prop, mats)
        EAL.save_asset(asset)
        return "changed"
    return "ok"


counts = {"changed": 0, "ok": 0, "missing": 0, "notex": 0}
n = 0
for entry in tex_man["meshes"]:
    if CATS and entry["category"] not in CATS:
        continue
    r = apply_mesh(entry)
    counts[r] += 1
    if r == "changed":
        n += 1
        if n % 100 == 0:
            print(f"[apply] {n} changed ... last={entry['asset'].rsplit('/',1)[-1]} ({counts})")
            unreal.SystemLibrary.collect_garbage()
        if LIMIT and n >= LIMIT:
            print("[apply] LIMIT reached")
            break

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
print(f"[apply] done: {counts}  (unique MIs this run: {len(_mi_cache)})")
