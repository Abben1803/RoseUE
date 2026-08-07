"""Import the generated item CSVs (RoseUE/DataTables/*.csv) as UE DataTable assets
under /Game/DataTables, using the FRoseWeaponRow / FRoseArmorRow structs.

Data-driven: re-run after regenerating the CSVs (tools/gen_item_tables.py).
Editor must be CLOSED."""
import unreal, os

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
PROJ = unreal.Paths.project_dir()
CSV_DIR = os.path.join(PROJ, "DataTables")
DST = "/Game/DataTables"

WROW = unreal.load_object(None, "/Script/RoseUE.RoseWeaponRow")
AROW = unreal.load_object(None, "/Script/RoseUE.RoseArmorRow")
NROW = unreal.load_object(None, "/Script/RoseUE.RoseNpcRow")
SKROW = unreal.load_object(None, "/Script/RoseUE.RoseSkillRow")        # RoseSkillTypes.h
JROW = unreal.load_object(None, "/Script/RoseUE.RoseJobRow")
SPROW = unreal.load_object(None, "/Script/RoseUE.RoseSkillPointRow")
SIROW = unreal.load_object(None, "/Script/RoseUE.RoseSimpleItemRow")   # consumable/gem/material
QROW = unreal.load_object(None, "/Script/RoseUE.RoseQuestRow")         # RoseQuest.h (journal)
STROW = unreal.load_object(None, "/Script/RoseUE.RoseStoreRow")        # NPC store tabs
PPROW = unreal.load_object(None, "/Script/RoseUE.RosePatPartRow")      # RosePatTypes.h
PMROW = unreal.load_object(None, "/Script/RoseUE.RosePatMotionRow")    # PAT motion grid

TABLES = [("weapons", WROW), ("body", AROW), ("arms", AROW), ("foot", AROW), ("cap", AROW),
          ("consumable", SIROW), ("gem", SIROW), ("material", SIROW),
          # subwpn is defensive EQUIPMENT (shields, magic tools): armour row, not
          # simple-item — SIROW has no Defense/MagicResist, so those columns were
          # silently dropped on import and every shield arrived with 0 defence.
          ("faceitem", AROW), ("jewel", AROW), ("subwpn", AROW), ("pat", SIROW),
          ("npcs", NROW),
          ("skills", SKROW), ("jobs", JROW), ("skill_points", SPROW),
          ("quests", QROW), ("stores", STROW), ("back", AROW),
          ("pat_parts", PPROW), ("pat_motion", PMROW)]


def import_dt(name, struct):
    csv = os.path.join(CSV_DIR, name + ".csv")
    if not os.path.isfile(csv):
        print(f"[dt] {name}: CSV missing ({csv})"); return
    if struct is None:
        print(f"[dt] {name}: row struct not found (compile first)"); return
    factory = unreal.CSVImportFactory()
    s = factory.get_editor_property("automated_import_settings")
    s.set_editor_property("import_type", unreal.CSVImportType.ECSV_DATA_TABLE)
    s.set_editor_property("import_row_struct", struct)
    factory.set_editor_property("automated_import_settings", s)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", csv)
    task.set_editor_property("destination_path", DST)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("factory", factory)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    AT.import_asset_tasks([task])

    dt = EAL.load_asset(f"{DST}/{name}")
    nrows = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)) if dt else 0
    print(f"[dt] {name}: {'OK' if dt else 'FAIL'}  rows={nrows}")


for name, struct in TABLES:
    import_dt(name, struct)
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
print("[dt] done")
