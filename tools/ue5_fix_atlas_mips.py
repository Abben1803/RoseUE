"""ue5_fix_atlas_mips.py — disable mipmaps on the 32 gear atlas pages.

The atlases are 2048px pages holding a 16x16 grid of 128px cells.  UE's mip
generation downsamples the WHOLE page with no knowledge of cell boundaries, so
by mip 3-4 each cell is 16->8px and is averaging in its NEIGHBOURS.  That is the
coloured/black speckling along alpha edges on wings and every other atlas item
(e.g. the red cell beside back_wing12 bleeding into the wing).

This is NOT the same bug as the alpha-dilation one: dilation pads edges *inside*
a cell and cannot stop cross-cell bleed at lower mips.

TMGS_NO_MIPMAPS makes every sample come from mip 0, so a cell can only ever read
its own texels.  Trade-off: more aliasing at distance.  The alternative (cell
padding + a mip-count clamp) is a bigger pipeline change; do that only if the
aliasing is objectionable.

Run headless with the editor CLOSED:
  UnrealEditor-Cmd.exe <proj> -ExecutePythonScript=tools/ue5_fix_atlas_mips.py \
      -unattended -nopause -nosplash -stdout -FullStdOutLogOutput -nullrhi
"""
import unreal

EAL = unreal.EditorAssetLibrary
DST = "/Game/Characters/Gear/Atlas"

fixed = skipped = missing = 0
for page in range(32):
    path = f"{DST}/T_gear_atlas_{page:02d}"
    if not EAL.does_asset_exist(path):
        print(f"[atlasmip] MISSING {path}")
        missing += 1
        continue

    tex = EAL.load_asset(path)
    cur = tex.get_editor_property("mip_gen_settings")
    if cur == unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS:
        print(f"[atlasmip] T_gear_atlas_{page:02d}: already no-mips")
        skipped += 1
        continue

    tex.set_editor_property("mip_gen_settings",
                            unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    # Alpha must survive: the gear masters route it into OpacityMask.
    if tex.get_editor_property("compression_no_alpha"):
        tex.set_editor_property("compression_no_alpha", False)
    EAL.save_asset(path)
    print(f"[atlasmip] T_gear_atlas_{page:02d}: {cur} -> TMGS_NO_MIPMAPS")
    fixed += 1

print(f"[atlasmip] done: fixed={fixed} already={skipped} missing={missing}")
