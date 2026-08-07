"""
ue5_fix_materials.py — Robustly (re)assign M_RoseChar material instances to a list
of modular items, reading the texture directly from each item's imported Texture
asset (by section index).  Verbose logging.

Set ROSE_FIX="hair_110,face_1,..." ; defaults to all hair_* + face_* under Female.
"""
import unreal
import os

ROOT = "/Game/Characters/Modular/Female"
MAT_PATH = "/Game/Characters/Materials"
MASTER = f"{MAT_PATH}/M_RoseChar"

ME = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary


def items():
    env = os.environ.get("ROSE_FIX", "").strip()
    if env:
        return [x for x in env.split(",") if x.strip()]
    # Default: EVERY modular item folder (base + all equip/appearance slots).
    out = []
    for d in EAL.list_assets(ROOT, recursive=False, include_folder=True):
        name = d.rstrip("/").rsplit("/", 1)[-1]
        if name and not name.startswith("_"):
            out.append(name)
    return sorted(out)


def textures_for(item):
    tex_dir = f"{ROOT}/{item}/{item}/Textures"
    if not EAL.does_directory_exist(tex_dir):
        return []
    paths = sorted(EAL.list_assets(tex_dir, recursive=False))
    # order by trailing _texture_<n>
    def key(p):
        n = p.rsplit("_", 1)[-1]
        return int(n) if n.isdigit() else 0
    return sorted(paths, key=key)


def run():
    master = EAL.load_asset(MASTER)
    if not master:
        print(f"[fix] MASTER not found: {MASTER}")
        return

    for it in items():
        sm_path = f"{ROOT}/{it}/{it}/SkeletalMeshes/{it}"
        sm = EAL.load_asset(sm_path)
        if not sm:
            print(f"[fix] {it}: mesh missing")
            continue
        tex_paths = textures_for(it)
        inst_dir = f"{ROOT}/{it}/{it}/Materials"
        if not EAL.does_directory_exist(inst_dir):
            EAL.make_directory(inst_dir)

        mats = sm.get_editor_property("materials")
        out = []
        for i, slot in enumerate(mats):
            tp = tex_paths[i] if i < len(tex_paths) else (tex_paths[0] if tex_paths else None)
            tex = EAL.load_asset(tp) if tp else None
            iname = f"MI_{it}_{i}"
            ipath = f"{inst_dir}/{iname}"
            try:
                # Reuse an existing instance rather than delete+recreate (which
                # fails because the deleted package is still registered this run).
                if EAL.does_asset_exist(ipath):
                    inst = EAL.load_asset(ipath)
                else:
                    inst = AT.create_asset(iname, inst_dir, unreal.MaterialInstanceConstant,
                                           unreal.MaterialInstanceConstantFactoryNew())
                if inst is None:
                    print(f"[fix] {it}[{i}]: create/load returned None ({ipath})")
                    out.append(slot); continue
                ME.set_material_instance_parent(inst, master)
                if tex:
                    ME.set_material_instance_texture_parameter_value(inst, "BaseColor", tex)
                EAL.save_asset(ipath)
                slot.set_editor_property("material_interface", inst)
                print(f"[fix] {it}[{i}]: -> {iname} tex={tex.get_name() if tex else None}")
            except Exception as e:
                print(f"[fix] {it}[{i}]: EXCEPTION {e}")
            out.append(slot)
        sm.set_editor_property("materials", out)
        EAL.save_asset(sm_path)
    print("[fix] done")


run()
