"""
gen_zsc_part_manifest.py — Map each imported map-scene actor back to its ZSC
part, carrying the ZSC data the Interchange import drops: per-part collision
flags (zz_collision_level) and constant-animation ZMO paths.

Replays export_map.py's object-node emission order (OBJECT lump, CNST lump,
then MORPH, per tile, tiles sorted) against the same client data, and
VALIDATES every replayed world matrix against the actual GLB node matrix —
so the node-index → part mapping is proven, not assumed.

Outputs (per zone):
  tools/_tmp/zsc_parts_<ZONE>.json     actionable actors: collision + anim
  <UE Content>/MapAnims/<ZONE>.json    runtime data for RoseMapAnimManager
                                       (only when the zone has node anims)

Env:
  MAPFORGE_ASSET_ROOT  client 3DDATA root the zone was exported from
  ROSE_ZONES           comma list of UE zone folder names (e.g. JPT01,JD01)

Run:  py -3.14 tools/gen_zsc_part_manifest.py
"""
import os
import sys
import json
import struct

import numpy as np

TOOLS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(TOOLS)
MAPFORGE = os.path.join(REPO, "mapforge")
sys.path.insert(0, MAPFORGE)
sys.path.insert(0, REPO)

import config                      # noqa: E402 (reads MAPFORGE_ASSET_ROOT)
import zone as Z                   # noqa: E402
import rose_map                    # noqa: E402
import rose_ifo as RI              # noqa: E402
from rose_zsc import read_zsc, COL_NOCAMERA, COL_HEIGHTONLY   # noqa: E402
from rose_zmo import read_zmo, CT_POSITION, CT_ROTATION, CT_SCALE  # noqa: E402
from export_map import compose     # noqa: E402  (same math as the exporter)

EXPORTS = os.path.join(MAPFORGE, "exports")
CONTENT = os.path.join(REPO, "unreal-engine rose", "RoseUE", "Content", "MapAnims")
OUT_DIR = os.path.join(TOOLS, "_tmp")
os.makedirs(OUT_DIR, exist_ok=True)
os.makedirs(CONTENT, exist_ok=True)

ZONES = [z.strip() for z in os.environ.get("ROSE_ZONES", "").split(",") if z.strip()]


def resolve(rel):
    parts = [p for p in rel.replace("\\", "/").split("/") if p]
    if parts and parts[0].lower() == "3ddata":
        parts = parts[1:]
    cur = config.ASSET_ROOT
    for p in parts:
        if not os.path.isdir(cur):
            return None
        m = [e for e in os.listdir(cur) if e.lower() == p.lower()]
        if not m:
            return None
        cur = os.path.join(cur, m[0])
    return cur if os.path.exists(cur) else None


def read_glb_nodes(path):
    with open(path, "rb") as f:
        data = f.read()
    jlen = struct.unpack_from("<II", data, 12)[0]
    g = json.loads(data[20:20 + jlen])
    nodes = g["nodes"]
    objects_children = None
    for nd in nodes:
        if nd.get("name") == "Objects":
            objects_children = nd.get("children", [])
            break
    return nodes, objects_children or []


def part_matrix(p):
    r = p.rotate   # (w, x, y, z)
    return compose(p.position, (r[1], r[2], r[3], r[0]), p.scale)


