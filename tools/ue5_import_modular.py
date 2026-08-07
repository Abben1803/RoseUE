"""
ue5_import_modular.py — Import modular avatar parts and wire them for a
Leader-Pose character creator.

  * imports base.glb + hair_*.glb + face_*.glb + faceitem_*.glb
  * re-skins every section with M_RoseChar (two-sided masked)
  * marks each part skeleton compatible (both ways) with base's skeleton so
    Leader Pose Component + the base animations work across them

Run headless WITH RHI (material compile).  Gender folder via GENDER below.
"""
import unreal
import os

GENDER = os.environ.get("ROSE_GENDER", "FEMALE").upper()   # FEMALE or MALE
PROJECT_DIR = unreal.Paths.project_dir()
MODULAR_DIR = os.path.normpath(os.path.join(
    PROJECT_DIR, "SourceAssets", "GLTF", "AVATAR", "MODULAR", GENDER))
GAME_ROOT = f"/Game/Characters/Modular/{GENDER.capitalize()}"

MAT_PATH = "/Game/Characters/Materials"
MASTER_NAME = "M_RoseChar"
ME = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary


def master():
    full = f"{MAT_PATH}/{MASTER_NAME}"
    if EAL.does_asset_exist(full):
        return EAL.load_asset(full)
    if not EAL.does_directory_exist(MAT_PATH):
        EAL.make_directory(MAT_PATH)
    m = AT.create_asset(MASTER_NAME, MAT_PATH, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("opacity_mask_clip_value", 0.5)
    t = ME.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    t.set_editor_property("parameter_name", "BaseColor")
    ME.connect_material_property(t, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    ME.connect_material_property(t, "A",   unreal.MaterialProperty.MP_OPACITY_MASK)
    ME.recompile_material(m)
    EAL.save_asset(full)
    return m


def _tex(mi):
    if isinstance(mi, unreal.MaterialInstanceConstant):
        tpv = mi.get_editor_property("texture_parameter_values")
        for p in tpv:
            n = str(p.get_editor_property("parameter_info").get_editor_property("name"))
            if n.lower().startswith("basecolor"):
                return p.get_editor_property("parameter_value")
        if tpv:
            return tpv[0].get_editor_property("parameter_value")
    return None


def import_glb(glb, dst):
    if EAL.does_directory_exist(dst):
        EAL.delete_directory(dst)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", glb)
    t.set_editor_property("destination_path", dst)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    AT.import_asset_tasks([t])
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
    sm = sk = None
    for a in EAL.list_assets(dst, recursive=True):
        o = EAL.load_asset(a)
        if not o:
            continue
        if o.get_class().get_name() == "SkeletalMesh":
            sm = o
        elif o.get_class().get_name() == "Skeleton":
            sk = o
    return sm, sk


def reskin(sm, mas):
    inst_dir = sm.get_path_name().rsplit("/SkeletalMeshes/", 1)[0] + "/Materials"
    if not EAL.does_directory_exist(inst_dir):
        EAL.make_directory(inst_dir)
    mats = sm.get_editor_property("materials")
    out = []
    for slot in mats:
        old = slot.get_editor_property("material_interface")
        nm = str(slot.get_editor_property("material_slot_name"))
        tex = _tex(old)
        ip = f"{inst_dir}/MI_{nm}"
        if EAL.does_asset_exist(ip):
            EAL.delete_asset(ip)
        inst = AT.create_asset(f"MI_{nm}", inst_dir, unreal.MaterialInstanceConstant,
                               unreal.MaterialInstanceConstantFactoryNew())
        ME.set_material_instance_parent(inst, mas)
        if tex:
            ME.set_material_instance_texture_parameter_value(inst, "BaseColor", tex)
        EAL.save_asset(ip)
        slot.set_editor_property("material_interface", inst)
        out.append(slot)
    sm.set_editor_property("materials", out)
    EAL.save_asset(sm.get_path_name())


# Optional: limit to specific item names (no extension) for a targeted re-import.
# Leave empty to import everything.  Set via env ROSE_ONLY="cap_2,cap_3".
ONLY = [x for x in os.environ.get("ROSE_ONLY", "").split(",") if x.strip()]

# Slot-wide filter.  ROSE_ONLY needs every name spelled out, which is impractical
# for a whole slot (face items alone are 338).  ROSE_PREFIX="faceitem_" imports
# just that slot; combine with ROSE_SKIP_EXISTING=1 + ROSE_LIMIT=<n> to walk a
# large slot in resumable chunks and stay clear of the GPU watchdog (these GLBs
# embed textures, so the import needs RHI and does compile shaders).
PREFIX = os.environ.get("ROSE_PREFIX", "").strip()


def run():
    mas = master()
    glbs = sorted(f for f in os.listdir(MODULAR_DIR) if f.lower().endswith(".glb"))
    if ONLY:
        keep = set(ONLY) | {"base"} if False else set(ONLY)
        glbs = [g for g in glbs if os.path.splitext(g)[0] in keep]
        print(f"[mod] ONLY filter -> {glbs}")
    if PREFIX:
        glbs = [g for g in glbs if g.lower().startswith(PREFIX.lower())]
        print(f"[mod] PREFIX '{PREFIX}' filter -> {len(glbs)} glbs")
    base_sk = None
    part_sks = []
    no_reskin = bool(os.environ.get("ROSE_NORESKIN", "").strip())
    skip_existing = bool(os.environ.get("ROSE_SKIP_EXISTING", "").strip())
    limit = int(os.environ.get("ROSE_LIMIT", "0") or 0)   # 0 = no limit
    done = skipped = 0
    for glb in glbs:
        name = os.path.splitext(glb)[0]
        dst = f"{GAME_ROOT}/{name}"
        # Resumable: skip items already imported (so a crashed batch can rerun).
        if skip_existing and name != "base" and \
                EAL.does_asset_exist(f"{dst}/{name}/SkeletalMeshes/{name}"):
            skipped += 1
            continue
        if limit and done >= limit:
            break
        sm, sk = import_glb(os.path.join(MODULAR_DIR, glb), dst)
        # The GLB embeds its textures, so Interchange creates textured materials
        # on import.  ROSE_NORESKIN keeps those (reliable) instead of the flaky
        # M_RoseChar re-skin.
        if sm and not no_reskin:
            reskin(sm, mas)
        if name == "base":
            base_sk = sk
        elif sk:
            part_sks.append(sk)
        done += 1
        if done % 25 == 0:
            print(f"[mod] imported {done} (skipped {skipped}) ... last={name}")
        unreal.SystemLibrary.collect_garbage()
    print(f"[mod] import phase: {done} imported, {skipped} skipped")

    # When doing a targeted re-import (ONLY), base wasn't re-imported — load its
    # existing skeleton so the new parts still get marked compatible.
    if base_sk is None:
        base_sk = EAL.load_asset(f"{GAME_ROOT}/base/base/SkeletalMeshes/base_Skeleton")

    # compatibility both directions (not needed for the merge, which matches by
    # bone name; only for the legacy leader-pose path).  ROSE_NOCOMPAT skips it.
    if os.environ.get("ROSE_NOCOMPAT", "").strip():
        print(f"[mod] skipped compat marking ({len(part_sks)} parts)")
        base_sk = None
    if base_sk:
        for sk in part_sks:
            try:
                base_sk.add_compatible_skeleton(sk)
                sk.add_compatible_skeleton(base_sk)
            except Exception as e:
                print(f"[mod] compat failed {sk.get_name()}: {e}")
        EAL.save_asset(base_sk.get_path_name())
        for sk in part_sks:
            EAL.save_asset(sk.get_path_name())
        print(f"[mod] marked {len(part_sks)} part skeletons compatible with base")
    print("[mod] done")


run()
