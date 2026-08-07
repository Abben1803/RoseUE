"""Export HIM heightmap data to Unreal Engine Landscape-compatible formats.

UE Landscape import expects:
  - A 16-bit grayscale RAW file (little-endian uint16, row-major)
  - Width and height must be (n*128 + 1) such as 257, 513, 1009, etc.
  - Each chunk (65×65 vertices) can be imported individually as a component
    or stitched together for a full zone landscape.

ROSE coordinate notes:
  - Heights are in ROSE world units (centimeters when scale = 1)
  - Typical map: 65×65 vertex grid per chunk, 16×16 chunks per zone
  - Full zone vertex grid = (16×64+1) = 1025 × 1025 vertices
  - UE Landscape import at 1025×1025 = valid (1025 = 8*128 + 1)
"""
import os
import struct
from typing import List, Optional

from ..formats.him import HIM


def to_raw_r16(him: HIM, out_path: str,
               height_min: float = 0.0,
               height_max: float = 10000.0) -> None:
    """Write HIM heights as a UE R16 (uint16 grayscale) RAW file.

    Values are normalized to [0, 65535] across [height_min, height_max].
    UE reads these as row-major, top-to-bottom (north to south).
    ROSE stores rows bottom-to-top (south to north) — we flip on export.
    """
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    scale = 65535.0 / max(height_max - height_min, 1.0)

    rows = him.heights  # row 0 = south
    # UE expects north (top) first, so iterate reversed
    with open(out_path, "wb") as f:
        for row in reversed(rows):
            for h in row:
                val = int(max(0, min(65535, (h - height_min) * scale)))
                f.write(struct.pack("<H", val))


def stitch_zone(chunk_grid: List[List[Optional[HIM]]],
                out_path: str,
                height_min: float = 0.0,
                height_max: float = 10000.0) -> None:
    """Stitch multiple HIM chunks into a single RAW heightmap for a full zone.

    Args:
        chunk_grid: 2D list [row][col] of HIM objects (None = missing chunk).
                    row 0 = southernmost chunk row.
        out_path: output .raw path
    """
    if not chunk_grid or not chunk_grid[0]:
        raise ValueError("Empty chunk grid")

    num_chunk_rows = len(chunk_grid)
    num_chunk_cols = len(chunk_grid[0])

    sample = next((c for row in chunk_grid for c in row if c), None)
    if sample is None:
        raise ValueError("All chunks are None")

    chunk_h = sample.height
    chunk_w = sample.width

    # Total vertex count: chunks share edge vertices (overlap by 1)
    total_h = (chunk_h - 1) * num_chunk_rows + 1
    total_w = (chunk_w - 1) * num_chunk_cols + 1

    scale = 65535.0 / max(height_max - height_min, 1.0)

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        # Write north-to-south (reversed chunk rows, reversed vertex rows)
        for chunk_row in reversed(range(num_chunk_rows)):
            # For each vertex row within this chunk row (skip last row of
            # interior chunks since it overlaps with the next chunk)
            v_start = 0
            v_end   = chunk_h if chunk_row == 0 else chunk_h - 1
            for vr in reversed(range(v_start, v_end)):
                for chunk_col in range(num_chunk_cols):
                    chunk = chunk_grid[chunk_row][chunk_col]
                    # Skip last col of interior chunks (overlaps)
                    vc_end = chunk_w if chunk_col == num_chunk_cols - 1 else chunk_w - 1
                    for vc in range(0, vc_end):
                        if chunk and vr < chunk.height and vc < chunk.width:
                            h = chunk.heights[vr][vc]
                        else:
                            h = height_min
                        val = int(max(0, min(65535, (h - height_min) * scale)))
                        f.write(struct.pack("<H", val))
