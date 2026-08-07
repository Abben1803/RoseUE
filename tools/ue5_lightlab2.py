"""
ue5_lightlab2.py — sun:sky ratio sweep for the classic ROSE flat look.
Loads ROSE_ZONE (default SUM_EVENT), fixed camera, screenshots each
(sun, sky) pair.  Nothing is saved.

Run (editor CLOSED, RHI):
  UnrealEditor-Cmd.exe <proj> -ExecCmds="py C:/rose-next-classic/tools/ue5_lightlab2.py" ...
"""
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "SUM_EVENT").upper()
SHOTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_tmp", "lightlab")
os.makedirs(SHOTS, exist_ok=True)

PAIRS = [(4.0, 3.0), (2.0, 6.0), (1.0, 8.0), (0.5, 10.0)]

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

if not LES.load_level(f"/Game/Maps/{ZONE}/L_{ZONE}"):
    raise RuntimeError("load failed")

sun = sky = None
start = None
for a in EAS.get_all_level_actors():
    if isinstance(a, unreal.DirectionalLight):
        sun = a
    elif isinstance(a, unreal.SkyLight):
        sky = a
    elif isinstance(a, unreal.PlayerStart) and start is None:
        start = a.get_actor_location()
if start is None:
    start = unreal.Vector(0, 0, 200)
eye = unreal.Vector(start.x, start.y, start.z + 250)
UES.set_level_viewport_camera_info(eye, unreal.Rotator(0, -12, 180))
print(f"[lab2] zone={ZONE} pairs={PAIRS}")

STEPS = []
def set_pair(s, k):
    def do():
        sun.light_component.set_editor_property("intensity", s)
        sky.light_component.set_editor_property("intensity", k)
        sky.light_component.recapture_sky()
        print(f"[lab2] sun={s} sky={k}")
    return do

def snap(tag):
    def do():
        p = os.path.join(SHOTS, f"{ZONE}_{tag}.png")
        unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, p)
        print(f"[lab2] shot {p}")
    return do

for s, k in PAIRS:
    tag = f"40_sun{s}_sky{k}".replace(".", "p")
    STEPS.append(set_pair(s, k))
    STEPS.append(snap(tag))

state = {"i": -1, "tick": 0, "handle": None}
def on_tick(dt):
    state["tick"] += 1
    if state["tick"] % 25 != 0:      # generous settle time (exposure adaptation)
        return
    state["i"] += 1
    if state["i"] >= len(STEPS):
        print("[lab2] done")
        unreal.unregister_slate_post_tick_callback(state["handle"])
        unreal.SystemLibrary.quit_editor()
        return
    try:
        STEPS[state["i"]]()
    except Exception as e:
        print(f"[lab2] step failed: {e}")

state["handle"] = unreal.register_slate_post_tick_callback(on_tick)
