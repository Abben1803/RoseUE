#!/usr/bin/env python3
"""
arua_vfs_extract.py — pull files out of the AruaROSE VFS by NAME HASH.

WHY THIS EXISTS
The Arua VFS (data.idx + data.rose) stores only one-way 32-bit filename hashes;
there is NO name table.  The original extract_vfs.py therefore had to *guess*
names from file content (magic bytes, embedded self-referential paths) and
recovered 60,098 of 99,508 records.  A DDS embeds no path and its magic reveals
no name, so every DDS whose name could not be inferred was silently dropped —
about 39,400 records are unnamed for that reason, NOT because they are corrupt.

That is why ICON50-61 went missing (later recovered this way), and why the
lightmap textures are absent while the .LIT manifests that reference them came
through: the extractor could identify a .LIT, but not the DDS it points at.

So: if you can NAME a file, you can have it.  This script constructs the paths
we already know from the surviving data and pulls the bytes out.

  * terrain lightmaps  MAPS/<planet>/<zone>/<chunk>/<chunk>_PLANELIGHTINGMAP.DDS
  * object lightmaps   MAPS/<planet>/<zone>/<chunk>/LIGHTMAP/<name from .LIT>

DDS is stored PLAINTEXT — it is absent from aruavfs.rs's per-extension
AES-128-CTR + zlib table, so no decrypt or inflate step is needed.  (CHR, CON,
HIM, IFO, LIT, LTB, QSD, STB, XML, ZON and ZSC *are* encrypted; this script
refuses those rather than writing garbage.)

Hash + index layout transcribed from
  C:\\titanRose\\rose-offline\\rose-file-readers\\src\\aruavfs.rs

Usage:
  py -3.14 tools/arua_vfs_extract.py --verify
  py -3.14 tools/arua_vfs_extract.py --lightmaps [--zone EJT01] [--dry-run]
"""
import argparse
import os
import struct
import sys

IDX = r"C:\Arua\data.idx"
DAT = r"C:\Arua\data.rose"
OUT_ROOT = r"C:\rose-next-classic\Arua_Extracted"
MAPS_REL = os.path.join("3DDATA", "MAPS")

# Extensions aruavfs.rs encrypts/deflates.  DDS is deliberately NOT here.
ENCRYPTED_EXT = {
    ".chr", ".con", ".him", ".ifo", ".lit", ".ltb",
    ".qsd", ".stb", ".xml", ".zon", ".zsc",
}

