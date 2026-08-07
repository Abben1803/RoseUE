"""Parser for LTB (ROSE language string table — ULNGTB_CON/QST/AI.LTB).

Authority: src/client/gamecommon/lngtbl.cpp AStringTable::Open/Load/GetWString.

File structure (little-endian):
  int32  col_count       (columns = languages)
  int32  row_count
  per row × col:  { int32 file_pos, int16 str_len }   (str_len in WCHARs incl. NUL?)
  ... UTF-16LE string data at absolute file_pos

GetWString returns the wide string at file_pos; strings are NUL-terminated
UTF-16LE (str_len counts WCHARs of the stored string).
"""
from dataclasses import dataclass, field
from typing import List, Optional
import struct


@dataclass
class LTB:
    col_count: int = 0
    row_count: int = 0
    # rows[row][col] -> str
    rows: List[List[str]] = field(default_factory=list)

    def get(self, row: int, col: int) -> str:
        if 0 <= row < self.row_count and 0 <= col < self.col_count:
            return self.rows[row][col]
        return ""


def parse(path: str) -> LTB:
    with open(path, "rb") as f:
        data = f.read()

    col_count, row_count = struct.unpack_from("<ii", data, 0)
    ltb = LTB(col_count=col_count, row_count=row_count)

    off = 8
    for _r in range(row_count):
        row: List[str] = []
        for _c in range(col_count):
            pos, slen = struct.unpack_from("<ih", data, off)
            off += 6
            if slen <= 0 or pos <= 0:
                row.append("")
                continue
            raw = data[pos:pos + slen * 2]
            s = raw.decode("utf-16-le", "replace")
            # stored strings may carry a trailing NUL
            row.append(s.split("\x00")[0])
        ltb.rows.append(row)

    return ltb