def replay_zone(zkey):
    """Reproduce export_map's Objects-group emission. Returns list of dicts,
    one per emitted node, in order."""
    z = Z.find_zone(zkey)
    if not z:
        raise KeyError(f"zone {zkey} not in LIST_ZONE under {config.ASSET_ROOT}")
    packs = {}
    for kind, rel in (("OBJECT", z["deco_pack"]), ("CNST", z["cnst_pack"])):
        ab = resolve(rel) if rel else None
        packs[kind] = read_zsc(ab) if ab else None

    morph_rows = []
    mstb = os.path.join(config.STB_DIR, "LIST_MORPH_OBJECT.STB")
    if os.path.exists(mstb):
        from parse_stb import StbFile
        stb = StbFile(mstb)
        for r in range(stb.row_count):
            mesh = stb.get(r, 1) if stb.col_count > 1 else ""
            morph_rows.append({"mesh": mesh} if mesh else None)

    resolve_cache = {}

    def mesh_ok(rel):
        if rel not in resolve_cache:
            resolve_cache[rel] = resolve(rel) is not None
        return resolve_cache[rel]

    out = []
    for x, y, stem in Z._tiles_in(z["dir"]):
        ifo = RI.read_ifo(stem + ".IFO")
        for kind in ("OBJECT", "CNST"):
            lump = ifo.lumps.get(RI.LUMP_OBJECT if kind == "OBJECT" else RI.LUMP_CNST)
            pack = packs.get(kind)
            if not lump or not pack:
                continue
            for o in lump.objects:
                if not (0 <= o.object_id < len(pack.models)):
                    continue
                parts = pack.models[o.object_id].parts
                ifoM = compose(o.pos, o.rot, o.scale)
                world = [None] * len(parts)
                for i, part in enumerate(parts):
                    mesh = pack.meshes[part.mesh_idx] if 0 <= part.mesh_idx < len(pack.meshes) else None
                    local = part_matrix(part)
                    if part.parent < 0 or part.parent >= i or world[part.parent] is None:
                        parentM = ifoM
                    else:
                        parentM = world[part.parent]
                    world[i] = parentM @ local
                    if not mesh:
                        continue
                    if not mesh_ok(mesh):
                        continue
                    out.append({
                        "kind": kind, "object_id": o.object_id, "part": i,
                        "mesh": mesh,
                        "world": world[i], "parent_world": parentM,
                        "pos": list(part.position),
                        "rot": list(part.rotate),          # (w,x,y,z)
                        "scl": list(part.scale),
                        "collision": part.collision,
                        "anim": part.anim_path or "",
                    })
        ml = ifo.lumps.get(RI.LUMP_MORPH)
        if ml and morph_rows:
            for o in ml.objects:
                if not (0 <= o.object_id < len(morph_rows)):
                    continue
                row = morph_rows[o.object_id]
                if not row or not mesh_ok(row["mesh"]):
                    continue
                out.append({"kind": "MORPH", "object_id": o.object_id, "part": 0,
                            "mesh": row["mesh"],
                            "world": compose(o.pos, o.rot, o.scale),
                            "parent_world": None, "collision": 0, "anim": ""})
    return out


def zmo_json(path_rel):
    """Node-anim ZMO -> dict, or None if it is a vertex-morph clip."""
    ab = resolve(path_rel)
    if not ab:
        return "missing"
    zmo = read_zmo(ab)
    pos = rot = scale = None
    for c in zmo.channels:
        if c.refer_id != 0:
            return "vertexmorph"       # per-vertex clip (flags/banners)
        if c.ctype == CT_POSITION:
            pos = [list(f) for f in c.frames]
        elif c.ctype == CT_ROTATION:
            rot = [list(f) for f in c.frames]   # (w,x,y,z)
        elif c.ctype == CT_SCALE:
            scale = [float(f) for f in c.frames]
        # ALPHA/UV/TEXTUREANIM channels are ignored for node motion
    if pos is None and rot is None and scale is None:
        return "vertexmorph"
    return {"fps": zmo.fps, "frames": zmo.num_frames,
            "pos": pos, "rot": rot, "scale": scale}


