#!/usr/bin/env python3
"""
export_portals.py — Export every zone's warp gates for UE placement.

Faithful chain (gs_user.cpp Recv_cli_TELEPORT_REQ + zonefile.cpp
ReadEventObjINFO): IFO WarpPoint object (warp_id + gate transform) →
WARP.STB[warp_id] (col 1 = dest zone id, col 2 = event-position name) →
dest zone's ZON event-object block (name → position, file order x, height, y,
zone-centred) → world.

UE mapping (same as the map scene): UE = (x+520000, −(y+520000), z).

Output: tools/_tmp/portals.json  { zone_key: [gate, ...] }
Also prints the valid-zone list (used by the batch driver).

Usage:  py -3.9 export_portals.py                      # all classic zones
        ROSE_ASSET_ROOT=<other 3DDATA> \
        py -3.9 export_portals.py <ZONE> <OUT_ZONE>    # cross-client single
            zone (JPT01 JPT01V2): gates + warp ids from the OTHER client,
            destinations resolved to CLASSIC zones by directory key, MERGED
            into portals.json under OUT_ZONE.
"""
import json
import math
import os
import sys

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.ifo import parse as parse_ifo, BLOCK_WARP
from rose_parser.formats.zon import parse as parse_zon

# Asset source: Arua (CLAUDE.md).  LIST_ZONE.STB is 235 rows in Arua vs 66 in
# classic and zone ids do not line up (row 10 = 'Adventurers Plains (EVO)' vs
# 'Character Select'), so classic-sourced portals resolved to the wrong zones.
SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
CENTER = 520000.0


def zone_table(root=None):
    """zone id -> {key, dir, zon_abs, name} for every zone with map data."""
    root = root or SRC
    zones = {}
    stb = parse_stb(os.path.join(root, "STB", "LIST_ZONE.STB"))
    for i in range(stb.num_rows()):
        zon_rel = stb.get(i, 1)          # col 1 = ZON path, col 0 = name
        if not zon_rel.strip():
            continue
        parts = [p for p in zon_rel.replace("\\", "/").split("/") if p]
        if parts and parts[0].upper() == "3DDATA":
            parts = parts[1:]
        zon_abs = os.path.join(root, *parts)
        zdir = os.path.dirname(zon_abs)
        if not os.path.isfile(zon_abs) or not os.path.isdir(zdir):
            continue
        if not any(f.upper().endswith(".IFO") for f in os.listdir(zdir)):
            continue
        key = os.path.basename(zdir).upper()
        name = stb.get(i, 0)
        zones[i] = {"key": key, "dir": zdir, "zon": zon_abs,
                    "name": name if name.isascii() and name.strip() else key}
    return zones


_zon_events = {}
def zon_events(zon_abs):
    if zon_abs not in _zon_events:
        try:
            z = parse_zon(zon_abs)
            # file order is (x, height, y) — server reads fX, fZ, fY.
            _zon_events[zon_abs] = {e.name.upper(): e.position for e in z.event_objects}
        except Exception as e:
            print(f"[portals] ZON {zon_abs}: {e}")
            _zon_events[zon_abs] = {}
    return _zon_events[zon_abs]


def quat_yaw_deg(q):
    """Yaw (deg) around ROSE Z from an IFO quaternion (w,x,y,z)."""
    w, x, y, z = q
    return math.degrees(math.atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z)))


