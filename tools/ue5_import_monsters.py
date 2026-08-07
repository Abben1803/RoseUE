"""
ue5_import_monsters.py — Import monster GLBs (SourceAssets/GLTF/NPC/npc_*.glb)
to /Game/Monsters/npc_<id>.

Each monster keeps ITS OWN skeleton (no merge, no compat marking needed — it
plays only its own CHR animations).  The GLBs embed their textures so
Interchange creates textured materials (same as ROSE_NORESKIN wardrobe path).

Run headless, editor CLOSED.  Env:
  ROSE_ONLY="npc_1,npc_2"   import only these
  ROSE_SKIP_EXISTING=1      resumable batch
  ROSE_LIMIT=N              stop after N imports
"""
import unreal
import os

PROJECT_DIR = unreal.Paths.project_dir()
GLB_DIR = os.path.normpath(os.path.join(PROJECT_DIR, "SourceAssets", "GLTF", "NPC"))

# The CURRENT npc folder — the one RoseMonster.cpp resolves against.  It used to
# be /Game/Monsters, which is Arua-era content the QQ-iROSE tables no longer
# agree with; keeping the import there meant the game had to reach across two
# eras to find a mesh.  Override with ROSE_NPC_ROOT if you need a scratch import
# that must not touch the live folder.
GAME_ROOT = os.environ.get("ROSE_NPC_ROOT", "/Game/Rose/Npcs")

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

ONLY = [x for x in os.environ.get("ROSE_ONLY", "").split(",") if x.strip()]


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


def run():
    glbs = sorted(f for f in os.listdir(GLB_DIR) if f.lower().endswith(".glb"))
    if ONLY:
        keep = set(ONLY)
        glbs = [g for g in glbs if os.path.splitext(g)[0] in keep]
        print(f"[mon] ONLY filter -> {glbs}")
    skip_existing = bool(os.environ.get("ROSE_SKIP_EXISTING", "").strip())
    limit = int(os.environ.get("ROSE_LIMIT", "0") or 0)
    done = skipped = 0
    for glb in glbs:
        name = os.path.splitext(glb)[0]
        dst = f"{GAME_ROOT}/{name}"
        if skip_existing and EAL.does_asset_exist(f"{dst}/{name}/SkeletalMeshes/{name}"):
            skipped += 1
            continue
        if limit and done >= limit:
            break
        import_glb(os.path.join(GLB_DIR, glb), dst)
        # Log what actually landed (mesh + anim assets) so the runtime paths
        # can be verified instead of guessed.
        assets = [a.split(".")[-1] for a in EAL.list_assets(dst, recursive=True)]
        anims = [a for a in assets if a.startswith(f"{name}_")]
        print(f"[mon] {name}: {len(assets)} assets, anims={sorted(anims)}")
        done += 1
        unreal.SystemLibrary.collect_garbage()
    print(f"[mon] done: {done} imported, {skipped} skipped")


run()
