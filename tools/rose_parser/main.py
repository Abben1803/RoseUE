"""ROSE Online → Unreal Engine 5 asset pipeline CLI.

Usage:
  python -m rose_parser <command> [options]

Commands:
  info <file>               Dump parsed info about any ROSE file
  export-tables             Export all STB data tables to CSV/JSON
  export-zone <zone_id>     Export a zone's heightmaps + placement to JSON/RAW
  export-mesh <zms_file>    Export a ZMS mesh to JSON
  export-skeleton <zmd>     Export a ZMD skeleton to JSON
  export-anim <zmo> <zmd>   Export a ZMO animation to JSON
  export-npc-chr <chr>      Export a CHR character definition to JSON
  list-zones                List all zones from LIST_ZONE.STB
  list-npcs                 List all NPCs from LIST_NPC.STB
  batch-export-zone <zone>  Full zone export (heightmaps + placement + manifest)

Run from the repo root:
  python -m tools.rose_parser <command> [options]
Or set PYTHONPATH and run:
  python -m rose_parser <command> [options]
"""
import argparse
import json
import os
import sys


def _vfs_root(args) -> str:
    root = getattr(args, "vfs_root", None) or os.environ.get("ROSE_VFS_ROOT", "")
    if not root:
        # auto-detect: look for client/out relative to this file's repo root
        here = os.path.dirname(os.path.abspath(__file__))
        candidate = os.path.normpath(os.path.join(here, "..", "..", "client", "out"))
        if os.path.isdir(candidate):
            root = candidate
    if not root or not os.path.isdir(root):
        print(f"[error] VFS root not found. Pass --vfs-root or set ROSE_VFS_ROOT.")
        sys.exit(1)
    return root


def cmd_info(args):
    path = args.file
    ext  = os.path.splitext(path)[1].upper()
    from . import formats

    try:
        if ext == ".STB":
            data = formats.stb.parse(path)
            print(f"STB: {data.num_rows()} rows × {data.num_cols()} cols")
            if data.header:
                print(f"  Header: {data.header[:8]}")
            print(f"  First data row: {data.data_rows[0][:8] if data.data_rows else []}")

        elif ext == ".ZMD":
            data = formats.zmd.parse(path)
            print(f"ZMD v{data.version}: {len(data.bones)} bones, {len(data.dummies)} dummies")
            for b in data.bones[:5]:
                print(f"  bone[{b.index}] parent={b.parent_id} name={b.name!r}")

        elif ext == ".ZMS":
            data = formats.zms.parse(path)
            print(f"ZMS v{data.version}: {len(data.vertices)} verts, {len(data.faces)} tris")
            print(f"  vertex_format=0x{data.vertex_format:04X}  UV channels={data.uv_channel_count()}")
            print(f"  skinned={data.has_skin()}  bones={len(data.bone_indices)}")

        elif ext == ".ZMO":
            data = formats.zmo.parse(path)
            print(f"ZMO: {data.fps}fps, {data.num_frames} frames ({data.duration_seconds():.2f}s)")
            print(f"  {len(data.channels)} channels")
            for ch in data.channels[:5]:
                print(f"  ch: type={ch.type_name} refer_id={ch.refer_id}")

        elif ext == ".ZSC":
            data = formats.zsc.parse(path)
            print(f"ZSC: {len(data.mesh_files)} meshes, {len(data.materials)} mats, "
                  f"{len(data.effect_files)} effects, {len(data.models)} models")
            for m in data.mesh_files[:5]:
                print(f"  mesh: {m}")

        elif ext == ".HIM":
            data = formats.him.parse(path)
            flat = data.to_flat_list()
            print(f"HIM: {data.width}×{data.height} vertices, "
                  f"patch_grid={data.patch_grid_count}, patch_size={data.patch_size}")
            print(f"  height range: {min(flat):.1f} .. {max(flat):.1f}")

        elif ext == ".TIL":
            data = formats.til.parse(path)
            print(f"TIL: {data.width}×{data.height} patches")

        elif ext == ".IFO":
            data = formats.ifo.parse(path)
            total = sum(len(v) for v in data.objects.values())
            print(f"IFO: {total} objects across {len(data.objects)} lump types")
            for ltype, objs in data.objects.items():
                from .formats.ifo import LUMP_NAMES
                print(f"  {LUMP_NAMES.get(ltype, ltype)}: {len(objs)} objects")

        elif ext == ".ZON":
            data = formats.zon.parse(path)
            zi = data.zone_info
            if zi:
                print(f"ZON: {zi.width}×{zi.height}, grid_size={zi.grid_size}")
                print(f"  center=({zi.center_x},{zi.center_y}), patch_size={zi.patch_size}")
            print(f"  tiles={len(data.tile_textures)}, brushes={len(data.brushes)}")
            print(f"  name={data.zone_name!r}, dungeon={data.is_dungeon}")

        elif ext == ".CHR":
            data = formats.chr_.parse(path)
            valid = sum(1 for m in data.models if m.is_valid)
            print(f"CHR: {len(data.skeleton_files)} skels, {len(data.motion_files)} motions, "
                  f"{len(data.models)} models ({valid} valid)")

        else:
            print(f"[error] Unsupported extension: {ext}")
            sys.exit(1)

    except Exception as e:
        print(f"[error] {e}")
        raise


