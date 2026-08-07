#!/usr/bin/env python3
"""build_effects.py — convert ROSE skill effects (.EFT + .PTL) to the UE runtime's
JSON + PNG textures.  FAITHFUL data: emitter parameters, keyframe events, blend
modes and sprite-sheet layouts are carried through 1:1 (parsers validated
against zz_interface.cpp loadEffect / zz_particle_event_sequence::Load).

Outputs:
  RoseUE/Effects/eft_<id>.json                 one per FILE_EFFECT.STB row used
  RoseUE/SourceAssets/GLTF/EFFECTS/<tex>.png   particle textures (DDS→PNG)

By default converts only the effects referenced by skills.csv (CastEffect /
HitEffect); set ROSE_ALL=1 to convert every FILE_EFFECT row.

Then: run tools/ue5_import_effect_textures.py (headless) to import the PNGs,
and tools/ue5_make_fx_assets.py to (re)create the particle materials.

Usage:  py -3.9 build_effects.py
"""
import csv
import json
import os
import re
import sys

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb        # noqa: E402
from rose_parser.formats import eft as eft_mod                # noqa: E402
from rose_parser.formats import ptl as ptl_mod                # noqa: E402

ROOT = r"C:/rose-next-classic/unreal-engine rose/RoseUE"

# Asset SOURCE and OUTPUT roots are deliberately separate.  ROOT used to supply
# both, which quietly pinned the source to the deprecated classic tree
# (SourceAssets/3DDATA); FILE_EFFECT.STB is 3713 rows in Arua vs 3162 in classic
# and row ids do not line up, so effects resolved to the wrong entries.
# Assets come from Arua (CLAUDE.md); generated files still go under ROOT.
DATA = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
OUT_JSON = ROOT + "/Effects"
OUT_TEX = ROOT + "/SourceAssets/GLTF/EFFECTS"
SKILLS_CSV = ROOT + "/DataTables/skills.csv"


def build_file_index():
    """lowercase relative path -> absolute path, for case-insensitive resolve."""
    index = {}
    for dirpath, _dirs, files in os.walk(DATA):
        rel = os.path.relpath(dirpath, DATA).replace("\\", "/")
        for f in files:
            key = (rel + "/" + f).lower().lstrip("./")
            index[key] = os.path.join(dirpath, f)
    return index


def resolve(index, rose_path):
    """'3DData\\Effect\\x.ptl' (any case) -> absolute path or None."""
    p = rose_path.replace("\\", "/").strip().strip('"').lower()
    p = re.sub(r"/+", "/", p)          # PTL strings carry doubled backslashes
    p = re.sub(r"^3ddata/", "", p)
    return index.get(p)


def tex_key(rose_path):
    """Texture asset name from its ROSE path (stem, sanitized)."""
    stem = os.path.splitext(os.path.basename(rose_path.replace("\\", "/")))[0]
    return re.sub(r"[^a-z0-9_]", "_", stem.lower())


def convert_texture(index, rose_path, done):
    key = tex_key(rose_path)
    if key in done:
        return key
    src = resolve(index, rose_path)
    out = os.path.join(OUT_TEX, key + ".png")
    if not src:
        print(f"[effects]   MISSING texture {rose_path}")
        done[key] = False
        return key
    if not os.path.isfile(out):
        try:
            from PIL import Image
            Image.open(src).convert("RGBA").save(out)
        except Exception as ex:
            print(f"[effects]   texture convert FAILED {rose_path}: {ex}")
            done[key] = False
            return key
    done[key] = True
    return key


def seq_to_json(s, index, tex_done):
    return {
        "name": s.name.strip('"'),
        "life": [s.life_min, s.life_max],
        "emit_rate": [s.emit_rate_min, s.emit_rate_max],
        "loops": s.loops,
        "spawn_dir": [list(s.spawn_dir_min), list(s.spawn_dir_max)],     # cm/s
        "emit_radius": [list(s.emit_radius_min), list(s.emit_radius_max)],  # cm
        "gravity": [list(s.gravity_min), list(s.gravity_max)],           # cm/s^2
        "texture": convert_texture(index, s.texture, tex_done),
        "num_particles": s.num_particles,
        "align": s.align_type,           # 0 billboard / 1 world / 2 z-axis
        "coord": s.update_coord,         # 0 world / 1 local-world / 2 local
        "grid": [s.tex_w, s.tex_h],
        "blend_dst": s.dst_blend,        # D3DBLEND: 2=ONE (additive), 6=INVSRCALPHA
        "blend_src": s.src_blend,
        "events": [
            {"type": e.type, "t0": e.time_min, "t1": e.time_max,
             "fade": 1 if e.fade else 0, "v": e.values}
            for e in s.events
        ],
    }


