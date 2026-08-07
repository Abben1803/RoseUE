"""Cross-reference resolver for ROSE asset dependency graph.

Reads the STB tables to build mappings between IDs and file paths,
then resolves those paths against the extracted VFS at client/out/.

Key STB files (all under 3DDATA/STB/):
  FILE_SKEL.STB  — col 0 = ZMD path
  FILE_MOTION.STB — col 0 = ZMO path
  FILE_AI.STB    — col 0 = AIP path
  LIST_ZONE.STB  — col 1 = zone folder, col 2 = .ZON file path
  LIST_WEAPON.STB, LIST_BODY.STB, etc. — item data tables
  LIST_NPC.STB   — col 4 = CHR row index, col 1 = NPC name
"""
import os
from typing import Dict, List, Optional
from .formats import stb as stb_mod

STB_DIR = os.path.join("3DDATA", "STB")


class AssetResolver:
    def __init__(self, vfs_root: str):
        """vfs_root: path to the extracted VFS directory (client/out/)."""
        self.root = os.path.normpath(vfs_root)
        self._stb_cache: Dict[str, stb_mod.STB] = {}

    # ---- path helpers ----

    def abs(self, rel_path: str) -> str:
        """Resolve a ROSE relative path (e.g. '3DDATA/AVATAR/MALE.ZMD') to absolute."""
        return os.path.join(self.root, rel_path.replace("\\", os.sep))

    def exists(self, rel_path: str) -> bool:
        return os.path.isfile(self.abs(rel_path))

    def find_ci(self, rel_path: str) -> Optional[str]:
        """Case-insensitive file lookup — ROSE paths use mixed case."""
        parts = rel_path.replace("\\", "/").split("/")
        current = self.root
        for part in parts:
            if not os.path.isdir(current):
                return None
            entries = {e.lower(): e for e in os.listdir(current)}
            matched = entries.get(part.lower())
            if matched is None:
                return None
            current = os.path.join(current, matched)
        return current if os.path.exists(current) else None

    # ---- STB helpers ----

    def stb(self, filename: str) -> stb_mod.STB:
        """Load and cache an STB by filename (relative to 3DDATA/STB/)."""
        key = filename.upper()
        if key not in self._stb_cache:
            path = self.find_ci(os.path.join(STB_DIR, filename))
            if path is None:
                raise FileNotFoundError(f"STB not found: {filename} under {self.root}")
            self._stb_cache[key] = stb_mod.parse(path)
        return self._stb_cache[key]

    # ---- skeleton lookups ----

    def skeleton_path(self, skel_row: int) -> Optional[str]:
        """Return VFS-relative ZMD path for a FILE_SKEL row index."""
        try:
            tbl = self.stb("FILE_SKEL.STB")
            return tbl.get(skel_row + 1, 0) or None  # row 0 = header
        except Exception:
            return None

    # ---- motion lookups ----

    def motion_path(self, motion_row: int) -> Optional[str]:
        """Return VFS-relative ZMO path for a FILE_MOTION row index."""
        try:
            tbl = self.stb("FILE_MOTION.STB")
            return tbl.get(motion_row + 1, 0) or None
        except Exception:
            return None

    # ---- zone lookups ----

    def zone_list(self) -> List[Dict]:
        """Return list of zones from LIST_ZONE.STB."""
        zones = []
        try:
            tbl = self.stb("LIST_ZONE.STB")
            for i, row in enumerate(tbl.data_rows):
                if not row or not row[0]:
                    continue
                zones.append({
                    "row": i + 1,
                    "name": row[0] if len(row) > 0 else "",
                    "folder": row[1] if len(row) > 1 else "",
                    "zon_file": row[2] if len(row) > 2 else "",
                    "minimap": row[4] if len(row) > 4 else "",
                })
        except Exception:
            pass
        return zones

    # ---- NPC lookups ----

    def npc_list(self) -> List[Dict]:
        """Return list of NPCs from LIST_NPC.STB."""
        npcs = []
        try:
            tbl = self.stb("LIST_NPC.STB")
            for i, row in enumerate(tbl.data_rows):
                if not row:
                    continue
                npcs.append({
                    "id": i + 1,
                    "name": row[1] if len(row) > 1 else "",
                    "chr_id": tbl.get_int(i + 1, 4),
                    "move_speed": tbl.get_int(i + 1, 5),
                    "attack_speed": tbl.get_int(i + 1, 6),
                    "level": tbl.get_int(i + 1, 7),
                    "hp": tbl.get_int(i + 1, 8),
                })
        except Exception:
            pass
        return npcs

    # ---- item lookups (general) ----

    def item_list(self, stb_name: str) -> List[Dict]:
        """Generic item table reader."""
        items = []
        try:
            tbl = self.stb(stb_name)
            header = tbl.header or []
            for i, row in enumerate(tbl.data_rows):
                item = {"id": i + 1}
                for j, val in enumerate(row):
                    col_name = header[j] if j < len(header) else f"col_{j}"
                    item[col_name] = val
                items.append(item)
        except Exception:
            pass
        return items

    # ---- chunk file resolution ----

    def chunk_files(self, zone_folder: str) -> List[Dict]:
        """Enumerate chunk files (HIM/TIL/IFO) for a zone folder."""
        chunks = []
        abs_zone = os.path.join(self.root, zone_folder.replace("\\", os.sep))
        if not os.path.isdir(abs_zone):
            return chunks

        for entry in os.listdir(abs_zone):
            if entry.upper().endswith(".HIM"):
                base = entry[:-4]
                parts = base.split("_")
                if len(parts) == 2:
                    try:
                        cx, cy = int(parts[0]), int(parts[1])
                    except ValueError:
                        continue
                    chunk = {"cx": cx, "cy": cy, "base": base}
                    for ext in (".HIM", ".TIL", ".IFO", ".MOV"):
                        fpath = os.path.join(abs_zone, base + ext)
                        chunk[ext[1:].lower()] = fpath if os.path.isfile(fpath) else None
                    chunks.append(chunk)

        chunks.sort(key=lambda c: (c["cx"], c["cy"]))
        return chunks
