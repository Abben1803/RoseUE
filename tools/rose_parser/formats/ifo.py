"""Parser for IFO (ROSE map info — per-chunk object/NPC/effect placement).

From rose-tools/rose-lib/src/files/ifo.rs (MapData):

File structure:
  uint32  block_count
  per block:
    uint32  block_type
    uint32  block_offset   (absolute file offset)

Block types:
  MapInfo       = 0   map_pos(vec2_i32) + zone_pos(vec2_i32) + 16 floats + cstring name
  Object        = 1   count + ObjectData[]
  Npc           = 2   count + (ObjectData + i32 ai + string_u8 file)[]
  Building      = 3   count + ObjectData[]
  Sound         = 4   count + (ObjectData + string_u8 file + i32 range + i32 interval)[]
  Effect        = 5   count + (ObjectData + string_u8 file)[]
  Animation     = 6   count + ObjectData[]
  Water         = 7   count + (u32 width + u32 height + (width*height)×(u8 has_water + f32 height))[]
  MonsterSpawn  = 8   count + (ObjectData + string_u8 name + spawns...)[]
  Ocean         = 9   f32 size + u32 patch_count + (vec3+vec3)[]
  Warp          = 10  count + ObjectData[]
  CollisionObj  = 11  count + ObjectData[]
  EventObject   = 12  count + (ObjectData + string_u8 function + string_u8 file)[]

ObjectData:
  string_u8  name
  int16      warp_id
  int16      event_id
  int32      object_type
  int32      object_id
  int32[2]   map_position (x, y)
  float4     rotation (w,x,y,z quaternion)
  float3     position
  float3     scale
"""
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple
from ..reader import BinaryReader

Vec3 = Tuple[float, float, float]
Vec4 = Tuple[float, float, float, float]

BLOCK_MAPINFO      = 0
BLOCK_OBJECT       = 1
BLOCK_NPC          = 2
BLOCK_BUILDING     = 3
BLOCK_SOUND        = 4
BLOCK_EFFECT       = 5
BLOCK_ANIMATION    = 6
BLOCK_WATER        = 7
BLOCK_MONSTER      = 8
BLOCK_OCEAN        = 9
BLOCK_WARP         = 10
BLOCK_COLLISION    = 11
BLOCK_EVENT_OBJECT = 12

# Keep old names for ue_placement.py compatibility
LUMP_MAPINFO           = BLOCK_MAPINFO
LUMP_TERRAIN_OBJECT    = BLOCK_OBJECT
LUMP_TERRAIN_MOB       = BLOCK_NPC
LUMP_TERRAIN_CNST      = BLOCK_BUILDING
LUMP_TERRAIN_SOUND     = BLOCK_SOUND
LUMP_TERRAIN_EFFECT    = BLOCK_EFFECT
LUMP_TERRAIN_MORPH     = BLOCK_ANIMATION
LUMP_TERRAIN_WATER     = BLOCK_WATER
LUMP_TERRAIN_REGEN     = BLOCK_MONSTER
LUMP_TERRAIN_OCEAN     = BLOCK_OCEAN
LUMP_TERRAIN_WARP      = BLOCK_WARP
LUMP_TERRAIN_COLLISION = BLOCK_COLLISION
LUMP_TERRAIN_EVENT_OBJECT = BLOCK_EVENT_OBJECT

LUMP_NAMES = {
    0: "mapinfo", 1: "object", 2: "npc", 3: "building",
    4: "sound", 5: "effect", 6: "animation", 7: "water",
    8: "monster_spawn", 9: "ocean", 10: "warp", 11: "collision",
    12: "event_object",
}


@dataclass
class IFOSpawnEntry:
    """One slot of a monster-spawn point's basic/tactic list (tagREGENMOB)."""
    name: str
    npc_id: int
    count: int


@dataclass
class IFOObject:
    name: str
    warp_id: int
    event_id: int
    obj_type: int
    obj_id: int
    map_x: int
    map_y: int
    rotation: Vec4
    position: Vec3
    scale: Vec3
    extra: str = ""  # extra string data (effect file, npc file, etc.)
    # NPC (BLOCK_NPC) payload — io_terrain.cpp LUMP_TERRAIN_MOB.
    npc_ai: int = 0
    npc_con: str = ""  # dialog CON filename ("" = no dialog)
    # MonsterSpawn (BLOCK_MONSTER) payload — CRegenPOINT::Load field order.
    # interval is SECONDS and spawn range METERS in the file (the server scales
    # ×1000 / ×100 at load).
    spawn_basic: List[IFOSpawnEntry] = field(default_factory=list)
    spawn_tactic: List[IFOSpawnEntry] = field(default_factory=list)
    spawn_interval: int = 0
    spawn_limit: int = 0
    spawn_range: int = 0
    spawn_tactic_points: int = 0


