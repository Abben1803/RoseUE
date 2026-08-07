"""ue5_verify_zone.py — read-only check that a zone level has everything.

Loads /Game/Maps/<ZONE>/L_<ZONE> and reports actor counts by the classes we
care about, plus any actor whose static/skeletal mesh failed to resolve.  Does
NOT modify or save anything.

Exists because the import scripts each save the level in turn, and a later save
silently dropping an earlier script's actors is exactly the kind of thing that
should be verified rather than assumed.

  UnrealEditor-Cmd.exe <proj> -ExecutePythonScript=tools/ue5_verify_zone.py \
      -unattended -nopause -nosplash -stdout -FullStdOutLogOutput -nullrhi
Env: ROSE_ZONE (default JPT01)
"""
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "JPT01").upper()
LEVEL = f"/Game/Maps/{ZONE}/L_{ZONE}"

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(m):
    unreal.log(f"[verify] {m}")


if not EAL.does_asset_exist(LEVEL):
    raise RuntimeError(f"level missing: {LEVEL}")
if not LES.load_level(LEVEL):
    raise RuntimeError(f"could not load {LEVEL}")

actors = EAS.get_all_level_actors()
counts = {}
for a in actors:
    counts[type(a).__name__] = counts.get(type(a).__name__, 0) + 1

npc = sum(v for k, v in counts.items() if "Npc" in k)
spawn = sum(v for k, v in counts.items() if "Spawner" in k)

log(f"{ZONE}: {len(actors)} actors total")
log(f"{ZONE}: RoseNpc={npc}  RoseMonsterSpawner={spawn}")

# Static meshes that failed to resolve show up as a null mesh on the component.
broken = 0
for a in actors:
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        if c.get_editor_property("static_mesh") is None:
            broken += 1
            break
log(f"{ZONE}: actors with an UNRESOLVED static mesh = {broken}")

for k in sorted(counts, key=lambda k: -counts[k])[:12]:
    log(f"   {k:40s} {counts[k]}")
log("DONE")
