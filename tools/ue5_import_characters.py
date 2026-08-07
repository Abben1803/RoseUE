"""
ue5_import_characters.py  —  Import ROSE avatar GLBs into UE5 via Interchange.

Run from UE5 editor:  Tools > Execute Python Script

IMPORTANT: GLB files go through UE5's Interchange/GLTFCore pipeline, NOT the FBX
pipeline.  Do NOT pass FbxImportUI options to GLB imports — it crashes the editor.

Strategy:
  1. Import the first female body GLB  → Interchange creates SK_Female + Skeleton
  2. Import the first male body GLB    → same for male
  3. Import remaining parts (automated=True, no options — Interchange will prompt
     "reuse existing skeleton?" for each batch; say Yes)
  4. Import animations the same way

For full automation (skip the skeleton reuse prompts) use the Interchange
pipeline override section below — requires UE5.3+.
"""
import unreal
import os

# ── paths ─────────────────────────────────────────────────────────────────────
_PROJECT_DIR = unreal.Paths.project_dir()
GLTF_ROOT = os.path.normpath(os.path.join(
    _PROJECT_DIR, "SourceAssets", "GLTF", "AVATAR"
))

AT = unreal.AssetToolsHelpers.get_asset_tools()


def _glbs_in(folder: str):
    if not os.path.isdir(folder):
        return []
    return sorted(
        os.path.join(folder, f) for f in os.listdir(folder)
        if f.lower().endswith(".glb")
    )


def _stem(path: str) -> str:
    return os.path.splitext(os.path.basename(path))[0]


def _make_task(src: str, dst_path: str, dst_name: str = "") -> unreal.AssetImportTask:
    """Create an import task for a GLB.  No options — let Interchange auto-detect."""
    task = unreal.AssetImportTask()
    task.set_editor_property("filename",         src)
    task.set_editor_property("destination_path", dst_path)
    if dst_name:
        task.set_editor_property("destination_name", dst_name)
    task.set_editor_property("automated",        True)
    task.set_editor_property("replace_existing", False)
    # Do NOT set task.options for GLB — FbxImportUI crashes the editor.
    return task


def _run(tasks):
    if tasks:
        AT.import_asset_tasks(tasks)


def _exists(ue_path: str, stem: str) -> bool:
    """True if an asset with this stem already lives at ue_path (resume support)."""
    return unreal.EditorAssetLibrary.does_asset_exist(f"{ue_path}/{stem}")


def _save_and_gc():
    """Flush imported packages to disk and release memory/GPU resources.

    Doing this per-batch is what keeps a long import from exhausting the GPU:
    each batch's render resources get freed before the next one allocates.
    A crash then costs only the current batch — re-running resumes via _exists().
    """
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(
        save_map_packages=False, save_content_packages=True)
    unreal.SystemLibrary.collect_garbage()


# ── Step 1: import one representative body per gender to establish skeletons ──

def import_skeletons():
    """Import the first body GLB for each gender.  This creates the Skeleton asset."""
    print("[1/3] Creating skeletons from first body mesh...")

    for gender, prefix, ue_path, dst_name in [
        ("FEMALE", "LIST_WBODY", "/Game/Characters/Female/Skeleton", "SK_Female_Base"),
        ("MALE",   "LIST_MBODY", "/Game/Characters/Male/Skeleton",   "SK_Male_Base"),
    ]:
        all_glbs = _glbs_in(os.path.join(GLTF_ROOT, gender))
        # Pick the first WBODY/MBODY file — body meshes are always skinned.
        # Avoid LIST_BACK and other accessories which may not have JOINTS_0 and
        # would import as StaticMesh rather than SkeletalMesh.
        skinned = [g for g in all_glbs if prefix in os.path.basename(g).upper()]
        if not skinned:
            skinned = [g for g in all_glbs
                       if any(p in os.path.basename(g).upper()
                              for p in ("WBODY","MBODY","WARMS","MARMS","WFOOT","MFOOT"))]
        if not skinned:
            print(f"  WARNING: no skinned GLBs found in GLTF/{gender}/")
            continue

        seed = skinned[0]
        if _exists(ue_path, _stem(seed)) or _exists(ue_path, dst_name):
            print(f"  {gender}: skeleton already present, skipping")
            continue
        print(f"  {gender}: importing {os.path.basename(seed)}")
        _run([_make_task(seed, ue_path, dst_name)])

    print("  Skeletons done.  Check /Game/Characters/*/Skeleton/ before continuing.")