@dataclass
class IFOMapInfo:
    map_x: int = 0
    map_y: int = 0
    zone_x: int = 0
    zone_y: int = 0
    name: str = ""


@dataclass
class IFO:
    map_info: Optional[IFOMapInfo] = None
    objects: Dict[int, List[IFOObject]] = field(default_factory=dict)

    def get_lump(self, lump_type: int) -> List[IFOObject]:
        return self.objects.get(lump_type, [])

    @property
    def deco_objects(self) -> List[IFOObject]:
        return self.get_lump(BLOCK_OBJECT)

    @property
    def cnst_objects(self) -> List[IFOObject]:
        return self.get_lump(BLOCK_BUILDING)

    @property
    def mobs(self) -> List[IFOObject]:
        return self.get_lump(BLOCK_NPC)

    @property
    def effects(self) -> List[IFOObject]:
        return self.get_lump(BLOCK_EFFECT)

    @property
    def warps(self) -> List[IFOObject]:
        return self.get_lump(BLOCK_WARP)


def _read_object_data(r: BinaryReader) -> IFOObject:
    name = r.pascal_str_u8()
    warp_id  = r.i16()
    event_id = r.i16()
    obj_type = r.i32()
    obj_id   = r.i32()
    map_x    = r.i32()
    map_y    = r.i32()
    rotation = r.quat()
    position = r.vec3()
    scale    = r.vec3()
    return IFOObject(
        name=name, warp_id=warp_id, event_id=event_id,
        obj_type=obj_type, obj_id=obj_id,
        map_x=map_x, map_y=map_y,
        rotation=rotation, position=position, scale=scale,
    )


def _read_block(r: BinaryReader, block_type: int) -> List[IFOObject]:
    count = r.u32()
    result: List[IFOObject] = []

    for _ in range(count):
        obj = _read_object_data(r)

        if block_type == BLOCK_NPC:
            obj.npc_ai = r.i32()            # ai pattern id
            obj.extra = str(obj.npc_ai)     # (legacy consumers)
            obj.npc_con = r.pascal_str_u8() # dialog CON file (matches LIST_EVENT col 3 basename)

        elif block_type == BLOCK_SOUND:
            obj.extra = r.pascal_str_u8()   # sound file
            r.i32()                         # range
            r.i32()                         # interval

        elif block_type == BLOCK_EFFECT:
            obj.extra = r.pascal_str_u8()   # effect file

        elif block_type == BLOCK_MONSTER:
            name = r.pascal_str_u8()        # spawn point name
            obj.extra = name
            basic_count = r.u32()
            for _ in range(basic_count):
                obj.spawn_basic.append(IFOSpawnEntry(
                    name=r.pascal_str_u8(), npc_id=r.u32(), count=r.u32()))
            tactical_count = r.u32()
            for _ in range(tactical_count):
                obj.spawn_tactic.append(IFOSpawnEntry(
                    name=r.pascal_str_u8(), npc_id=r.u32(), count=r.u32()))
            obj.spawn_interval = r.u32()        # seconds
            obj.spawn_limit = r.u32()
            obj.spawn_range = r.u32()           # meters
            obj.spawn_tactic_points = r.u32()

        elif block_type == BLOCK_EVENT_OBJECT:
            r.pascal_str_u8()               # function name
            r.pascal_str_u8()               # file

        result.append(obj)

    return result


def parse(path: str) -> IFO:
    r = BinaryReader.from_file(path)
    ifo = IFO()

    block_count = r.u32()
    blocks: List[Tuple[int, int]] = []
    for _ in range(block_count):
        btype  = r.u32()
        offset = r.u32()
        blocks.append((btype, offset))

    for btype, offset in blocks:
        r.seek(offset)

        if btype == BLOCK_MAPINFO:
            mi = IFOMapInfo()
            mi.map_x, mi.map_y = r.i32(), r.i32()
            mi.zone_x, mi.zone_y = r.i32(), r.i32()
            r.skip(16 * 4)          # unused 4×4 float matrix
            mi.name = r.null_str()
            ifo.map_info = mi

        elif btype == BLOCK_OCEAN:
            _size = r.f32()
            patch_count = r.u32()
            for _ in range(patch_count):
                r.vec3()  # start
                r.vec3()  # end

        elif btype == BLOCK_WATER:
            count = r.u32()
            for _ in range(count):
                width  = r.u32()
                height = r.u32()
                for _ in range(width * height):
                    r.u8()   # has_water
                    r.f32()  # height

        else:
            objs = _read_block(r, btype)
            ifo.objects.setdefault(btype, []).extend(objs)

    return ifo
