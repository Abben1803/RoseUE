"""Parser for HIM (ROSE heightmap format, one per terrain chunk).

From src/client/io_terrain.cpp (CMAP::Load):
  int32 width           (= PATCH_COUNT_PER_MAP_AXIS * grid_cnt + 1, typically 65)
  int32 height
  int32 patch_grid_cnt  (grids per patch axis, typically 4)
  float patch_size      (world-space size of one grid cell)
  float[height][width]  heights, stored bottom-to-top (row 0 = south edge)
  # then patch section "PTH":
  char[4] section_tag   (pascal-string 4-byte read: length byte + 3 chars "PTH")
  int32   num_patches
  float[num_patches][2] patch_height_range (max, min)
  int32   num_quad_patches
  float[num_quad_patches][2] quad_height_range

PATCH_COUNT_PER_MAP_AXIS = 16  (16×16 patches per chunk)
Vertex grid = 65×65 per chunk (for default 4 grids/patch)
"""
from dataclasses import dataclass, field
from typing import List, Optional, Tuple
from ..reader import BinaryReader

PATCH_COUNT = 16


@dataclass
class PatchBounds:
    height_min: float
    height_max: float


@dataclass
class HIM:
    width: int
    height: int
    patch_grid_count: int
    patch_size: float
    heights: List[List[float]] = field(default_factory=list)
    patch_bounds: List[PatchBounds] = field(default_factory=list)

    def get_height(self, row: int, col: int) -> float:
        """row=0 is south (bottom) edge; col=0 is west edge."""
        return self.heights[row][col]

    def to_flat_list(self) -> List[float]:
        """Heights as a flat list row-major (south to north, west to east)."""
        result = []
        for row in self.heights:
            result.extend(row)
        return result


def parse(path: str) -> HIM:
    r = BinaryReader.from_file(path)

    width  = r.i32()
    height = r.i32()
    patch_grid_cnt = r.i32()
    patch_size     = r.f32()

    # heights stored bottom-to-top: loop from height-1 down to 0
    heights: List[List[float]] = [[] for _ in range(height)]
    for row in range(height - 1, -1, -1):
        heights[row] = [r.f32() for _ in range(width)]

    return HIM(
        width=width,
        height=height,
        patch_grid_count=patch_grid_cnt,
        patch_size=patch_size,
        heights=heights,
    )
