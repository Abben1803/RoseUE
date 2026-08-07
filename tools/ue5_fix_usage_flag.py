"""Set the 'Used with Skeletal Mesh' usage flag on M_RoseChar so UE stops
substituting the default grey material on skeletal/merged meshes."""
import unreal

EAL = unreal.EditorAssetLibrary
PATH = "/Game/Characters/Materials/M_RoseChar"

mat = EAL.load_asset(PATH)
if not mat:
    print("[usage] M_RoseChar NOT FOUND")
else:
    before = mat.get_editor_property("used_with_skeletal_mesh")
    mat.set_editor_property("used_with_skeletal_mesh", True)
    after = mat.get_editor_property("used_with_skeletal_mesh")
    ok = EAL.save_asset(PATH, only_if_is_dirty=False)
    print(f"[usage] M_RoseChar used_with_skeletal_mesh {before} -> {after}, saved={ok}")

# Resave the MI instances too, to clear the "recompiles every launch" warning.
n = 0
for d in EAL.list_assets("/Game/Characters/Modular/Female", recursive=True):
    if "/Materials/MI_" in d:
        if EAL.save_asset(d.split(".")[0], only_if_is_dirty=False):
            n += 1
print(f"[usage] resaved {n} MI instances")