def export_cross_client(src_zone, out_zone):
    """Gates of <src_zone> read from ROSE_ASSET_ROOT's IFOs + WARP.STB, with
    destinations mapped to CLASSIC zones by directory KEY (zone ids are not
    trusted across clients) — merged into portals.json as <out_zone>."""
    root = os.environ["ROSE_ASSET_ROOT"]
    new_zones = zone_table(root)
    new_by_key = {z["key"]: z for z in new_zones.values()}
    classic = zone_table()
    classic_by_key = {z["key"]: z for z in classic.values()}
    warp = parse_stb(os.path.join(root, "STB", "WARP.STB"))

    src = new_by_key.get(src_zone)
    if not src:
        raise SystemExit(f"[portals] {src_zone} not in {root}")

    gates, misses = [], 0
    for f in sorted(os.listdir(src["dir"])):
        if not f.upper().endswith(".IFO"):
            continue
        ifo = parse_ifo(os.path.join(src["dir"], f))
        for o in ifo.get_lump(BLOCK_WARP):
            wid = o.warp_id
            if wid <= 0 or wid >= warp.num_rows():
                continue
            dest_id = warp.get_int(wid, 1)
            event = warp.get(wid, 2).strip()
            new_dest = new_zones.get(dest_id)
            # dest resolves to a CLASSIC zone (its level + ZON positions)
            dz = classic_by_key.get(new_dest["key"]) if new_dest else None
            if not dz or not event:
                misses += 1
                continue
            ev = zon_events(dz["zon"]).get(event.upper())
            if ev is None:
                misses += 1
                print(f"[portals] {src_zone}: warp {wid} -> {dz['key']}:'{event}' not in classic ZON")
                continue
            x, y, h = o.position
            sx, sy, sz = (abs(s) or 1.0 for s in o.scale)
            # a same-zone gate must come back to the V2 level, not classic JPT01
            dest_key = out_zone if dz["key"] == src_zone else dz["key"]
            gates.append({
                "name": o.name or f"warp_{wid}",
                "pos": [x + CENTER, -(y + CENTER), h],
                "yaw": -quat_yaw_deg(o.rotation),
                "extent": [max(120.0, 150.0 * sx), max(120.0, 150.0 * sy),
                           max(200.0, 200.0 * sz)],
                "dest_zone": dest_key,
                "dest_level": f"L_{dest_key}",
                "dest_name": dz["name"],
                "dest_pos": [ev[0] + CENTER, -(ev[2] + CENTER)],
            })

    path = os.path.join(_T, "_tmp", "portals.json")
    data = {}
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as fp:
            data = json.load(fp)
    data[out_zone] = gates
    with open(path, "w", encoding="utf-8") as fp:
        json.dump(data, fp, indent=1)
    print(f"[portals] {out_zone}: {len(gates)} gates ({misses} unresolved) merged -> {path}")


def main():
    zones = zone_table()
    warp = parse_stb(os.path.join(SRC, "STB", "WARP.STB"))
    by_id = {i: z for i, z in zones.items()}

    out = {}
    misses = 0
    for zid, z in sorted(zones.items()):
        gates = []
        for f in sorted(os.listdir(z["dir"])):
            if not f.upper().endswith(".IFO"):
                continue
            ifo = parse_ifo(os.path.join(z["dir"], f))
            for o in ifo.get_lump(BLOCK_WARP):
                wid = o.warp_id
                if wid <= 0 or wid >= warp.num_rows():
                    continue
                dest_zone = warp.get_int(wid, 1)
                event = warp.get(wid, 2).strip()
                dz = by_id.get(dest_zone)
                if not dz or not event:
                    misses += 1
                    continue
                ev = zon_events(dz["zon"]).get(event.upper())
                if ev is None:
                    misses += 1
                    print(f"[portals] {z['key']}: warp {wid} -> {dz['key']}:'{event}' not in ZON")
                    continue
                x, y, h = o.position
                sx, sy, sz = (abs(s) or 1.0 for s in o.scale)
                gates.append({
                    "name": o.name or f"warp_{wid}",
                    "pos": [x + CENTER, -(y + CENTER), h],
                    "yaw": -quat_yaw_deg(o.rotation),
                    "extent": [max(120.0, 150.0 * sx), max(120.0, 150.0 * sy),
                               max(200.0, 200.0 * sz)],
                    "dest_zone": dz["key"],
                    "dest_level": f"L_{dz['key']}",
                    "dest_name": dz["name"],
                    "dest_pos": [ev[0] + CENTER, -(ev[2] + CENTER)],
                })
        if gates:
            out[z["key"]] = gates

    path = os.path.join(_T, "_tmp", "portals.json")
    with open(path, "w", encoding="utf-8") as fp:
        json.dump(out, fp, indent=1)
    total = sum(len(g) for g in out.values())
    print(f"[portals] {total} gates across {len(out)} zones ({misses} unresolved) -> {path}")
    print("[portals] zones with map data: " + ",".join(z["key"] for z in zones.values()))


if __name__ == "__main__":
    if len(sys.argv) > 2 and os.environ.get("ROSE_ASSET_ROOT"):
        export_cross_client(sys.argv[1].upper(), sys.argv[2].upper())
    else:
        main()