# ── Step 2: import remaining body parts ───────────────────────────────────────

def import_parts(batch_size: int = 8):
    """
    Import all body-part GLBs in small batches.  Resumable: already-imported
    assets are skipped, and each batch is saved + GC'd before the next starts.
    Re-run after any crash to continue where it left off.
    """
    print("[2/3] Importing mesh parts...")

    for gender, ue_path in [
        ("FEMALE", "/Game/Characters/Female/Parts"),
        ("MALE",   "/Game/Characters/Male/Parts"),
    ]:
        glbs = [g for g in _glbs_in(os.path.join(GLTF_ROOT, gender))
                if _stem(g) != "skeleton"]
        # Drop ones already in the Content Browser (resume)
        pending = [g for g in glbs if not _exists(ue_path, _stem(g))]
        skipped = len(glbs) - len(pending)
        total = len(pending)
        print(f"  {gender}: {total} to import, {skipped} already present")

        for i in range(0, total, batch_size):
            batch = pending[i : i + batch_size]
            _run([_make_task(g, ue_path) for g in batch])
            _save_and_gc()
            print(f"  {gender}: {min(i + batch_size, total)}/{total}")

    print("  Parts done.")


# ── Step 3: import animations ─────────────────────────────────────────────────

def import_animations():
    """Import the two combined animation GLBs (FEMALE_anims.glb / MALE_anims.glb).

    Each bundles the gender skeleton + a reference body mesh + every avatar
    animation for that gender.  Importing one file yields a shared Skeleton plus
    all AnimSequences linked to it — no per-file pipeline config, and it dodges
    the "nothing to import" failure that animation-only GLBs hit.
    """
    print("[3/3] Importing combined animation rigs...")
    combined_dir = os.path.join(GLTF_ROOT, "ANIM_COMBINED")

    for gender in ("FEMALE", "MALE"):
        glb = os.path.join(combined_dir, f"{gender}_anims.glb")
        if not os.path.isfile(glb):
            print(f"  WARNING: {glb} not found — run build_combined_anims.py first")
            continue
        ue_path = f"/Game/Characters/{gender.capitalize()}/AnimRig"
        print(f"  {gender}: importing {os.path.basename(glb)}")
        _run([_make_task(glb, ue_path)])
        _save_and_gc()

    print("  Animations done.  Find them under /Game/Characters/*/AnimRig/")


# ── wipe (for a clean re-import) ──────────────────────────────────────────────

def wipe_characters(include_animations: bool = False):
    """Delete previously-imported character assets so the next run re-imports
    from scratch.  A GPU crash can leave half-written assets; wiping first gives
    a clean slate.  Skeletons + parts always wiped; animations only if asked."""
    print("[wipe] Deleting existing character assets...")
    dirs = [
        "/Game/Characters/Female/Skeleton",
        "/Game/Characters/Female/Parts",
        "/Game/Characters/Male/Skeleton",
        "/Game/Characters/Male/Parts",
    ]
    if include_animations:
        dirs += [
            "/Game/Characters/Female/AnimRig",
            "/Game/Characters/Male/AnimRig",
        ]
    for d in dirs:
        if unreal.EditorAssetLibrary.does_directory_exist(d):
            unreal.EditorAssetLibrary.delete_directory(d)
            print(f"  deleted {d}")
    unreal.SystemLibrary.collect_garbage()
    print("  wipe done.")


# ── run all steps ─────────────────────────────────────────────────────────────

def run_all():
    import_skeletons()
    import_parts()
    import_animations()
    print("\nAll done!  Check /Game/Characters/ in the Content Browser.")


# Re-import ONLY the AnimRig (now a full-body reference: body+face+hair+foot on
# the shared skeleton with all animations).  Parts/skeletons are already correct.
for _d in ("/Game/Characters/Female/AnimRig", "/Game/Characters/Male/AnimRig"):
    if unreal.EditorAssetLibrary.does_directory_exist(_d):
        unreal.EditorAssetLibrary.delete_directory(_d)
        print(f"[wipe] deleted {_d}")
unreal.SystemLibrary.collect_garbage()
import_animations()
print("\nAnimRig re-import complete. Open /Game/Characters/Female/AnimRig/FEMALE_anims/")
