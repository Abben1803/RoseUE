"""
ue5_build_charselect.py — build /Game/Maps/CharacterSelect/L_CharacterSelect:
the ROSE-style character select/create screen (a camera flying through a town
while you pick/create a character).

Recipe:
  1. duplicate the promoted JPT01 town as the backdrop (save_map as the new level)
  2. strip gameplay actors (spawners / portals / npcs / dummies / monsters)
  3. set the level's GameMode override -> ARoseCharSelectGameMode
  4. place ARoseCharacterCreator (the preview mannequin) on the ground at PlayerStart
  5. place ARoseCharSelectCamPath with a looping fly-through spline around it
  6. ensure a PlayerStart (the camera pawn spawns there)
  7. save

DefaultEngine.ini GameDefaultMap is pointed here by the caller (post-batch script).
Run headless, editor CLOSED, WITH RHI is not required (-nullrhi fine).
Requires the game DLL compiled with the char-select classes.
"""
import math
import unreal

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

SRC = "/Game/Maps/TITLE_JPT/L_TITLE_JPT"   # the ROSE Title platform (classic backdrop)
DST = "/Game/Maps/CharacterSelect/L_CharacterSelect"

# ── 1. duplicate the town ────────────────────────────────────────────────────
if not LES.load_level(SRC):
    raise RuntimeError("could not load Title backdrop")
world = UES.get_editor_world()
if not unreal.EditorLoadingAndSavingUtils.save_map(world, DST):
    raise RuntimeError("save_map to CharacterSelect failed")
LES.load_level(DST)
world = UES.get_editor_world()
print("[cs] duplicated Title backdrop -> CharacterSelect")

# ── 2. strip gameplay actors ─────────────────────────────────────────────────
STRIP = (unreal.RoseMonsterSpawner, unreal.RoseWarpPortal, unreal.RoseNpc,
         unreal.RoseMonster, unreal.RoseTargetDummy, unreal.RoseGroundItem)
removed = 0
player_start = None
for a in EAS.get_all_level_actors():
    if isinstance(a, unreal.PlayerStart):
        player_start = a
        continue
    if isinstance(a, STRIP):
        EAS.destroy_actor(a)
        removed += 1
print(f"[cs] stripped {removed} gameplay actors")

# ── 6. ensure a PlayerStart (camera pawn spawn) ──────────────────────────────
if player_start is None:
    player_start = EAS.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(544000, -512000, 300))
start_loc = player_start.get_actor_location()

# ground-trace the stand position from the PlayerStart
hit = world.line_trace_single_by_channel(
    unreal.Vector(start_loc.x, start_loc.y, start_loc.z + 5000),
    unreal.Vector(start_loc.x, start_loc.y, start_loc.z - 5000),
    unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
    False, []) if hasattr(world, "line_trace_single_by_channel") else None
ground_z = start_loc.z
try:
    if hit and hit.get_editor_property("blocking_hit"):
        ground_z = hit.get_editor_property("location").z
except Exception:
    pass
stand = unreal.Vector(start_loc.x, start_loc.y, ground_z)

# ── 4. the director (spawns the roster avatars in an arc + drives the camera) ─
# One actor owns the whole scene: on BeginPlay it reads URoseCharSlotSave, sorts
# by level, and spawns up to 5 ARoseCharacterCreator avatars on a shallow arc
# facing the camera (ROSE c_AvatarPositions layout).  Its +X (forward) is the
# direction the avatars face / the camera views from — rotate it in-editor if the
# arc points into terrain.  No preview mannequin or circular cam path anymore.
director = EAS.spawn_actor_from_class(unreal.RoseCharSelectDirector, stand,
                                      unreal.Rotator(0, 0, 0))
director.set_actor_label("CharSelect_Director")
print(f"[cs] placed RoseCharSelectDirector at {stand}")

# ── 3. GameMode override ─────────────────────────────────────────────────────
ws = world.get_world_settings()
ws.set_editor_property("default_game_mode", unreal.RoseCharSelectGameMode)
print("[cs] GameMode override -> RoseCharSelectGameMode")

LES.save_current_level()
print("[cs] done ->", DST)
