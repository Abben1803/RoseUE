"""
gen_subwpn_manifest.py

Reads LIST_SUBWPN.ZSC and exports subweapon part/material metadata
for Unreal MaterialInstance processing.

Output:
    tools/_tmp/body_manifest.json
    tools/_tmp/cap_manifest.json
    tools/_tmp/arms_manifest.json
    tools/_tmp/feet_manifest.json

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


def warms():
    zsc_path = resolve(
        "3DDATA/AVATAR/LIST_WARMS.ZSC"
    )

    if not zsc_path:
        raise FileNotFoundError(
            "LIST_WARMS.ZSC not found"
        )
    print(f"Reading: {zsc_path}")

    zsc = read_zsc(zsc_path)

    manifest = {
        "source": "LIST_WARMS.ZSC",
        "warms": {}
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
            manifest["warms"][str(wid)] = rows

        if wid % 100 == 0:
            print(
                f"processed warms: {wid}"
            )
    out = os.path.join(
        OUT_DIR,
        "warms_manifest.json"
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

def marms():
    zsc_path = resolve(
        "3DDATA/AVATAR/LIST_MARMS.ZSC"
    )

    if not zsc_path:
        raise FileNotFoundError(
            "LIST_MARMS.ZSC not found"
        )


    print(f"Reading: {zsc_path}")

    zsc = read_zsc(zsc_path)

    manifest = {
        "source": "LIST_MARMS.ZSC",
        "marms": {}
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
            manifest["marms"][str(wid)] = rows

        if wid % 100 == 0:
            print(
                f"processed marms: {wid}"
            )
    out = os.path.join(
        OUT_DIR,
        "marms_manifest.json"
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

def mfeet():
    zsc_path = resolve(
        "3DDATA/AVATAR/LIST_MFOOT.ZSC"
    )

    if not zsc_path:
        raise FileNotFoundError(
            "LIST_MFOOT.ZSC not found"
        )
    
    print(f"Reading: {zsc_path}")

    zsc = read_zsc(zsc_path)

    manifest = {
        "source": "LIST_MFOOT.ZSC",
        "mfoot": {}
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
            manifest["mfoot"][str(wid)] = rows

        if wid % 100 == 0:
            print(
                f"processed mfoot: {wid}"
            )
    out = os.path.join(
        OUT_DIR,
        "mfoot_manifest.json"
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


def wfeet():
    zsc_path = resolve(
        "3DDATA/AVATAR/LIST_WFOOT.ZSC"
    )

    if not zsc_path:
        raise FileNotFoundError(
            "LIST_WFOOT.ZSC not found"
        )

    print(f"Reading: {zsc_path}")

    zsc = read_zsc(zsc_path)

    manifest = {
        "source": "LIST_WFOOT.ZSC",
        "wfoot": {}
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
            manifest["wfoot"][str(wid)] = rows

        if wid % 100 == 0:
            print(
                f"processed wfoot: {wid}"
            )
    out = os.path.join(
        OUT_DIR,
        "wfoot_manifest.json"
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

def mcap():
    zsc_path = resolve(
        "3DDATA/AVATAR/LIST_MCAP.ZSC"
    )

    if not zsc_path:
        raise FileNotFoundError(
            "LIST_MCAP.ZSC not found"
        )

    print(f"Reading: {zsc_path}")

    zsc = read_zsc(zsc_path)

    manifest = {
        "source": "LIST_MCAP.ZSC",
        "mcap": {}
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
            manifest["mcap"][str(wid)] = rows

        if wid % 100 == 0:
            print(
                f"processed mcap: {wid}"
            )

    out = os.path.join(
        OUT_DIR,
        "mcap_manifest.json"
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


def wcap():
    zsc_path = resolve(
        "3DDATA/AVATAR/LIST_WCAP.ZSC"
    )

    if not zsc_path:
        raise FileNotFoundError(
            "LIST_WCAP.ZSC not found"
        )

    print(f"Reading: {zsc_path}")

    zsc = read_zsc(zsc_path)

    manifest = {
        "source": "LIST_WCAP.ZSC",
        "wcap": {}
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
            manifest["wcap"][str(wid)] = rows

        if wid % 100 == 0:
            print(
                f"processed wcap: {wid}"
            )

    out = os.path.join(
        OUT_DIR,
        "wcap_manifest.json"
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


    

def wbody():
    zsc_path = resolve(
        "3DDATA/AVATAR/LIST_WBODY.ZSC"
    )

    if not zsc_path:
        raise FileNotFoundError(
            "LIST_WBODY.ZSC not found"
        )

    print(f"Reading: {zsc_path}")

    zsc = read_zsc(zsc_path)

    manifest = {
        "source": "LIST_WBODY.ZSC",
        "wbody": {}
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
            manifest["wbody"][str(wid)] = rows

        if wid % 100 == 0:
            print(
                f"processed wbody: {wid}"
            )

    out = os.path.join(
        OUT_DIR,
        "wbody_manifest.json"
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


def mbody():
    zsc_path = resolve(
        "3DDATA/AVATAR/LIST_MBODY.ZSC"
    )

    if not zsc_path:
        raise FileNotFoundError(
            "LIST_MBODY.ZSC not found"
        )

    print(f"Reading: {zsc_path}")

    zsc = read_zsc(zsc_path)

    manifest = {
        "source": "LIST_MBODY.ZSC",
        "mbody": {}
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
            manifest["mbody"][str(wid)] = rows

        if wid % 100 == 0:
            print(
                f"processed mbody: {wid}"
            )

    out = os.path.join(
        OUT_DIR,
        "mbody_manifest.json"
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

def main():
    wcap()
    mcap()
    wbody()
    mbody()
    warms()
    marms()
    wfeet()
    mfeet()

if __name__ == "__main__":
    main()