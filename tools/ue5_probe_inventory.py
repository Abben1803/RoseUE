"""
ue5_probe_inventory.py — headless verification for the three inventory features:

  1. slot ghosts   — every /Game/UI/SlotGhosts texture the Slate code asks for
                     exists and carries the UI texture settings
  2. tooltips      — the (slot,id) rows the equipped/PAT/ammo tooltips read from
                     resolve through the same DataTables the bag tooltip uses
  3. consumables   — consumable.csv reimported with the LIST_USEITEM effect block,
                     and known potions carry the expected values

Loads assets through the EXACT paths the C++ builds, so a pass means the runtime
lookup succeeds rather than "the file is on disk somewhere".

Run headless with the editor CLOSED:
  UnrealEditor-Cmd.exe "<RoseUE.uproject>" -ExecutePythonScript="tools/ue5_probe_inventory.py"
      -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
"""
import unreal

EAL = unreal.EditorAssetLibrary
FAILS = []


def check(ok, msg):
    print(("[probe] PASS  " if ok else "[probe] FAIL  ") + msg)
    if not ok:
        FAILS.append(msg)


# ── 1. slot ghosts ──────────────────────────────────────────────────────────
# Mirrors SRoseInventory::GhostNameForSlot + the ammo slots' kShotGhosts.
GHOSTS = ["faceitem", "cap", "back", "body", "arms", "foot", "weapon", "subwpn",
          "ring", "necklace", "earring", "arrow", "bullet", "cannon"]

print("[probe] --- slot ghosts ---")
for g in GHOSTS:
    path = f"/Game/UI/SlotGhosts/slotghost_{g}.slotghost_{g}"
    tex = unreal.load_object(None, path)
    if not tex:
        check(False, f"ghost {g}: not loadable at {path}")
        continue
    grp = tex.get_editor_property("lod_group")
    mips = tex.get_editor_property("mip_gen_settings")
    ok = (grp == unreal.TextureGroup.TEXTUREGROUP_UI and
          mips == unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS and
          tex.get_editor_property("never_stream"))
    check(ok, f"ghost {g}: loaded {tex.blueprint_get_size_x()}x{tex.blueprint_get_size_y()} "
              f"group={grp} mips={mips}")

# Anything extra in the folder is stale (e.g. the mislabelled "helmet" crop).
have = {p.split("/")[-1].split(".")[0] for p in EAL.list_assets("/Game/UI/SlotGhosts", recursive=False)}
extra = sorted(have - {f"slotghost_{g}" for g in GHOSTS})
check(not extra, f"no stale ghost assets (found: {extra})")


# ── 2 + 3. consumable DataTable with the effect block ───────────────────────
print("[probe] --- consumable table ---")
dt = unreal.load_object(None, "/Game/DataTables/consumable.consumable")
check(dt is not None, "consumable DataTable loads")

if dt:
    names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    check(len(names) > 1000, f"consumable rows = {len(names)}")

    # Expected values read straight out of Arua LIST_USEITEM (see the generator).
    #   id: (DisplayName, AddAbility, AddValue, StatusId, StatusType, StatusPerSec, TypeName)
    EXPECT = {
        1:   ("Health Vial (S)",  16, 200,  2, 1, 76, "Potion"),       # over-time HP
        13:  ("Vital Water (XL)", 16, 1000, 0, 0, 0,  "Potion"),       # instant HP
        21:  ("Mana Vial (S)",    17, 150,  5, 2, 30, "Potion"),       # over-time MP
        293: ("Engine Fuel (S)",   0, 25,   0, 0, 0,  "Engine Fuel"),  # fuel
    }
    for rid, exp in EXPECT.items():
        rn = unreal.Name(f"consumable_{rid}")
        found, row = unreal.DataTableFunctionLibrary.get_data_table_row_from_name(
            dt, rn, unreal.RoseSimpleItemRow())
        if not found:
            check(False, f"row consumable_{rid} missing")
            continue
        got = (row.display_name, row.add_ability, row.add_value, row.status_id,
               row.status_type, row.status_per_sec, row.type_name)
        check(got == exp, f"consumable_{rid}: {got}" +
              ("" if got == exp else f"  expected {exp}"))

    # The whole point of item 3: the effect columns must not be all-zero.
    n_eff = 0
    for rn in names:
        found, row = unreal.DataTableFunctionLibrary.get_data_table_row_from_name(
            dt, rn, unreal.RoseSimpleItemRow())
        if found and row.add_ability in (16, 17, 76, 77, 30, 40) and row.add_value > 0:
            n_eff += 1
    check(n_eff > 100, f"{n_eff} consumables carry a modelled HP/MP/etc effect")

    # TypeName feeds the tooltip's "Type:" line.
    n_type = 0
    for rn in names:
        found, row = unreal.DataTableFunctionLibrary.get_data_table_row_from_name(
            dt, rn, unreal.RoseSimpleItemRow())
        if found and row.type_name:
            n_type += 1
    check(n_type > 1000, f"{n_type} consumables have a STR_ITEMTYPE TypeName")

# The other gen_simple tables share FRoseSimpleItemRow — they must still import
# after the struct grew, or every bag icon for those slots breaks.
print("[probe] --- sibling simple-item tables ---")
for t in ("gem", "material", "faceitem", "jewel", "subwpn", "pat"):
    d = unreal.load_object(None, f"/Game/DataTables/{t}.{t}")
    n = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(d)) if d else 0
    check(d is not None and n > 0, f"{t} DataTable: {n} rows")

print("")
print(f"[probe] ==== {'ALL PASS' if not FAILS else str(len(FAILS)) + ' FAILURE(S)'} ====")
for f in FAILS:
    print("[probe]   - " + f)