def cmd_export_tables(args):
    from .resolver import AssetResolver
    from .exporters import ue_datatable
    root  = _vfs_root(args)
    res   = AssetResolver(root)
    stb_dir = res.abs(os.path.join("3DDATA", "STB"))
    out_dir = args.out_dir or os.path.join("export", "datatables")
    fmt = args.format or "csv"
    print(f"Exporting STB tables from {stb_dir} → {out_dir} (format={fmt})")
    outputs = ue_datatable.batch_export(stb_dir, out_dir, fmt)
    print(f"  Exported {len(outputs)} files")


def cmd_list_zones(args):
    from .resolver import AssetResolver
    root = _vfs_root(args)
    res  = AssetResolver(root)
    zones = res.zone_list()
    print(f"{'ID':>4}  {'Name':<30}  {'ZON File'}")
    print("-" * 70)
    for z in zones:
        print(f"{z['row']:>4}  {z['name']:<30}  {z['zon_file']}")


def cmd_list_npcs(args):
    from .resolver import AssetResolver
    root = _vfs_root(args)
    res  = AssetResolver(root)
    npcs = res.npc_list()
    print(f"{'ID':>4}  {'Name':<30}  CHR  LvL  HP")
    print("-" * 60)
    for n in npcs[:50]:
        print(f"{n['id']:>4}  {n['name']:<30}  {n['chr_id']:>3}  {n['level']:>3}  {n['hp']}")


def cmd_export_zone(args):
    from .resolver import AssetResolver
    from .exporters import ue_landscape, ue_placement
    from .formats import him as him_mod, ifo as ifo_mod, zon as zon_mod

    root = _vfs_root(args)
    res  = AssetResolver(root)

    zones = res.zone_list()
    zone_id = int(args.zone_id)
    zone = next((z for z in zones if z["row"] == zone_id), None)
    if not zone:
        print(f"[error] Zone {zone_id} not found")
        sys.exit(1)

    out_base = args.out_dir or os.path.join("export", "zones", zone["name"] or f"zone_{zone_id}")
    os.makedirs(out_base, exist_ok=True)
    print(f"Exporting zone {zone_id} ({zone['name']}) → {out_base}")

    # Export ZON
    zon_path = res.find_ci(zone["zon_file"])
    if zon_path:
        try:
            zon_data = zon_mod.parse(zon_path)
            with open(os.path.join(out_base, "zone_info.json"), "w") as f:
                zi = zon_data.zone_info
                json.dump({
                    "name": zon_data.zone_name,
                    "is_dungeon": zon_data.is_dungeon,
                    "music": zon_data.music_file,
                    "sky": zon_data.sky_name,
                    "grid_size": zi.grid_size if zi else 0,
                    "patch_size": zi.patch_size if zi else 0,
                    "center": [zi.center_x, zi.center_y] if zi else [0, 0],
                    "tiles": zon_data.tile_textures,
                    "brushes": [
                        {"t1": b.texture1_id, "t2": b.texture2_id,
                         "blend": b.is_blending, "orient": b.orientation}
                        for b in zon_data.brushes
                    ],
                }, f, indent=2)
            print(f"  zone_info.json written")
        except Exception as e:
            print(f"  [warn] ZON: {e}")

    # Enumerate chunks
    folder = zone.get("folder") or os.path.dirname(zone.get("zon_file", ""))
    chunks = res.chunk_files(folder)
    print(f"  {len(chunks)} chunks found")

    # Export placement manifest
    placement_path = os.path.join(out_base, "placement.json")
    ue_placement.export_zone_placements(chunks, placement_path)
    print(f"  placement.json written")

    # Export heightmaps
    hm_dir = os.path.join(out_base, "heightmaps")
    os.makedirs(hm_dir, exist_ok=True)
    heights_ok = 0
    for chunk in chunks:
        him_path = chunk.get("him")
        if not him_path:
            continue
        try:
            him_data = him_mod.parse(him_path)
            flat = him_data.to_flat_list()
            hmin, hmax = min(flat), max(flat)
            raw_path = os.path.join(hm_dir, f"{chunk['cx']}_{chunk['cy']}.r16")
            ue_landscape.to_raw_r16(him_data, raw_path, hmin, hmax)
            heights_ok += 1
        except Exception as e:
            print(f"  [warn] HIM {chunk['base']}: {e}")
    print(f"  {heights_ok} heightmap RAW files written to {hm_dir}")


def cmd_export_mesh(args):
    from .formats import zms as zms_mod
    from .exporters import ue_mesh
    data = zms_mod.parse(args.zms_file)
    out  = args.out or args.zms_file + ".json"
    ue_mesh.export_static_mesh(data, out)
    print(f"Exported mesh → {out}")


