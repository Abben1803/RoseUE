"""Verify the ground-drop wiring end to end, in the DataTables the GAME reads.

FieldItemId present and non-zero  ->  /Game/Rose/Equipment/FieldItem/SM_field_<id> exists

Absence of an import warning is NOT proof the column bound — a CSV column that
matches no UPROPERTY is dropped quietly.  This reads the imported asset back.

Reads via get_data_table_column_as_string: DataTableFunctionLibrary has no
get_data_table_row_from_name in UE 5.8 (row structs are not exposed to Python
that way), but a named column can be pulled straight out as strings.
"""
import unreal

EAL = unreal.EditorAssetLibrary
DTF = unreal.DataTableFunctionLibrary
TABLES = ["weapons", "cap", "back", "consumable", "jewel", "material", "subwpn"]

total = withmodel = resolvable = 0
missing_ids = set()

for name in TABLES:
    path = f"/Game/DataTables/{name}"
    if not EAL.does_asset_exist(path):
        print(f"[verify] {name}: table missing at {path}")
        continue
    dt = EAL.load_asset(path)

    ids = DTF.get_data_table_column_as_string(dt, "FieldItemId")
    if not ids:
        print(f"[verify] {name}: FieldItemId column ABSENT or empty — did not bind")
        continue
    names = DTF.get_data_table_column_as_string(dt, "DisplayName")

    shown = 0
    for i, raw in enumerate(ids):
        try:
            fid = int(raw or 0)
        except ValueError:
            continue
        total += 1
        if fid <= 0:
            continue
        withmodel += 1
        mesh = f"/Game/Rose/Equipment/FieldItem/SM_field_{fid}"
        ok = EAL.does_asset_exist(mesh)
        if ok:
            resolvable += 1
        else:
            missing_ids.add(fid)
        if shown < 3:
            nm = names[i] if names and i < len(names) else ""
            print(f"[verify] {name:11} FieldItemId={fid:5} "
                  f"mesh={'OK' if ok else 'MISSING'}   {nm[:30]}")
            shown += 1

print(f"[verify] rows {total} | with a field model {withmodel} | "
      f"mesh resolvable {resolvable}")
if missing_ids:
    print(f"[verify] ids with no mesh (fall back to the cube): "
          f"{sorted(missing_ids)}")
print("[verify] FAIL — column did not bind" if withmodel == 0 else "[verify] PASS")