HASH_TABLE = [
    0x697A5, 0x6045C, 0xAB4E2, 0x409E4, 0x71209, 0x32392, 0xA7292, 0xB09FC, 0x4B658, 0xAAAD5,
    0x9B9CF, 0xA326A, 0x8DD12, 0x38150, 0x8E14D, 0x2EB7F, 0xE0A56, 0x7E6FA, 0xDFC27, 0xB1301,
    0x8B4F7, 0xA7F70, 0xAA713, 0x6CC0F, 0x6FEDF, 0x2EC87, 0xC0F1C, 0x45CA4, 0x30DF8, 0x60E99,
    0xBC13E, 0x4E0B5, 0x6318B, 0x82679, 0x26EF2, 0x79C95, 0x86DDC, 0x99BC0, 0xB7167, 0x72532,
    0x68765, 0xC7446, 0xDA70D, 0x9D132, 0xE5038, 0x2F755, 0x9171F, 0xCB49E, 0x6F925, 0x601D3,
    0x5BD8A, 0x2A4F4, 0x9B022, 0x706C3, 0x28C10, 0x2B24B, 0x7CD55, 0xCA355, 0xD95F4, 0x727BC,
    0xB1138, 0x9AD21, 0xC0ACA, 0xCD928, 0x953E5, 0x97A20, 0x345F3, 0xBDC03, 0x7E157, 0x96C99,
    0x968EF, 0x92AA9, 0xC2276, 0xA695D, 0x6743B, 0x2723B, 0x58980, 0x66E08, 0x51D1B, 0xB97D2,
    0x6CAEE, 0xCC80F, 0x3BA6C, 0xB0BF5, 0x9E27B, 0xD122C, 0x48611, 0x8C326, 0xD2AF8, 0xBB3B7,
    0xDED7F, 0x4B236, 0xD298F, 0xBE912, 0xDC926, 0xC873F, 0xD0716, 0x9E1D3, 0x48D94, 0x9BD91,
    0x5825D, 0x55637, 0xB2057, 0xBCC6C, 0x460DE, 0xAE7FB, 0x81B03, 0x34D8F, 0xC0528, 0xC9B59,
    0x3D260, 0x6051D, 0x93757, 0x8027F, 0xB7C34, 0x4A14E, 0xB12B8, 0xE4945, 0x28203, 0xA1C0F,
    0xAA382, 0x46ABB, 0x330B9, 0x5A114, 0xA754B, 0xC68D0, 0x9040E, 0x6C955, 0xBB1EF, 0x51E6B,
    0x9FF21, 0x51BCA, 0x4C879, 0xDFF70, 0x5B5EE, 0x29936, 0xB9247, 0x42611, 0x2E353, 0x26F3A,
    0x683A3, 0xA1082, 0x67333, 0x74EB7, 0x754BA, 0x369D5, 0x8E0BC, 0xABAFD, 0x6630B, 0xA3A7E,
    0xCDBB1, 0x8C2DE, 0x92D32, 0x2F8ED, 0x7EC54, 0x572F5, 0x77461, 0xCB3F5, 0x82C64, 0x35FE0,
    0x9203B, 0xADA2D, 0xBAEBD, 0xCB6AF, 0xC8C9A, 0x5D897, 0xCB727, 0xA13B3, 0xB4D6D, 0xC4929,
    0xB8732, 0xCCE5A, 0xD3E69, 0xD4B60, 0x89941, 0x79D85, 0x39E0F, 0x6945B, 0xC37F8, 0x77733,
    0x45D7D, 0x25565, 0xA3A4E, 0xB9F9E, 0x316E4, 0x36734, 0x6F5C3, 0xA8BA6, 0xC0871, 0x42D05,
    0x40A74, 0x2E7ED, 0x67C1F, 0x28BE0, 0xE162B, 0xA1C0F, 0x2F7E5, 0xD505A, 0x9FCC8, 0x78381,
    0x29394, 0x53D6B, 0x7091D, 0xA2FB1, 0xBB942, 0x29906, 0xC412D, 0x3FCD5, 0x9F2EB, 0x8F0CC,
    0xE25C3, 0x7E519, 0x4E7D9, 0x5F043, 0xBBA1B, 0x6710A, 0x819FB, 0x9A223, 0x38E47, 0xE28AD,
    0xB690B, 0x42328, 0x7CF7E, 0xAE108, 0xE54BA, 0xBA5A1, 0xA09A6, 0x9CAB7, 0xDB2B3, 0xA98CC,
    0x5CEBA, 0x9245D, 0x5D083, 0x8EA21, 0xAE349, 0x54940, 0x8E557, 0x83EFD, 0xDC504, 0xA6059,
    0xB85C9, 0x9D162, 0x7AEB6, 0xBED34, 0xB4963, 0xE367B, 0x4C891, 0x9E42C, 0xD4304, 0x96EAA,
    0xD5D69, 0x866B8, 0x83508, 0x7BAEC, 0xD03FD, 0xDA122,
]
M32 = 0xFFFFFFFF


def name_hash(path: str) -> int:
    """aruavfs.rs FileNameHash::from — uppercased, backslash-separated."""
    path = path.replace("/", "\\").replace("\\\\", "\\")
    if not path:
        return 0
    s1, s2 = 0xDEADC0DE, 0x7FED7FED
    for c in path.upper():
        ch = ord(c) & M32
        s1 = HASH_TABLE[ch & 0xFF] ^ ((s1 + s2) & M32)
        s2 = (ch + s1 + s2 + ((s2 << 5) & M32) + 3) & M32
    return s1 & M32


def load_index(path=IDX):
    """version u32 | file_count u32 | (hash u32, size u32, offset u64) * n"""
    raw = open(path, "rb").read()
    version, file_count = struct.unpack_from("<II", raw, 0)
    if file_count & (1 << 28):
        # aruavfs.rs handles this, but the note records that THIS idx is
        # unencrypted.  Refuse rather than silently produce nonsense.
        raise SystemExit("data.idx is encrypted — this script does not decrypt it")
    files = {}
    for i in range(file_count):
        h, size, off = struct.unpack_from("<IIQ", raw, 8 + i * 16)
        files[h] = (off, size)
    return version, files


def read_blob(fh, entry):
    off, size = entry
    fh.seek(off)
    return fh.read(size)


