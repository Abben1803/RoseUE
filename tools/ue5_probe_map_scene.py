"""
ue5_probe_map_scene.py — Dump every actor in a map scene level to JSON so the
placements can be verified offline against the ROSE IFO/ZSC source data.

Env:
  ROSE_MAP_ZONE   zone key (default JPT01)
  ROSE_PROBE_OUT  output json (default tools/map_scene_probe.json)
"""
import os
import json
import unreal

ZONE = os.environ.get("ROSE_MAP_ZONE", "JPT01")
OUT = os.environ.get("ROSE_PROBE_OUT",
                     os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                  "map_scene_probe.json"))
LEVEL = f"/Game/Maps/{ZONE}/L_{ZONE}"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not les.load_level(LEVEL):
    raise RuntimeError(f"could not load {LEVEL}")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

GROUPS = ("Objects", "Terrain", "Collision", "Water")


def group_of(a):
    p = a.get_attach_parent_actor()
    while p:
        if p.get_actor_label() in GROUPS:
            return p.get_actor_label()
        p = p.get_attach_parent_actor()
    return None


rows = []
for a in eas.get_all_level_actors():
    if not isinstance(a, unreal.StaticMeshActor):
        continue
    loc = a.get_actor_location()
    rot = a.get_actor_rotation()
    scl = a.get_actor_scale3d()
    mesh = a.static_mesh_component.static_mesh
    rows.append({
        "label": a.get_actor_label(),
        "group": group_of(a),
        "loc": [loc.x, loc.y, loc.z],
        "rot": [rot.roll, rot.pitch, rot.yaw],
        "scale": [scl.x, scl.y, scl.z],
        "mesh": mesh.get_name() if mesh else None,
    })

with open(OUT, "w") as f:
    json.dump({"zone": ZONE, "actors": rows}, f)
unreal.log(f"[probe] wrote {len(rows)} static-mesh actors -> {OUT}")
