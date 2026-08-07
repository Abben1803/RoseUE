"""
ue5_refit_map_mats.py — Refit every imported zone's atlas MIs to the ROSE
material state 1:1, IN PLACE (no GLB re-export / scene re-import).

The original export collapsed the ZSC render state ("everything two-sided
masked"): backfaces that ROSE culls were being drawn, and with a shading
normal facing away from the light they render pitch black — the "one side of
every structure is black" bug.  This script re-reads the zone's deco/cnst ZSC
packs (the source of truth) and sets each existing MI to exactly what
zz_material.cpp::apply does in the classic client:

  alpha=0                      -> OPAQUE (alpha_test is inert without blending)
  alpha=1, alpha_test=1        -> MASKED, clip = alpha_ref/255
  alpha=1, alpha_test=0        -> TRANSLUCENT
  blend_type=3 (Lighten)       -> ADDITIVE
  two_sided                    -> ONLY where the ZSC flag is set

MI identity is preserved (names/slots untouched) — only BasePropertyOverrides
change.  Materials with no ZSC user (terrain layers, water, morph objects)
are left untouched.

Env: ROSE_ZONE=JPT01 for one zone, else every zone with a manifest in _tmp.
Run headless, editor CLOSED (-nullrhi fine; shaders compile on next load).
Re-runnable.
"""
import json
import os
import sys

import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
from rose_parser.formats.stb import parse as parse_stb   # noqa: E402
from rose_parser.formats.zsc import parse as parse_zsc   # noqa: E402

# Fallback 3DDATA root used when ASSET_ROOT does not resolve a zone row.  Was
# the deprecated SourceAssets tree (66 zone rows vs Arua's 235), so anything
# Arua-era silently fell back onto a wrong zone row.
SRC = r"C:\QQ-iROSE Online\extracted\3DDATA"
# Mesh/texture resolution root.  MUST match mapforge's ASSET_ROOT, which is now
# Arua (mapforge/config.py).  This was hardcoded to the classic client and is
# how JPT01 got imported from the classic tree.  No model asset resolves from
# classic any more -- see CLAUDE.md "Asset source: Arua, always".
CLIENT_3D = os.environ.get("MAPFORGE_ASSET_ROOT", SRC)   # mapforge ASSET_ROOT
GAME_ROOT = "/Game/Atlas"
MI_ROOT = f"{GAME_ROOT}/MI"
ONLY = os.environ.get("ROSE_ZONE", "").upper()
# Overrides for zones whose source is a DIFFERENT client (e.g. the promoted
# JPT01 = modern-client Zant, extracted under RoseUE/extracted/3DDATA):
#   ROSE_ASSET_ROOT  3DDATA root for LIST_ZONE.STB + pack/texture resolution
#   ROSE_ZSC_KEY     zone key in that LIST_ZONE.STB (default = zone)
#   ROSE_SCENE_MATS  mapforge materials.json stem (default = zone)
#   ROSE_FORCE_SCENE=1  skip the atlas path even if manifests exist
ASSET_ROOT = os.environ.get("ROSE_ASSET_ROOT", CLIENT_3D)
ZSC_KEY = os.environ.get("ROSE_ZSC_KEY", "").upper()
SCENE_MATS = os.environ.get("ROSE_SCENE_MATS", "")
FORCE_SCENE = os.environ.get("ROSE_FORCE_SCENE", "") == "1"

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary


def norm(p):
    return os.path.normcase(os.path.normpath(p))


def resolve(rel):
    parts = [q for q in rel.replace("\\", "/").split("/") if q]
    if parts and parts[0].upper() == "3DDATA":
        parts = parts[1:]
    return os.path.join(ASSET_ROOT, *parts)


def zsc_flag_map(zone):
    """texture abs path (normcased) -> merged ROSE flags from the zone's
    deco + cnst ZSC packs.  Zone rows are keyed the way mapforge keys them:
    the map-tile FOLDER name from the .ZON path (col 1) — that's the only key
    that knows modern-client zones like JPT01V2.  The client STB is the
    authority; SourceAssets is the fallback."""
    zone = ZSC_KEY or zone
    row = None
    zones = None
    for stb_path in (os.path.join(ASSET_ROOT, "STB", "LIST_ZONE.STB"),
                     os.path.join(SRC, "STB", "LIST_ZONE.STB")):
        if not os.path.exists(stb_path):
            continue
        zones = parse_stb(stb_path)
        for i in range(zones.num_rows()):
            zon_rel = zones.get(i, 1)
            key = os.path.basename(os.path.dirname(zon_rel.replace("\\", "/"))).upper()
            if key == zone:
                row = i
                break
        if row is not None:
            break
    if row is None:
        print(f"[refit] {zone}: not found in any LIST_ZONE.STB")
        return {}
    flags = {}
    conflicts = 0
    for col in (11, 12):
        rel = zones.get(row, col)
        if not rel.strip():
            continue
        try:
            zsc = parse_zsc(resolve(rel))
        except Exception as e:
            print(f"[refit] {zone}: pack {rel}: {e}")
            continue
        for m in zsc.materials:
            if not m.texture_path:
                continue
            f = {"two_sided": bool(m.is_2side), "alpha": bool(m.is_alpha),
                 "alpha_test": bool(m.alpha_test),
                 "alpha_ref": m.alpha_ref or 128,
                 "blend_type": m.blend_type}
            k = norm(resolve(m.texture_path))
            if k in flags and flags[k] != f:
                conflicts += 1
                # merge permissively: two-sided/alpha win (visually safe)
                flags[k]["two_sided"] |= f["two_sided"]
                flags[k]["alpha"] |= f["alpha"]
                flags[k]["alpha_test"] |= f["alpha_test"]
                flags[k]["blend_type"] = max(flags[k]["blend_type"], f["blend_type"])
            else:
                flags[k] = f
    if conflicts:
        print(f"[refit] {zone}: {conflicts} same-texture flag conflicts (OR-merged)")
    return flags