def verify(files):
    """Prove the hash before trusting a single miss.

    MINIMAP.DDS extracted successfully, so its path is known-good: if it does
    not resolve, the hash or path shape is wrong and every 'missing' file below
    would be a false negative.
    """
    probes = [
        r"3DDATA\MAPS\JUNON\JPT01\MINIMAP.DDS",
        r"3DDATA\STB\LIST_SKILL.STB",
        r"3DDATA\AVATAR\FEMALE.ZMD",
    ]
    print(f"index: {len(files)} records")
    ok = 0
    for p in probes:
        h = name_hash(p)
        hit = files.get(h)
        print(f"  {'HIT ' if hit else 'MISS'} {h:#010x}  {p}"
              + (f"  size={hit[1]}" if hit else ""))
        ok += bool(hit)
    return ok


def lit_filenames(lit_path):
    """The DDS names a .LIT references (rose-lib lit.rs: objects then filenames)."""
    d = open(lit_path, "rb").read()
    o = 0
    (obj_count,) = struct.unpack_from("<i", d, o); o += 4
    for _ in range(obj_count):
        (part_count,) = struct.unpack_from("<i", d, o); o += 8      # part_count, id
        for _ in range(part_count):
            n = d[o]; o += 1 + n                                    # name
            o += 4                                                  # id
            n = d[o]; o += 1 + n                                    # filename
            o += 16                                                 # 4 x i32
    (file_count,) = struct.unpack_from("<i", d, o); o += 4
    out = []
    for _ in range(file_count):
        n = d[o]; o += 1
        out.append(d[o:o + n].decode("cp1252", "replace")); o += n
    return out


def collect_lightmap_paths(zone_filter=None):
    """Every lightmap DDS path we can NAME, from the surviving zone tree."""
    wanted = []
    maps = os.path.join(OUT_ROOT, MAPS_REL)
    for planet in sorted(os.listdir(maps)):
        pdir = os.path.join(maps, planet)
        if not os.path.isdir(pdir):
            continue
        for zone in sorted(os.listdir(pdir)):
            if zone_filter and zone.upper() != zone_filter.upper():
                continue
            zdir = os.path.join(pdir, zone)
            if not os.path.isdir(zdir):
                continue
            for chunk in sorted(os.listdir(zdir)):
                cdir = os.path.join(zdir, chunk)
                if not os.path.isdir(cdir):
                    continue
                # terrain lightmap: <chunk>/<chunk>_PLANELIGHTINGMAP.DDS
                rel = f"3DDATA\\MAPS\\{planet}\\{zone}\\{chunk}\\{chunk}_PLANELIGHTINGMAP.DDS"
                wanted.append((rel, os.path.join(cdir, f"{chunk}_PLANELIGHTINGMAP.DDS")))
                # object lightmaps named by the .LIT manifests
                lmdir = os.path.join(cdir, "LIGHTMAP")
                if not os.path.isdir(lmdir):
                    continue
                names = set()
                for lit in ("OBJECTLIGHTMAPDATA.LIT", "BUILDINGLIGHTMAPDATA.LIT"):
                    lp = os.path.join(lmdir, lit)
                    if os.path.exists(lp):
                        try:
                            names.update(lit_filenames(lp))
                        except Exception as e:
                            print(f"    !! {lp}: {e}")
                for nm in sorted(names):
                    rel = f"3DDATA\\MAPS\\{planet}\\{zone}\\{chunk}\\LIGHTMAP\\{nm}"
                    wanted.append((rel, os.path.join(lmdir, nm)))
    return wanted


def walk_extracted():
    """Every file already extracted, as its VFS-relative backslash path."""
    for root, _dirs, names in os.walk(OUT_ROOT):
        for n in names:
            full = os.path.join(root, n)
            rel = os.path.relpath(full, OUT_ROOT).replace("/", "\\")
            yield rel, full


# Magic -> extension, for records whose NAME we cannot reconstruct.
# Only plaintext formats can be sniffed: aruavfs.rs AES-CTR encrypts CHR/CON/
# HIM/IFO/LIT/LTB/QSD/STB/XML/ZON/ZSC, so those look like noise here.
MAGICS = [
    (b"DDS ", "dds"),
    (b"ZMS0", "zms"),
    (b"ZMO0", "zmo"),
    (b"ZMD0", "zmd"),
    (b"\x89PNG", "png"),
    (b"\xff\xd8\xff", "jpg"),
    (b"RIFF", "wav"),
    (b"OggS", "ogg"),
    (b"ID3", "mp3"),
    (b"BM", "bmp"),
    (b"NRST", "stl"),
    (b"ITST", "stl"),
    (b"QEST", "stl"),
    (b"<", "xml"),
]


