"""Parser for ZON (ROSE zone definition — one per zone, defines terrain config).

From src/client/io_terrain.cpp (CTERRAIN::LoadZONE):

File structure (lump-based, same pattern as IFO):
  int32 lump_count
  per lump: int32 lump_type, int32 data_offset

Lump types (ZONE_LUMP_TYPE from io_terrain.h):
  LUMP_ZONE_INFO  = 0
  LUMP_EVENT_OBJECT = 1
  LUMP_ZONE_TILE  = 2
  LUMP_BRUSHES    = 3
  LUMP_ECONOMY    = 4

LUMP_ZONE_INFO data (ReadZoneINFO):
  int32  (skipped — unused field)
  int32  width
  int32  height
  int32  patch_grid_count  (grids per patch, typically 4)
  float  grid_size         (world units per grid cell, typically 250)
  int32  center_x          (center chunk X index in zone)
  int32  center_y

LUMP_EVENT_OBJECT data (ReadEventObjINFO):
  int32 count
  per event:
    float3 position
    uint8  name_len
    char[name_len] name

LUMP_ZONE_TILE data (ReadTileINFO):
  int32 tile_count
  per tile: uint8 name_len, char[name_len] texture_path

LUMP_BRUSHES data (read_brushes):
  int32 brush_count
  per brush (Brush struct):
    int32 texture1_id
    int32 texture2_id
    int32 texture1_offset
    int32 texture2_offset
    int32 is_blending
    int32 orientation
    int32 type

LUMP_ECONOMY data (ReadECONOMY):
  uint8 name_len, char[name_len] zone_name
  int32 is_dungeon
  uint8 music_len, char[music_len] music_file
  uint8 sky_len,   char[sky_len]   sky_name
"""
from dataclasses import dataclass, field
from typing import List, Optional, Tuple
from ..reader import BinaryReader

Vec3 = Tuple[float, float, float]

LUMP_ZONE_INFO    = 0
LUMP_EVENT_OBJECT = 1
LUMP_ZONE_TILE    = 2
LUMP_BRUSHES      = 3
LUMP_ECONOMY      = 4


@dataclass
class ZoneInfo:
    width: int
    height: int
    patch_grid_count: int
    grid_size: float
    center_x: int
    center_y: int

    @property
    def patch_size(self) -> float:
        return self.grid_size * self.patch_grid_count


@dataclass
class EventObject:
    position: Vec3
    name: str


@dataclass
class Brush:
    texture1_id: int
    texture2_id: int
    texture1_offset: int
    texture2_offset: int
    is_blending: bool
    orientation: int
    type: int


@dataclass
class ZON:
    zone_info: Optional[ZoneInfo] = None
    event_objects: List[EventObject] = field(default_factory=list)
    tile_textures: List[str] = field(default_factory=list)
    brushes: List[Brush] = field(default_factory=list)
    zone_name: str = ""
    is_dungeon: bool = False
    music_file: str = ""
    sky_name: str = ""


def _read_zone_info(r: BinaryReader, offset: int) -> ZoneInfo:
    r.seek(offset)
    _unused = r.i32()
    width  = r.i32()
    height = r.i32()
    patch_grid_cnt = r.i32()
    grid_size      = r.f32()
    center_x = r.i32()
    center_y = r.i32()
    return ZoneInfo(
        width=width,
        height=height,
        patch_grid_count=patch_grid_cnt,
        grid_size=grid_size,
        center_x=center_x,
        center_y=center_y,
    )


def _read_event_objects(r: BinaryReader, offset: int) -> List[EventObject]:
    r.seek(offset)
    count = r.i32()
    events: List[EventObject] = []
    for _ in range(count):
        pos = r.vec3()
        name_len = r.u8()
        name = r.fixed_str(name_len) if name_len > 0 else ""
        events.append(EventObject(position=pos, name=name))
    return events


def _read_tile_textures(r: BinaryReader, offset: int) -> List[str]:
    r.seek(offset)
    count = r.i32()
    textures: List[str] = []
    for _ in range(count):
        name_len = r.u8()
        path = r.fixed_str(name_len) if name_len > 0 else ""
        textures.append(path)
    return textures


def _read_brushes(r: BinaryReader, offset: int) -> List[Brush]:
    r.seek(offset)
    count = r.i32()
    brushes: List[Brush] = []
    for _ in range(count):
        t1_id  = r.i32()
        t2_id  = r.i32()
        t1_off = r.i32()
        t2_off = r.i32()
        blend  = r.i32()
        orient = r.i32()
        btype  = r.i32()
        brushes.append(Brush(
            texture1_id=t1_id, texture2_id=t2_id,
            texture1_offset=t1_off, texture2_offset=t2_off,
            is_blending=bool(blend), orientation=orient, type=btype,
        ))
    return brushes


def _read_economy(r: BinaryReader, offset: int) -> dict:
    r.seek(offset)
    name_len  = r.u8()
    zone_name = r.fixed_str(name_len) if name_len > 0 else ""
    is_dungeon = r.i32()
    music_len  = r.u8()
    music_file = r.fixed_str(music_len) if music_len > 0 else ""
    sky_len    = r.u8()
    sky_name   = r.fixed_str(sky_len) if sky_len > 0 else ""
    return {"zone_name": zone_name, "is_dungeon": bool(is_dungeon),
            "music_file": music_file, "sky_name": sky_name}


def parse(path: str) -> ZON:
    r = BinaryReader.from_file(path)
    zon = ZON()

    lump_count = r.i32()
    lumps: List[Tuple[int, int]] = []
    for _ in range(lump_count):
        ltype  = r.i32()
        offset = r.i32()
        lumps.append((ltype, offset))

    for ltype, offset in lumps:
        if ltype == LUMP_ZONE_INFO:
            zon.zone_info = _read_zone_info(r, offset)
        elif ltype == LUMP_EVENT_OBJECT:
            zon.event_objects = _read_event_objects(r, offset)
        elif ltype == LUMP_ZONE_TILE:
            zon.tile_textures = _read_tile_textures(r, offset)
        elif ltype == LUMP_BRUSHES:
            zon.brushes = _read_brushes(r, offset)
        elif ltype == LUMP_ECONOMY:
            eco = _read_economy(r, offset)
            zon.zone_name  = eco["zone_name"]
            zon.is_dungeon = eco["is_dungeon"]
            zon.music_file = eco["music_file"]
            zon.sky_name   = eco["sky_name"]

    return zon
