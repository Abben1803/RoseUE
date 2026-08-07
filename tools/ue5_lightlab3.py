"""
ue5_lightlab3.py — screenshot a zone AS SAVED (no light changes): PlayerStart
camera, 4 yaws.  Env: ROSE_ZONE, ROSE_TAG (filename tag, default 'check').
Run (editor CLOSED, RHI): -ExecCmds="py C:/rose-next-classic/tools/ue5_lightlab3.py"
"""
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "JD01").upper()
TAG = os.environ.get("ROSE_TAG", "check")
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
eye = unreal.Vector(start.x, start.y, start.z + 250)
print(f"[lab3] zone={ZONE} eye={eye}")

STEPS = []
def cam(yaw):
    def do():
        UES.set_level_viewport_camera_info(eye, unreal.Rotator(0, -12, yaw))
    return do

def snap(name):
    def do():
        p = os.path.join(SHOTS, f"{ZONE}_{TAG}_yaw{name}.png")
        unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, p)
        print(f"[lab3] shot {p}")
    return do

for yaw in (0, 90, 180, 270):
    STEPS.append(cam(yaw))
    STEPS.append(snap(yaw))

import time
# Slate post-tick can fire several times per engine frame, so pace by WALL
# CLOCK (2s/step) — tick counting once collapsed all steps into one frame and
# the editor quit before the queued screenshots ever rendered.
state = {"i": -1, "t": time.monotonic() + 4.0, "handle": None}
def on_tick(dt):
    now = time.monotonic()
    if now < state["t"]:
        return
    state["t"] = now + 2.0
    state["i"] += 1
    if state["i"] >= len(STEPS):
        print("[lab3] done")
        unreal.unregister_slate_post_tick_callback(state["handle"])
        unreal.SystemLibrary.quit_editor()
        return
    try:
        STEPS[state["i"]]()
    except Exception as e:
        print(f"[lab3] step failed: {e}")

state["handle"] = unreal.register_slate_post_tick_callback(on_tick)