def sniff(blob):
    for magic, ext in MAGICS:
        if blob.startswith(magic):
            return ext
    return None


def coverage(files):
    """How much of the index do the names we can already construct account for?"""
    known = {}
    for rel, _full in walk_extracted():
        known.setdefault(name_hash(rel), rel)
    matched = set(files) & set(known)
    unmatched = set(files) - set(known)
    print(f"index records      : {len(files)}")
    print(f"extracted files    : {len(known)} distinct hashes")
    print(f"  matched in index : {len(matched)}")
    print(f"  UNNAMED in index : {len(unmatched)}")
    total = sum(files[h][1] for h in unmatched)
    print(f"  unnamed bytes    : {total/1e9:.2f} GB")
    return unmatched


def entropy(b):
    import collections
    import math
    if not b:
        return 0.0
    c = collections.Counter(b)
    n = len(b)
    return -sum((v / n) * math.log2(v / n) for v in c.values())


def embedded_name(blob):
    """Many plaintext ROSE records begin u32(0) + u8(len) + NAME.

    That name is the asset's own identifier (grass001_Object, RmCeiling03,
    lunarsnow01 ...), which is far more useful on disk than a hash — even
    without knowing the original directory.
    """
    if len(blob) < 6 or blob[:4] != b"\x00\x00\x00\x00":
        return None
    n = blob[4]
    if not (3 <= n <= 96) or len(blob) < 5 + n:
        return None
    raw = blob[5:5 + n]
    try:
        s = raw.decode("cp1252")
    except Exception:
        return None
    # Must look like a name, not binary that happens to be short.
    if not all(32 <= ord(c) < 127 for c in s):
        return None
    keep = "".join(ch if (ch.isalnum() or ch in "._- ") else "_" for ch in s).strip()
    return keep or None


def dump_unnamed(files, unmatched, out_dir, dry_run):
    """Write EVERY record we cannot place, typed as well as we can manage.

    Three tiers, because they are genuinely different things:
      magic/    identified by file magic  -> real extension, usable directly
      named/    plaintext with an embedded self-name -> <name>.bin
      opaque/   high-entropy = AES-encrypted (aruavfs.rs per-extension keys),
                or plaintext we cannot characterise at all -> <hash>.bin
    """
    from collections import Counter
    kinds = Counter()
    wrote = 0
    with open(DAT, "rb") as fh:
        for h in sorted(unmatched):
            blob = read_blob(fh, files[h])

            ext = sniff(blob)
            if ext:
                tier, name = "magic/" + ext, f"{h:08x}.{ext}"
            else:
                nm = embedded_name(blob)
                if nm:
                    tier, name = "named", f"{nm}.{h:08x}.bin"
                elif entropy(blob[:4096]) >= 7.0:
                    tier, name = "opaque/encrypted", f"{h:08x}.bin"
                else:
                    tier, name = "opaque/plain", f"{h:08x}.bin"

            kinds[tier] += 1
            if dry_run:
                continue
            sub = os.path.join(out_dir, *tier.split("/"))
            os.makedirs(sub, exist_ok=True)
            with open(os.path.join(sub, name), "wb") as out:
                out.write(blob)
            wrote += 1

    print("\nunnamed records written by tier:")
    for k, v in kinds.most_common():
        print(f"   {k:<24} {v}")
    print(f"written: {wrote}")


