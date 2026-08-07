"""Parser for TIL (ROSE tilemap format, one per terrain chunk).

From src/client/io_terrain.cpp (CMAP::Load, TIL section):
  int32 width   (= PATCH_COUNT_PER_MAP_AXIS = 16)
  int32 height
  per cell (bottom-to-top, left-to-right):
    uint8  brush_no
    uint8  tile_idx
    uint8  tile_set
    int32  tile_no   (material/tile ID into ZON tile list)

Brush references: ZON LUMP_BRUSHES defines Brush records.
  tile_no is the index into the ZON tile table used to look up texture layers.
"""
from dataclasses import dataclass, field
from typing import List, Tuple
from ..reader import BinaryReader


@dataclass
class TileCell:
    brush_no: int
    tile_idx: int
    tile_set: int
    tile_no: int


@dataclass
class TIL:
    width: int
    height: int
    cells: List[List[TileCell]] = field(default_factory=list)

    def get(self, row: int, col: int) -> TileCell:
        """row=0 is south edge."""
        return self.cells[row][col]


def parse(path: str) -> TIL:
    r = BinaryReader.from_file(path)

    width  = r.i32()
    height = r.i32()

    cells: List[List[TileCell]] = [[] for _ in range(height)]
    for row in range(height - 1, -1, -1):
        for _col in range(width):
            brush_no = r.u8()
            tile_idx = r.u8()
            tile_set = r.u8()
            tile_no  = r.i32()
            cells[row].append(TileCell(
                brush_no=brush_no,
                tile_idx=tile_idx,
                tile_set=tile_set,
                tile_no=tile_no,
            ))

    return TIL(width=width, height=height, cells=cells)