def cmd_export_skeleton(args):
    from .formats import zmd as zmd_mod
    data = zmd_mod.parse(args.zmd_file)
    out  = args.out or args.zmd_file + ".json"
    result = {
        "version": data.version,
        "bones": [
            {"index": b.index, "parent": b.parent_id, "name": b.name,
             "pos": list(b.translation), "rot": list(b.rotation)}
            for b in data.bones
        ],
        "dummies": [
            {"index": d.index, "parent": d.parent_id, "name": d.name,
             "pos": list(d.translation),
             "rot": list(d.rotation) if d.rotation else None}
            for d in data.dummies
        ],
    }
    import os
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    print(f"Exported skeleton → {out}")


def cmd_export_anim(args):
    from .formats import zmo as zmo_mod, zmd as zmd_mod
    from .exporters import ue_mesh
    zmo_data = zmo_mod.parse(args.zmo_file)
    zmd_data = zmd_mod.parse(args.zmd_file)
    out = args.out or args.zmo_file + ".json"
    anim_name = os.path.splitext(os.path.basename(args.zmo_file))[0]
    ue_mesh.export_animation(zmo_data, zmd_data, out, anim_name)
    print(f"Exported animation → {out}")


def cmd_export_npc_chr(args):
    from .formats import chr_ as chr_mod
    data = chr_mod.parse(args.chr_file)
    out  = args.out or args.chr_file + ".json"
    result = {
        "skeleton_files": data.skeleton_files,
        "motion_files":   data.motion_files,
        "effect_files":   data.effect_files,
        "models": [
            {
                "is_valid": m.is_valid,
                "name": m.name,
                "skel_index": m.skel_index,
                "skel_file": data.skeleton_files[m.skel_index]
                             if m.is_valid and m.skel_index < len(data.skeleton_files) else "",
                "body_parts": m.body_part_indices,
                "animations": {str(k): {"motion_idx": v,
                                        "motion_file": data.motion_files[v]
                                        if v < len(data.motion_files) else ""}
                               for k, v in m.animations.items()},
                "bone_effects": [{"bone": be.bone_idx, "effect_idx": be.effect_file_idx,
                                  "effect_file": data.effect_files[be.effect_file_idx]
                                  if be.effect_file_idx < len(data.effect_files) else ""}
                                 for be in m.bone_effects],
            }
            for m in data.models
        ],
    }
    import os
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    print(f"Exported CHR → {out}")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="rose_parser",
        description="ROSE Online → Unreal Engine 5 asset pipeline",
    )
    p.add_argument("--vfs-root", dest="vfs_root", default="",
                   help="Path to extracted VFS directory (client/out/)")
    sub = p.add_subparsers(dest="command")

    # info
    s = sub.add_parser("info", help="Dump info about a ROSE file")
    s.add_argument("file")
    s.set_defaults(func=cmd_info)

    # export-tables
    s = sub.add_parser("export-tables", help="Export all STB tables to CSV/JSON")
    s.add_argument("--out-dir", default="")
    s.add_argument("--format", choices=["csv", "json"], default="csv")
    s.set_defaults(func=cmd_export_tables)

    # list-zones
    s = sub.add_parser("list-zones", help="List zones from LIST_ZONE.STB")
    s.set_defaults(func=cmd_list_zones)

    # list-npcs
    s = sub.add_parser("list-npcs", help="List NPCs from LIST_NPC.STB")
    s.set_defaults(func=cmd_list_npcs)

    # export-zone
    s = sub.add_parser("export-zone", help="Export a zone (heightmaps + placement)")
    s.add_argument("zone_id", type=int)
    s.add_argument("--out-dir", default="")
    s.set_defaults(func=cmd_export_zone)

    # export-mesh
    s = sub.add_parser("export-mesh", help="Export a ZMS mesh to JSON")
    s.add_argument("zms_file")
    s.add_argument("--out", default="")
    s.set_defaults(func=cmd_export_mesh)

    # export-skeleton
    s = sub.add_parser("export-skeleton", help="Export a ZMD skeleton to JSON")
    s.add_argument("zmd_file")
    s.add_argument("--out", default="")
    s.set_defaults(func=cmd_export_skeleton)

    # export-anim
    s = sub.add_parser("export-anim", help="Export a ZMO animation + ZMD skeleton to JSON")
    s.add_argument("zmo_file")
    s.add_argument("zmd_file")
    s.add_argument("--out", default="")
    s.set_defaults(func=cmd_export_anim)

    # export-npc-chr
    s = sub.add_parser("export-npc-chr", help="Export a CHR NPC definition to JSON")
    s.add_argument("chr_file")
    s.add_argument("--out", default="")
    s.set_defaults(func=cmd_export_npc_chr)

    return p


def main():
    parser = build_parser()
    args = parser.parse_args()
    if not args.command:
        parser.print_help()
        sys.exit(1)
    args.func(args)


if __name__ == "__main__":
    main()
