"""
ue5_setup_materials.py — Create a proper ROSE character master material and
re-skin the AnimRig sections with instances of it.

ROSE characters render two-sided with alpha test (masked).  Interchange's
auto-generated M_GLTF material doesn't replicate that, which causes parts to
backface-cull (disappear) at grazing angles.  This builds one master material
(M_RoseChar: masked + two-sided) and points each section at it, reusing the
textures Interchange already imported.

Run headless after the avatar import.
"""
import unreal

MAT_PATH = "/Game/Characters/Materials"
MASTER_NAME = "M_RoseChar"
SKELETAL_MESHES = [
    "/Game/Characters/Female/AnimRig/FEMALE_anims/SkeletalMeshes/FEMALE_anims",
    "/Game/Characters/Male/AnimRig/MALE_anims/SkeletalMeshes/MALE_anims",
]

ME = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary


def get_or_create_master():
    full = f"{MAT_PATH}/{MASTER_NAME}"
    if EAL.does_asset_exist(full):
        return EAL.load_asset(full)

    if not EAL.does_directory_exist(MAT_PATH):
        EAL.make_directory(MAT_PATH)

    mat = AT.create_asset(MASTER_NAME, MAT_PATH, unreal.Material,
                          unreal.MaterialFactoryNew())
    # ROSE render state
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("opacity_mask_clip_value", 0.5)

    # Texture sample parameter "BaseColor"
    tex = ME.create_material_expression(
        mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    tex.set_editor_property("parameter_name", "BaseColor")

    ME.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    ME.connect_material_property(tex, "A",   unreal.MaterialProperty.MP_OPACITY_MASK)

    ME.recompile_material(mat)
    EAL.save_asset(full)
    print(f"[mat] created master {full}")
    return mat


def _section_texture(material_interface):
    """Pull the BaseColorTexture from an existing material instance."""
    if isinstance(material_interface, unreal.MaterialInstanceConstant):
        for p in material_interface.get_editor_property("texture_parameter_values"):
            name = p.get_editor_property("parameter_info").get_editor_property("name")
            if str(name).lower().startswith("basecolor"):
                return p.get_editor_property("parameter_value")
        # fallback: first texture param
        tpv = material_interface.get_editor_property("texture_parameter_values")
        if tpv:
            return tpv[0].get_editor_property("parameter_value")
    return None


def run():
    master = get_or_create_master()

    for sm_path in SKELETAL_MESHES:
        sm = EAL.load_asset(sm_path)
        if not sm:
            print(f"[mat] SKIP (missing): {sm_path}")
            continue
        inst_dir = sm_path.rsplit("/SkeletalMeshes/", 1)[0] + "/Materials"
        if not EAL.does_directory_exist(inst_dir):
            EAL.make_directory(inst_dir)

        mats = sm.get_editor_property("materials")
        new_mats = []
        for i, slot in enumerate(mats):
            old = slot.get_editor_property("material_interface")
            slot_name = str(slot.get_editor_property("material_slot_name"))
            tex = _section_texture(old)

            inst_name = f"MI_{slot_name}"
            inst_path = f"{inst_dir}/{inst_name}"
            if EAL.does_asset_exist(inst_path):
                EAL.delete_asset(inst_path)
            inst = AT.create_asset(inst_name, inst_dir, unreal.MaterialInstanceConstant,
                                   unreal.MaterialInstanceConstantFactoryNew())
            ME.set_material_instance_parent(inst, master)
            if tex:
                ME.set_material_instance_texture_parameter_value(inst, "BaseColor", tex)
            EAL.save_asset(inst_path)

            slot.set_editor_property("material_interface", inst)
            new_mats.append(slot)
            print(f"[mat] {slot_name}: tex={tex.get_name() if tex else None}")

        sm.set_editor_property("materials", new_mats)
        EAL.save_asset(sm_path)
        print(f"[mat] re-skinned {sm_path} ({len(mats)} slots)")

    print("[mat] done")


run()
