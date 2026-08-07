"""ue5_probe_fixes.py — Verify the 2026-07-20 four-bug sweep actually resolves at
runtime (not just on disk):

  1. pat_parts / pat_motion DataTables load and GetPatPartRow's key shape works
     (these were declared-but-never-loaded, which silently blocked every PAT equip)
  2. weapon static meshes load at the path ARoseCharacter::LoadWeaponStatic builds
  3. faceitem skeletal meshes load at the path ARoseCharacter::LoadPart builds

Run headless (RHI not required for pure asset loads):
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=tools/ue5_probe_fixes.py
    -unattended -nopause -nosplash -stdout -nullrhi
"""
import unreal

EAL = unreal.EditorAssetLibrary
ok = True


def check(label, cond, detail=""):
    global ok
    if not cond:
        ok = False
    print(f"[probe] {'PASS' if cond else 'FAIL'}  {label}{('  ' + detail) if detail else ''}")


# ── 1. PAT tables ────────────────────────────────────────────────────────────
for name in ("pat", "pat_parts", "pat_motion"):
    path = f"/Game/DataTables/{name}.{name}"
    dt = unreal.load_object(None, path)
    n = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)) if dt else 0
    check(f"DataTable {name}", dt is not None and n > 0, f"rows={n}")

# GetPatPartRow looks up row name "pat_<id>" in pat_parts — verify that key shape.
dt = unreal.load_object(None, "/Game/DataTables/pat_parts.pat_parts")
if dt:
    names = {str(x) for x in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)}
    # Use ids that actually exist — LIST_PAT has gaps (e.g. 99/100 are absent).
    for pid in (1, 2, 101):
        check(f"pat_parts row key pat_{pid}", f"pat_{pid}" in names)

# ── 2. weapons (LoadWeaponStatic path shape) ─────────────────────────────────
for wid in (28, 59, 81, 129, 155):
    n = f"weapon_{wid}"
    p = f"/Game/Characters/Modular/WeaponsStatic/{n}/{n}/StaticMeshes/{n}.{n}"
    check(f"static mesh {n}", unreal.load_object(None, p) is not None)

# A dual-wield off-hand (LoadWeaponStatic(Base + "_off")).
n = "weapon_1151_off"
p = f"/Game/Characters/Modular/WeaponsStatic/{n}/{n}/StaticMeshes/{n}.{n}"
check("static mesh weapon_1151_off", unreal.load_object(None, p) is not None)

# ── 3. face items (LoadPart path shape), both genders ────────────────────────
for gender in ("Female", "Male"):
    for fid in (1, 2, 100):
        n = f"faceitem_{fid}"
        p = f"/Game/Characters/Modular/{gender}/{n}/{n}/SkeletalMeshes/{n}.{n}"
        check(f"{gender} {n}", unreal.load_object(None, p) is not None)

# Coverage counts.
ws = len([a for a in EAL.list_assets("/Game/Characters/Modular/WeaponsStatic", True, False)
          if a.endswith("StaticMeshes/" + a.split("/")[-1])])
print(f"[probe] WeaponsStatic static meshes found: {ws}")

print(f"[probe] RESULT: {'ALL PASS' if ok else 'FAILURES PRESENT'}")