def harvest_reference_paths():
    """Paths that surviving files NAME, so we can hash-look-them-up.

    This is the general form of the lightmap trick: a ZSC lists its meshes and
    textures, a CHR its skeletons and motions, a ZON its tile textures.  Those
    references survived even where the referenced DDS/ZMS did not, because the
    extractor could identify the container but not the payload.
    """
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
    from rose_parser.formats import zsc as ZSC
    from rose_parser.formats import zon as ZON

    refs = set()

    def add(p):
        if not p:
            return
        p = p.replace("/", "\\").lstrip("\\")
        # Stored paths sometimes already carry the data root, sometimes not.
        if not p.upper().startswith("3DDATA"):
            p = "3DDATA\\" + p
        refs.add(p)

    from rose_parser.formats import stb as STB

    # A path-shaped cell: has an extension we care about.  STB columns are the
    # single richest source of names (per the ICON50-61 recovery note), and they
    # reference far more than ZSC does — effects, motions, NPC parts, UI art.
    import re
    PATHY = re.compile(
        r"^[\w\\/\.\- ]+\.(dds|zms|zmo|zmd|chr|zsc|eft|ptl|tsi|stl|wav|ogg|jpg|bmp|png)$",
        re.IGNORECASE)

    root = os.path.join(OUT_ROOT, "3DDATA")
    for dirpath, _d, names in os.walk(root):
        for n in names:
            ext = os.path.splitext(n)[1].lower()
            full = os.path.join(dirpath, n)
            try:
                if ext == ".zsc":
                    z = ZSC.parse(full)
                    for m in getattr(z, "meshes", []):
                        add(m)
                    for mat in getattr(z, "materials", []):
                        add(getattr(mat, "texture_path", None))
                elif ext == ".zon":
                    z = ZON.parse(full)
                    for t in getattr(z, "tile_textures", []):
                        add(t)
                elif ext == ".stb":
                    s = STB.parse(full)
                    for r in range(s.num_rows()):
                        for c in range(s.num_cols()):
                            v = s.get(r, c).strip()
                            if v and PATHY.match(v):
                                add(v)
                elif ext in (".eft", ".ptl"):
                    # EFT/PTL are plaintext and embed texture/particle paths as
                    # length-prefixed strings; a coarse scan is enough to name
                    # the DDS they point at.
                    blob = open(full, "rb").read()
                    for m in re.finditer(rb"[\w\\/\.\- ]{4,120}\.(?:dds|ptl|zms|zmo)", blob,
                                         re.IGNORECASE):
                        add(m.group(0).decode("cp1252", "ignore"))
            except Exception:
                pass
    return refs


def extract_named(files, refs, dry_run):
    hit = miss = wrote = exists = 0
    with open(DAT, "rb") as fh:
        for rel in sorted(refs):
            dest = os.path.join(OUT_ROOT, rel.replace("\\", os.sep))
            if os.path.exists(dest):
                exists += 1
                continue
            entry = files.get(name_hash(rel))
            if not entry:
                miss += 1
                continue
            hit += 1
            if dry_run:
                continue
            blob = read_blob(fh, entry)
            # Only write formats stored PLAINTEXT; the rest would be ciphertext.
            if os.path.splitext(dest)[1].lower() in ENCRYPTED_EXT:
                continue
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as out:
                out.write(blob)
            wrote += 1
    print(f"  referenced paths : {len(refs)}")
    print(f"  already on disk  : {exists}")
    print(f"  recoverable      : {hit}")
    print(f"  not in index     : {miss}")
    print(f"  written          : {wrote}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--lightmaps", action="store_true")
    ap.add_argument("--coverage", action="store_true")
    ap.add_argument("--dump-unnamed", metavar="OUTDIR")
    ap.add_argument("--from-references", action="store_true",
                    help="harvest paths referenced by surviving ZSC/ZON and recover them")
    ap.add_argument("--zone")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.from_references:
        _, files = load_index()
        if verify(files) == 0:
            print("verification FAILED — refusing to continue")
            return 1
        refs = harvest_reference_paths()
        extract_named(files, refs, args.dry_run)
        return 0

    if args.coverage or args.dump_unnamed:
        _, files = load_index()
        if verify(files) == 0:
            print("verification FAILED — refusing to continue")
            return 1
        unmatched = coverage(files)
        if args.dump_unnamed:
            dump_unnamed(files, unmatched, args.dump_unnamed, args.dry_run)
        return 0

    _, files = load_index()

    if args.verify or not args.lightmaps:
        hits = verify(files)
        if not args.lightmaps:
            return 0 if hits else 1
        if hits == 0:
            print("verification FAILED — refusing to run; the hash or path shape is wrong")
            return 1

    wanted = collect_lightmap_paths(args.zone)
    print(f"\ncandidate lightmap paths: {len(wanted)}")

    hit = miss = wrote = skipped = 0
    with open(DAT, "rb") as fh:
        for rel, dest in wanted:
            if os.path.splitext(dest)[1].lower() in ENCRYPTED_EXT:
                skipped += 1
                continue
            entry = files.get(name_hash(rel))
            if not entry:
                miss += 1
                continue
            hit += 1
            if args.dry_run:
                continue
            blob = read_blob(fh, entry)
            if blob[:4] != b"DDS ":
                print(f"    !! not a DDS, refusing to write: {rel}")
                continue
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as out:
                out.write(blob)
            wrote += 1

    print(f"  in index : {hit}")
    print(f"  missing  : {miss}")
    print(f"  written  : {wrote}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
