"""
ue5_shot_terrain.py — elevated far-view screenshots to expose terrain patch
seams (Nanite cluster decimation shows at distance, not at the PlayerStart).
Env: ROSE_ZONE, ROSE_TAG. Camera: high above PlayerStart looking down at a
shallow angle, 2 yaws + one very high top-down.
Run (editor CLOSED, RHI): -ExecCmds="py C:/rose-next-classic/tools/ue5_shot_terrain.py"
"""
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "JG01").upper()
TAG = os.environ.get("ROSE_TAG", "terrain")
SHOTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_tmp", "lightlab")
os.makedirs(SHOTS, exist_ok=True)

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

if not LES.load_level(f"/Game/Maps/{ZONE}/L_{ZONE}"):
    raise RuntimeError("load failed")

start = None
for a in EAS.get_all_level_actors():
    if isinstance(a, unreal.PlayerStart):
        start = a.get_actor_location()
        break
if start is None:
    start = unreal.Vector(0, 0, 200)
print(f"[shot] zone={ZONE} start={start}")

VIEWS = [
    ("mid0",   unreal.Vector(start.x, start.y, start.z + 4000),  unreal.Rotator(0, -25, 0)),
    ("mid90",  unreal.Vector(start.x, start.y, start.z + 4000),  unreal.Rotator(0, -25, 90)),
    ("mid180", unreal.Vector(start.x, start.y, start.z + 4000),  unreal.Rotator(0, -25, 180)),
    ("mid270", unreal.Vector(start.x, start.y, start.z + 4000),  unreal.Rotator(0, -25, 270)),
    ("top",    unreal.Vector(start.x, start.y, start.z + 30000), unreal.Rotator(0, -88, 0)),
]

STEPS = []
for name, eye, rot in VIEWS:
    def cam(eye=eye, rot=rot):
        UES.set_level_viewport_camera_info(eye, rot)
    def snap(name=name):
        p = os.path.join(SHOTS, f"{ZONE}_{TAG}_{name}.png")
        unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, p)
        print(f"[shot] {p}")
    STEPS.append(cam)
    STEPS.append(snap)

import time
state = {"i": -1, "t": time.monotonic() + 4.0, "handle": None}
def on_tick(dt):
    now = time.monotonic()
    if now < state["t"]:
        return
    state["t"] = now + 2.0
    state["i"] += 1
    if state["i"] >= len(STEPS):
        print("[shot] done")
        unreal.unregister_slate_post_tick_callback(state["handle"])
        unreal.SystemLibrary.quit_editor()
        return
    try:
        STEPS[state["i"]]()
    except Exception as e:
        print(f"[shot] step failed: {e}")

state["handle"] = unreal.register_slate_post_tick_callback(on_tick)
