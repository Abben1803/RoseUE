"""ue5_reimport_gear_atlases.py — re-import ONLY the 32 gear atlas pages.

Use after `build_gear_atlases.py` regenerates the PNGs (e.g. the alpha-dilation
black-fringe fix).  Deliberately does NOT touch the master materials:
`ue5_gear_materials.py` would recreate M_RoseGear as MASKED and clobber the
ZSC-faithful three-master setup from `ue5_gear_masters_zsc.py`
(M_RoseGear=OPAQUE / M_RoseGearMasked / M_RoseGearBlend).

Alpha matters here — the masters feed atlas A into OpacityMask — so this asserts
the imported textures kept their alpha channel instead of silently trusting the
importer's auto-detected compression.

Headless, editor CLOSED.  -nullrhi is fine (textures compile no shaders).
"""
import os
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
SRC = os.path.normpath(os.path.join(
    unreal.Paths.project_dir(), "SourceAssets", "GLTF", "AVATAR", "GEAR_ATLAS"))
DST = "/Game/Characters/Gear/Atlas"

pngs = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".png"))
print(f"[atlasre] re-importing {len(pngs)} atlas pages -> {DST}")

tasks = []
for f in pngs:
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(SRC, f))
    t.set_editor_property("destination_path", DST)
    t.set_editor_property("destination_name", "T_" + os.path.splitext(f)[0])
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    tasks.append(t)
AT.import_asset_tasks(tasks)

bad = 0
for f in pngs:
    name = "T_" + os.path.splitext(f)[0]
    tex = EAL.load_asset(f"{DST}/{name}")
    if not tex:
        print(f"[atlasre] MISSING {name}")
        bad += 1
        continue
    # The gear masters route atlas alpha into OpacityMask; a compression setting
    # that drops alpha would silently reintroduce the black-fringe bug.
    if tex.get_editor_property("compression_no_alpha"):
        tex.set_editor_property("compression_no_alpha", False)
        EAL.save_asset(f"{DST}/{name}")
        print(f"[atlasre] {name}: cleared compression_no_alpha")
    print(f"[atlasre] {name}: fmt={tex.get_editor_property('compression_settings')} "
          f"srgb={tex.get_editor_property('srgb')} no_alpha="
          f"{tex.get_editor_property('compression_no_alpha')}")

print(f"[atlasre] done: {len(pngs) - bad}/{len(pngs)} imported")
