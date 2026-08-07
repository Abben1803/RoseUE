"""Force every imported monster material OPAQUE (+ two-sided).

ROSE NPC materials carry alpha_test=1 with specular/glow packed in the DDS
alpha channel — Interchange imports that as MASKED and the alpha clips body
pixels away (the same weapon saga; see STATUS.md).  Fix on the M_GLTF
instances under /Game/Monsters:
  * scalar 'AlphaMode' = 0   (glTF: 0=OPAQUE — base-color alpha ignored)
  * blend-mode override = Opaque (explicit, belt and braces)
  * two-sided = True         (thin parts: ears, fins, leaves)

Run headless, editor CLOSED:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=tools/ue5_fix_monster_materials.py ...
Env: ROSE_ONLY="npc_1,npc_2" to limit (default: every npc_* under /Game/Monsters).
"""
import os
import unreal

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
ROOT = "/Game/Monsters"

ONLY = {x.strip() for x in os.environ.get("ROSE_ONLY", "").split(",") if x.strip()}

n = setc = 0
seen = set()
for d in EAL.list_assets(ROOT, recursive=False, include_folder=True):
    name = d.rstrip("/").rsplit("/", 1)[-1]
    if not name.startswith("npc_") or (ONLY and name not in ONLY):
        continue
    sm = EAL.load_asset(f"{ROOT}/{name}/{name}/SkeletalMeshes/{name}")
    if not sm:
        print(f"[mobmat] {name}: mesh missing, skipped")
        continue
    for s in sm.get_editor_property("materials"):
        mi = s.get_editor_property("material_interface")
        if not isinstance(mi, unreal.MaterialInstanceConstant):
            continue
        path = mi.get_path_name().split(".")[0]
        if path in seen:
            continue
        seen.add(path)
        ME.set_material_instance_scalar_parameter_value(mi, "AlphaMode", 0.0)
        ov = mi.get_editor_property("base_property_overrides")
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        ov.set_editor_property("override_two_sided", True)
        ov.set_editor_property("two_sided", True)
        mi.set_editor_property("base_property_overrides", ov)
        ME.update_material_instance(mi)
        EAL.save_asset(path)
        setc += 1
    n += 1
print(f"[mobmat] done: {n} monsters, {setc} materials -> opaque + two-sided")
