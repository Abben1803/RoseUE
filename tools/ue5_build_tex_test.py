"""Test whether update_resource() builds a nullrhi-imported texture's platform
data (size should go from placeholder to real)."""
import unreal
EAL = unreal.EditorAssetLibrary
ROOT = "/Game/Characters/Modular/Female"

def sz(t):
    try: return f"{t.blueprint_get_size_x()}x{t.blueprint_get_size_y()}"
    except Exception: return "?"

for it in ["body_5", "body_6", "cap_5", "arms_5"]:
    texdir = f"{ROOT}/{it}/{it}/Textures"
    al = EAL.list_assets(texdir)
    if not al:
        print(f"[buildtex] {it}: no tex"); continue
    t = EAL.load_asset(al[0])
    before = sz(t)
    t.update_resource()
    print(f"[buildtex] {it}: {t.get_name()} before={before} after={sz(t)}")