def apply_flags(mi, f):
    """Set the MI's BasePropertyOverrides to the faithful ROSE state."""
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
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    else:
        # ROSE renders this opaque — clear any masked/translucent override
        ov.set_editor_property("override_blend_mode", False)
        ov.set_editor_property("override_opacity_mask_clip_value", False)
    mi.set_editor_property("base_property_overrides", ov)
    unreal.MaterialEditingLibrary.update_material_instance(mi)


def refit_scene_zone(zone):
    """Zones without the atlas pass (e.g. JPT01V2): the live materials are the
    Interchange MIs under Maps/<Z>/Scene — apply the ZSC state to those,
    matching by asset name via the mapforge materials manifest (same
    longest-prefix rule as ue5_fix_map_materials).  Texture/scalar params
    (e.g. a manually cleared MetallicFactor) are not touched."""
    mats_json = os.path.join(os.path.dirname(TOOLS), "mapforge", "exports",
                             f"{SCENE_MATS or zone}.glb.materials.json")
    if not os.path.exists(mats_json):
        print(f"[refit] {zone}: SKIP (no manifests, no mapforge materials.json)")
        return
    with open(mats_json, encoding="utf-8") as fh:
        by_name = {m["name"]: m for m in json.load(fh)["materials"]}
    zflags = zsc_flag_map(zone)

    def entry_for(asset_name):
        if asset_name in by_name:
            return by_name[asset_name]
        best = None
        for name in by_name:
            if asset_name.startswith(name) and (best is None or len(name) > len(best)):
                best = name
        return by_name.get(best)

    fixed = untouched = 0
    for path in EAL.list_assets(f"/Game/Maps/{zone}/Scene", recursive=True):
        clean = str(path).split(".")[0]
        name = clean.rsplit("/", 1)[-1]
        if name.endswith("_color"):
            continue
        mi = EAL.load_asset(clean)
        if not isinstance(mi, unreal.MaterialInstanceConstant):
            continue
        e = entry_for(name)
        src = e.get("texture_src") if e else None
        f = zflags.get(norm(src)) if src else None
        if not f or (e and e.get("kind") == "water"):
            untouched += 1
            continue
        apply_flags(mi, f)
        EAL.save_asset(clean)
        fixed += 1
    print(f"[refit] {zone}: SCENE mode — {fixed} MIs refit, {untouched} kept")


def refit_zone(zone):
    man_path = os.path.join(TOOLS, "_tmp", f"map_texture_manifest_{zone}.json")
    atl_path = os.path.join(TOOLS, "_tmp", f"map_atlas_manifest_{zone}.json")
    if FORCE_SCENE or not (os.path.exists(man_path) and os.path.exists(atl_path)):
        refit_scene_zone(zone)
        return
    with open(man_path, encoding="utf-8") as f:
        entries = json.load(f)["materials"]
    with open(atl_path, encoding="utf-8") as f:
        atlas = json.load(f)["map"]
    zflags = zsc_flag_map(zone)

    done = set()
    fixed = untouched = missing = 0
    for e in entries:
        src = e.get("texture_src")
        if not src or src not in atlas:
            continue
        f = zflags.get(norm(src))
        if not f:
            untouched += 1          # terrain layer / morph / water: leave as-is
            continue
        rec = atlas[src]
        px = int(round(rec["offset"][0] * 2048))
        py = int(round(rec["offset"][1] * 2048))
        # OLD apply naming (identity of the already-created MIs)
        name = f"MI_{rec['page'].replace('/', '_')}_{px}_{py}"
        mode = e.get("mode", "OPAQUE")
        if mode == "MASK":
            name += "_M"
        elif mode == "BLEND":
            name += "_T"
        if name in done:
            continue
        done.add(name)
        path = f"{MI_ROOT}/{name}"
        if not EAL.does_asset_exist(path):
            missing += 1
            continue
        mi = EAL.load_asset(path)
        if not isinstance(mi, unreal.MaterialInstanceConstant):
            continue

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
            ov.set_editor_property("override_blend_mode", True)
            ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        else:
            # ROSE renders this opaque — clear any masked/translucent override
            ov.set_editor_property("override_blend_mode", False)
            ov.set_editor_property("override_opacity_mask_clip_value", False)
        mi.set_editor_property("base_property_overrides", ov)
        ME.update_material_instance(mi)
        EAL.save_asset(path)
        fixed += 1

    print(f"[refit] {zone}: {fixed} MIs refit, {untouched} non-ZSC kept, "
          f"{missing} MIs missing")


if ONLY:
    zones = [ONLY]
else:
    import glob as _g
    zones = sorted(os.path.basename(p)[len("map_texture_manifest_"):-len(".json")]
                   for p in _g.glob(os.path.join(TOOLS, "_tmp", "map_texture_manifest_*.json")))
print(f"[refit] zones: {zones}")
for z in zones:
    refit_zone(z)
    unreal.SystemLibrary.collect_garbage()
print("[refit] done")
