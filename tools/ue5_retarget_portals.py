"""
ue5_retarget_portals.py — Point every ARoseWarpPortal at a new destination
level (JPT01 → JPT01V2: the modern-client Zant import is THE town map now).

Walks every /Game/Maps/<ZONE>/L_<ZONE> level, rewrites DestLevel on matching
gates, saves.  Arrival coordinates carry over (both clients' JPT01 share the
zone grid/origin).

Env:  ROSE_FROM=L_JPT01  ROSE_TO=L_JPT01V2
Run headless, editor CLOSED.  Re-runnable (no-op once retargeted).
"""
import os
import unreal

FROM = os.environ.get("ROSE_FROM", "L_JPT01")
TO = os.environ.get("ROSE_TO", "L_JPT01V2")

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EAL = unreal.EditorAssetLibrary

zones = []
for a in EAL.list_assets("/Game/Maps", recursive=True, include_folder=False):
    name = a.split("/")[-1].split(".")[0]
    if name.startswith("L_") and EAL.does_asset_exist(a.split(".")[0]):
        zones.append(a.split(".")[0])
zones = sorted(set(zones))

total = 0
for level in zones:
    name = level.split("/")[-1]
    if name == TO:
        continue
    if not LES.load_level(level):
        continue
    changed = 0
    for actor in EAS.get_all_level_actors():
        if isinstance(actor, unreal.RoseWarpPortal):
            if actor.get_editor_property("dest_level") == FROM:
                actor.set_editor_property("dest_level", TO)
                changed += 1
    if changed:
        LES.save_current_level()
        unreal.log(f"[retarget] {name}: {changed} gates {FROM} -> {TO}")
        total += changed
unreal.log(f"[retarget] DONE — {total} gates retargeted")