def convert_effect(index, effect_id, eft_abs, tex_done):
    e = eft_mod.parse(eft_abs)
    out = {"id": effect_id,
           "name": os.path.splitext(os.path.basename(eft_abs))[0],
           "particles": [], "mesh_count": len(e.meshes)}
    for p in e.particles:
        ptl_abs = resolve(index, p.ptl_path)
        if not ptl_abs:
            print(f"[effects]   MISSING ptl {p.ptl_path}")
            continue
        ptl = ptl_mod.parse(ptl_abs)
        out["particles"].append({
            "ptl": os.path.splitext(os.path.basename(ptl_abs))[0],
            # EFT offsets are engine METRES (loadEffect passes them raw) → cm.
            "pos_cm": [p.position[0] * 100.0, p.position[1] * 100.0, p.position[2] * 100.0],
            "rot_deg": list(p.rotation_deg),   # D3D pitch/yaw/roll degrees
            "delay_ms": p.delay_ms,
            "link": p.is_link,
            "sequences": [seq_to_json(s, index, tex_done) for s in ptl.sequences],
        })
    path = os.path.join(OUT_JSON, f"eft_{effect_id}.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(out, f, separators=(",", ":"))
    return len(out["particles"])


def used_effect_ids(list_effect):
    """FILE_EFFECT rows referenced by skills (CastEffect/HitEffect) or by
    LIST_EFFECT weapon-effect rows (hit + bullet, normal + critical)."""
    ids = set()
    with open(SKILLS_CSV, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            for col in ("CastEffect", "HitEffect"):
                v = int(row.get(col) or 0)
                if v > 0:
                    ids.add(v)
    for i in range(1, list_effect.num_rows()):
        for col in (9, 10, 11, 12):   # EFFECT_HITTED_* / EFFECT_BULLET_*
            v = list_effect.get_int(i, col)
            if v > 0:
                ids.add(v)
    # Status visuals (LIST_STATUS STATE_STEP_EFFECT via status_effects.json) —
    # the buff/debuff on-character loops.
    try:
        sfx = json.load(open(ROOT + "/Content/DataTables/status_effects.json"))
        ids |= {v["fx"] for v in sfx.values() if v.get("fx", 0) > 0}
    except (OSError, ValueError) as e:
        print(f"[fx] status_effects.json unavailable ({e}) — status fx skipped")
    return ids


def write_weapon_effect_header(list_effect):
    """RoseWeaponEffectData.h — LIST_EFFECT.STB as a static C++ table
    (stb.h:322-342): per row the hit/bullet FILE_EFFECT ids (normal+critical),
    bullet speed (cm/s) and fire/hit FILE_SOUND rows."""
    out = os.path.join(
        r"C:/rose-next-classic/unreal-engine rose/RoseUE/Source/RoseUE",
        "RoseWeaponEffectData.h")
    n = list_effect.num_rows()
    lines = [
        "// Generated by tools/build_effects.py from LIST_EFFECT.STB — do not edit.",
        "// Weapon-effect rows (WEAPON_BULLET_EFFECT / NPC hand effects point here):",
        "// hit + bullet FILE_EFFECT ids, bullet speed (cm/s), fire/hit FILE_SOUND.",
        "#pragma once",
        "#include \"CoreMinimal.h\"",
        "",
        "struct FRoseWeaponEffectRow",
        "{",
        "\tint32 HitNormal, HitCritical, BulletNormal, BulletCritical;",
        "\tint32 BulletSpeed, FireSound, HitSound;",
        "};",
        "",
        f"static const FRoseWeaponEffectRow RoseWeaponEffects[{n}] = {{",
    ]
    for i in range(n):
        g = list_effect.get_int
        lines.append("\t{%d, %d, %d, %d, %d, %d, %d}," %
                     (g(i, 9), g(i, 10), g(i, 11), g(i, 12), g(i, 15), g(i, 16), g(i, 17)))
    lines += [
        "};",
        "",
        "static const FRoseWeaponEffectRow* RoseGetWeaponEffect(int32 Row)",
        "{",
        f"\treturn (Row > 0 && Row < {n}) ? &RoseWeaponEffects[Row] : nullptr;",
        "}",
        "",
    ]
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"[effects] RoseWeaponEffectData.h: {n} rows")


def main():
    os.makedirs(OUT_JSON, exist_ok=True)
    os.makedirs(OUT_TEX, exist_ok=True)

    index = build_file_index()
    stb = parse_stb(os.path.join(DATA, "STB", "FILE_EFFECT.STB"))
    list_effect = parse_stb(os.path.join(DATA, "STB", "LIST_EFFECT.STB"))

    write_weapon_effect_header(list_effect)

    wanted = None if os.environ.get("ROSE_ALL") == "1" else used_effect_ids(list_effect)
    tex_done = {}
    n_ok = n_missing = n_fail = 0
    for i in range(1, stb.num_rows()):
        if wanted is not None and i not in wanted:
            continue
        path = stb.get(i, 1).strip()
        if not path:
            continue
        eft_abs = resolve(index, path)
        if not eft_abs:
            n_missing += 1
            continue
        try:
            convert_effect(index, i, eft_abs, tex_done)
            n_ok += 1
        except Exception as ex:
            print(f"[effects] FAIL id {i} ({path}): {ex}")
            n_fail += 1

    n_tex = sum(1 for ok in tex_done.values() if ok)
    print(f"[effects] {n_ok} effects converted ({n_missing} missing files, "
          f"{n_fail} parse failures), {n_tex} textures -> {OUT_TEX}")
    print(f"[effects] JSON -> {OUT_JSON}")


if __name__ == "__main__":
    main()