def main():
    for zone in ZONES:
        glb_candidates = [os.path.join(EXPORTS, f"{zone}.glb")]
        # JPT01 used to be the PROMOTED modern-client Zant, so JPT01V2.glb was
        # preferred here.  JPT01 is now rebuilt from Arua's own JPT01.glb, and
        # preferring V2 binds the manifest's expected actor locations to a
        # DIFFERENT export — every actor then fails the location check in
        # ue5_apply_zsc_part_flags.py and is silently skipped (no anims, no
        # collision flags).  Opt in explicitly if you really want V2.
        if zone.upper() == "JPT01" and os.environ.get("ROSE_USE_JPT01V2") == "1":
            glb_candidates.insert(0, os.path.join(EXPORTS, "JPT01V2.glb"))
        try:
            replay = replay_zone(zone)
        except KeyError as e:
            print(f"[{zone}] SKIP: {e}")
            continue

        matched = None
        for glb in glb_candidates:
            if not os.path.exists(glb):
                continue
            nodes, children = read_glb_nodes(glb)
            if len(children) != len(replay):
                print(f"[{zone}] {os.path.basename(glb)}: node count mismatch "
                      f"glb={len(children)} replay={len(replay)}")
                continue
            worst = 0.0
            ok = True
            for ci, rp in zip(children, replay):
                nm = nodes[ci].get("matrix")
                if nm is None:
                    ok = False
                    break
                gm = np.array(nm, dtype=np.float64).reshape(4, 4, order="F")
                d = float(np.abs(gm - rp["world"]).max())
                worst = max(worst, d)
                if d > 1.0:     # cm-scale tolerance on translations
                    ok = False
                    break
            if ok:
                matched = (glb, children)
                print(f"[{zone}] matched {os.path.basename(glb)} "
                      f"({len(children)} nodes, worst dev {worst:.4f})")
                break
            print(f"[{zone}] {os.path.basename(glb)}: matrix mismatch (dev {d:.3f} at child {children.index(ci)})")
        if not matched:
            print(f"[{zone}] FAIL: no GLB candidate validates — skipped")
            continue

        glb, children = matched
        actors = {}
        anim_actors = []
        anim_zmos = {}
        n_nocol = n_nocam = n_anim = 0
        for ci, rp in zip(children, replay):
            if rp["kind"] == "MORPH":
                continue
            col = rp["collision"]
            mode = col & 7
            nocam = bool(col & COL_NOCAMERA)
            entry = {}
            if mode == 0:
                entry["no_collision"] = True
                n_nocol += 1
            if nocam:
                entry["no_camera"] = True
                n_nocam += 1
            if col & COL_HEIGHTONLY:
                entry["height_only"] = True
            if rp["anim"]:
                zkey_anim = rp["anim"].replace("\\", "/").upper()
                if zkey_anim not in anim_zmos:
                    anim_zmos[zkey_anim] = zmo_json(zkey_anim)
                zj = anim_zmos[zkey_anim]
                if isinstance(zj, dict):
                    entry["anim"] = zkey_anim
                    wu = rp["world"]
                    anim_actors.append({
                        "tag": f"RoseAnim_{ci}",
                        "anim": zkey_anim,
                        "parent": [float(v) for v in rp["parent_world"].flatten(order="F")],
                        "pos": [float(v) for v in rp["pos"]],
                        "rot": [float(v) for v in rp["rot"]],
                        "scl": [float(v) for v in rp["scl"]],
                        "ue_loc": [wu[0, 3], -wu[1, 3], wu[2, 3]],
                    })
                    n_anim += 1
                else:
                    entry["anim_skipped"] = zj    # "missing" | "vertexmorph"
            if entry:
                wu = rp["world"]
                entry["ue_loc"] = [round(wu[0, 3], 2), round(-wu[1, 3], 2), round(wu[2, 3], 2)]
                entry["mesh"] = rp["mesh"]
                actors[str(ci)] = entry

        man = {"zone": zone, "glb": os.path.basename(glb),
               "root": config.ASSET_ROOT, "actors": actors}
        mp = os.path.join(OUT_DIR, f"zsc_parts_{zone}.json")
        with open(mp, "w") as f:
            json.dump(man, f, indent=1)

        if anim_actors:
            anims = {k: v for k, v in anim_zmos.items() if isinstance(v, dict)}
            ap = os.path.join(CONTENT, f"{zone}.json")
            with open(ap, "w") as f:
                json.dump({"zone": zone, "anims": anims, "actors": anim_actors}, f)
            print(f"[{zone}] anim json: {len(anim_actors)} actors, {len(anims)} ZMOs -> {ap}")
        skipped = {k: v for k, v in anim_zmos.items() if not isinstance(v, dict)}
        for k, v in skipped.items():
            print(f"[{zone}]   anim skipped ({v}): {k}")
        print(f"[{zone}] manifest: {len(actors)} actionable actors "
              f"(no_collision={n_nocol}, no_camera={n_nocam}, anim={n_anim}) -> {mp}")


if __name__ == "__main__":
    main()
