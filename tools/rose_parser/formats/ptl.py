"""PTL (ROSE particle system) parser — validated against the engine loaders
`zz_particle_emitter::load` (src/engine/src/zz_particle_emitter.cpp:69) and
`zz_particle_event_sequence::Load` (zz_particle_event_sequence.cpp:839), event
payloads per zz_particle_event.cpp.

A PTL = N event SEQUENCES (emitters).  Distance-like fields (spawn dir, emit
radius, gravity, velocities, sizes) are stored in the file in CENTIMETRES —
the engine multiplies by ZZ_SCALE_IN (0.01) into metres; we keep the RAW cm
values, which are exactly UE units.  Event sizes are HALF-extents (the engine
renders quads spanning ±size).  Rotation events are in degrees.

Event types (EVENT_TYPE, zz_particle_event.h:62):
  1 size(4)  2 timer(2)  3 red(2)  4 green(2)  5 blue(2)  6 alpha(2)
  7 color(8) 8 velx(2)   9 vely(2) 10 velz(2)  11 velocity(6)
  12 texture(2)  13 rotation(2)
Every event starts with time_min/time_max (SECONDS into the particle's life —
the engine nails a random actual time in that range) and a fade byte
(1 = interpolate from the previous same-channel event to this one's value).

Blend modes are D3DBLEND values (SrcBlend/DestBlend): dest ONE(2) = additive,
dest INVSRCALPHA(6) = alpha blend.  Align type: 0 = camera billboard,
1 = world-aligned quad, 2 = billboard around the Z axis only.  Update coord
(zz_particle_event_sequence.h:90): 0 world, 1 local-world (position follows
the node), 2 local.
"""
from dataclasses import dataclass, field
from typing import List

from ..reader import BinaryReader

# floats each event type carries after (time_min, time_max, fade)
EVENT_PAYLOAD_FLOATS = {1: 4, 2: 2, 3: 2, 4: 2, 5: 2, 6: 2, 7: 8,
                        8: 2, 9: 2, 10: 2, 11: 6, 12: 2, 13: 2}


@dataclass
class PtlEvent:
    type: int = 0
    time_min: float = 0.0
    time_max: float = 0.0
    fade: bool = False
    values: List[float] = field(default_factory=list)   # payload (min.., max..)


@dataclass
class PtlSequence:
    name: str = ""
    life_min: float = 1.0
    life_max: float = 1.0
    emit_rate_min: float = 1.0
    emit_rate_max: float = 1.0
    loops: int = 0                 # 0/negative = endless
    spawn_dir_min: tuple = (0, 0, 0)   # cm/s
    spawn_dir_max: tuple = (0, 0, 0)
    emit_radius_min: tuple = (0, 0, 0)  # cm
    emit_radius_max: tuple = (0, 0, 0)
    gravity_min: tuple = (0, 0, 0)      # cm/s^2
    gravity_max: tuple = (0, 0, 0)
    texture: str = ""
    num_particles: int = 0
    align_type: int = 0
    update_coord: int = 0
    tex_w: int = 1
    tex_h: int = 1
    implement_type: int = 1
    dst_blend: int = 2
    src_blend: int = 5
    blend_op: int = 1
    events: List[PtlEvent] = field(default_factory=list)


@dataclass
class PtlFile:
    sequences: List[PtlSequence] = field(default_factory=list)


def _str32(r: BinaryReader) -> str:
    n = r.i32()
    return r.read_bytes(n).decode("latin-1", errors="replace").rstrip("\x00")


def parse(path: str) -> PtlFile:
    r = BinaryReader.from_file(path)
    ptl = PtlFile()
    count = r.u32()
    for _ in range(count):
        s = PtlSequence()
        s.name = _str32(r)
        s.life_min, s.life_max = r.f32(), r.f32()
        s.emit_rate_min, s.emit_rate_max = r.f32(), r.f32()
        s.loops = r.i32()
        s.spawn_dir_min = r.vec3()
        s.spawn_dir_max = r.vec3()
        s.emit_radius_min = r.vec3()
        s.emit_radius_max = r.vec3()
        s.gravity_min = r.vec3()
        s.gravity_max = r.vec3()
        s.texture = _str32(r)
        s.num_particles = r.u32()
        s.align_type = r.u32()
        s.update_coord = r.u32()
        s.tex_w = max(1, r.u32())
        s.tex_h = max(1, r.u32())
        s.implement_type = r.u32()
        s.dst_blend = r.u32()
        s.src_blend = r.u32()
        s.blend_op = r.u32()

        event_count = r.u32()
        for _ in range(event_count):
            e = PtlEvent()
            e.type = r.u32()
            e.time_min, e.time_max = r.f32(), r.f32()
            e.fade = r.u8() != 0
            n = EVENT_PAYLOAD_FLOATS.get(e.type)
            if n is None:
                raise ValueError(f"{path}: unknown PTL event type {e.type}")
            e.values = [r.f32() for _ in range(n)]
            s.events.append(e)

        ptl.sequences.append(s)
    return ptl
