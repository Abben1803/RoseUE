"""Parser for STB (String Table Binary) — the main ROSE game database format.

From rose-tools/rose-lib/src/files/stb.rs:
  char[4]    magic        "STB1"
  uint32     data_offset
  uint32     row_count    (stored = data_rows + 1)
  uint32     col_count    (stored = data_cols + 1)

  Gap (bytes 16..data_offset):
    uint32   row_height          editor hint, ignored
    uint16   root_col_width      editor hint, ignored
    uint16[col_count] col_widths editor hints, ignored
    string_u16  root_col_name    headers[0]  (the "ID" column label)
    string_u16[col_count-1] col_names  headers[1..col_count-1]
    string_u16  unknown          skipped
    string_u16[row_count-1] row_ids

  Data (at data_offset):
    string_u16[(row_count-1) x (col_count-1)] cells  row-major

Game indexing note (from stb_column_offset memory):
  get_*(row, col) skips the root column, so game col 0 == data[row][0].
"""
from dataclasses import dataclass, field
from typing import List, Optional
from ..reader import BinaryReader


@dataclass
class STB:
    headers: List[str] = field(default_factory=list)  # length = col_count (incl. root)
    row_ids: List[str] = field(default_factory=list)   # length = row_count - 1
    data: List[List[str]] = field(default_factory=list) # (row_count-1) × (col_count-1)

    # ---- compatibility aliases ----
    @property
    def header(self) -> Optional[List[str]]:
        return self.headers or None

    @property
    def data_rows(self) -> List[List[str]]:
        """Data rows without the row-ID column (game-style: col 0 = first data col)."""
        return self.data

    def num_rows(self) -> int:
        return len(self.data)

    def num_cols(self) -> int:
        """Number of data columns (excluding the root/ID column)."""
        return len(self.data[0]) if self.data else 0

    def get(self, row: int, col: int, default: str = "") -> str:
        """col 0 = first data column (matches game get_* indexing).

        NUL bytes are stripped: the STB pads its fixed-width string fields with
        them, and they are padding rather than data.  Left in, they travel into
        the generated CSVs and every later reader chokes -- `_csv.Error: line
        contains NUL` killed gen_skill_motion on 9 rows of skills.csv.
        """
        try:
            return self.data[row][col].replace("\x00", "")
        except IndexError:
            return default
        except AttributeError:          # non-str cell
            return self.data[row][col]

    def get_int(self, row: int, col: int, default: int = 0) -> int:
        try:
            v = self.data[row][col]
            return int(v) if v else default
        except (IndexError, ValueError, TypeError):
            return default

    def get_row_id(self, row: int) -> str:
        try:
            return self.row_ids[row]
        except IndexError:
            return ""

    def col_name(self, col: int) -> str:
        """Header label for data column col (col 0 → headers[1])."""
        try:
            return self.headers[col + 1]
        except IndexError:
            return ""


def parse(path: str) -> STB:
    r = BinaryReader.from_file(path)

    magic = r.fixed_str(4)
    if magic not in ("STB1", "STB0"):
        raise ValueError(f"Not an STB file: magic='{magic}' in {path}")

    data_offset = r.u32()
    row_count   = r.u32()   # stored = data_rows + 1
    col_count   = r.u32()   # stored = data_cols + 1

    # Skip editor display hints: row_height(u32) + root_col_width(u16) + col_count×u16
    r.skip(4 + 2 + col_count * 2)

    # Column headers
    headers: List[str] = []
    headers.append(r.pascal_str_u16())       # root col name (ID column)
    for _ in range(col_count - 1):
        headers.append(r.pascal_str_u16())  # data col names

    r.pascal_str_u16()  # unknown string, skip

    # Row IDs (one per data row)
    row_ids: List[str] = [r.pascal_str_u16() for _ in range(row_count - 1)]

    # Data cells
    r.seek(data_offset)
    data: List[List[str]] = []
    for _ in range(row_count - 1):
        row = [r.pascal_str_u16() for _ in range(col_count - 1)]
        data.append(row)

    return STB(headers=headers, row_ids=row_ids, data=data)
