"""
gen_subwpn_manifest.py

Reads LIST_SUBWPN.ZSC and exports subweapon part/material metadata
for Unreal MaterialInstance processing.

Output:
    tools/_tmp/subwpn_manifest.json

Env:
    MAPFORGE_ASSET_ROOT = client 3DDATA root
"""

import os
import sys
import json

TOOLS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(TOOLS)

MAPFORGE = os.path.join(REPO, "mapforge")
sys.path.insert(0, MAPFORGE)
sys.path.insert(0, REPO)

import config
from rose_zsc import read_zsc


OUT_DIR = os.path.join(TOOLS, "_tmp")
os.makedirs(OUT_DIR, exist_ok=True)


def resolve(rel):
    parts = [
        p for p in rel.replace("\\", "/").split("/")
        if p
    ]

    if parts and parts[0].lower() == "3ddata":
        parts = parts[1:]

    cur = config.ASSET_ROOT

    for p in parts:
        if not os.path.isdir(cur):
            return None

        match = [
            e for e in os.listdir(cur)
            if e.lower() == p.lower()
        ]

        if not match:
            return None

        cur = os.path.join(cur, match[0])

    return cur if os.path.exists(cur) else None


def main():

    zsc_path = resolve(
        "3DDATA/AVATAR/LIST_BACK.ZSC"
    )

    if not zsc_path:
        raise FileNotFoundError(
            "LIST_BACK.ZSC not found"
        )

    print(f"Reading: {zsc_path}")

    zsc = read_zsc(zsc_path)

    manifest = {
        "source": "LIST_BACK.ZSC",
        "back": {}
    }

    total_parts = 0
    total_materials = 0

    for wid, model in enumerate(zsc.models):

        rows = []

        for slot, part in enumerate(model.parts):

            if not (
                0 <= part.mat_idx < len(zsc.materials)
            ):
                continue

            mat = zsc.materials[part.mat_idx]

            mesh = None

            if 0 <= part.mesh_idx < len(zsc.meshes):
                mesh = zsc.meshes[part.mesh_idx]

            rows.append({
                "slot": slot,

                "mesh": mesh,

                "mesh_idx": part.mesh_idx,

                "material_idx": part.mat_idx,

                "flags": {
                    "alpha": bool(mat.is_alpha),
                    "alpha_test": bool(mat.alpha_test),
                    "alpha_ref": int(mat.alpha_ref),
                    "two_sided": bool(mat.is_two_side),
                    "blend_type": int(mat.blend_type)
                }
            })

            total_parts += 1
            total_materials += 1

        if rows:
            manifest["back"][str(wid)] = rows

        if wid % 100 == 0:
            print(
                f"processed back: {wid}"
            )

    out = os.path.join(
        OUT_DIR,
        "back_manifest.json"
    )

    with open(out, "w") as f:
        json.dump(
            manifest,
            f,
            indent=2
        )

    print(
        f"Done: {total_parts} parts, "
        f"{total_materials} materials"
    )

    print(out)


if __name__ == "__main__":
    main()