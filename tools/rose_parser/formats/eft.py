"""EFT (ROSE effect) parser — validated against the engine loader
`loadEffect` in src/engine/src/zz_interface.cpp:6574 (read order transcribed
1:1; strings are int32-length-prefixed, NOT null-terminated).

An effect = a list of PARTICLE entries (each referencing a .PTL emitter file,
with a position/rotation offset, start delay and link flag) plus a list of
MESH-ANIMATION entries (morph meshes — parsed but currently unused by the UE
runtime).  Positions are in engine units (metres); rotations are D3D euler
pitch/yaw/roll in DEGREES.
"""
from dataclasses import dataclass, field
from typing import List

from ..reader import BinaryReader


def _str32(r: BinaryReader) -> str:
    n = r.i32()
    return r.read_bytes(n).decode("latin-1", errors="replace").rstrip("\x00")


@dataclass
class EftParticle:
    name: str = ""
    unique: str = ""
    ptl_path: str = ""           # 3DData\Effect\... .PTL
    use_motion: int = 0
    motion_path: str = ""        # optional ZMO moving the emitter node
    position: tuple = (0.0, 0.0, 0.0)   # engine metres
    rotation_deg: tuple = (0.0, 0.0, 0.0)  # (pitch, yaw, roll) D3D degrees
    delay_ms: int = 0
    is_link: int = 0             # 1 = follows the effect root, 0 = world


@dataclass
class EftMeshAni:
    name: str = ""
    mesh_path: str = ""
    motion_path: str = ""
    texture_path: str = ""
    use_alpha: int = 0
    two_side: int = 0
    src_blend: int = 0
    dest_blend: int = 0
    blend_op: int = 0
    position: tuple = (0.0, 0.0, 0.0)
    rotation_deg: tuple = (0.0, 0.0, 0.0)
    delay_ms: int = 0
    loop_count: int = 0
    is_link: int = 0


@dataclass
class EftFile:
    name: str = ""
    sound_path: str = ""
    sound_loops: int = 0
    particles: List[EftParticle] = field(default_factory=list)
    meshes: List[EftMeshAni] = field(default_factory=list)


def parse(path: str) -> EftFile:
    r = BinaryReader.from_file(path)
    eft = EftFile()
    eft.name = _str32(r)

    r.i32()                       # sound use flag (path presence is the signal)
    eft.sound_path = _str32(r)
    eft.sound_loops = r.i32()

    num_particles = r.i32()
    for _ in range(num_particles):
        p = EftParticle()
        p.name = _str32(r)
        p.unique = _str32(r)
        r.i32()                   # STB index (unused)
        p.ptl_path = _str32(r)
        p.use_motion = r.i32()
        p.motion_path = _str32(r)
        r.i32()                   # ani loop count
        r.i32()                   # STB index (again)
        p.position = r.vec3()
        pitch, yaw, roll = r.f32(), r.f32(), r.f32()
        r.f32()                   # unused 4th euler component
        p.rotation_deg = (pitch, yaw, roll)
        p.delay_ms = r.i32()
        p.is_link = r.i32()
        eft.particles.append(p)

    num_morph = r.i32()
    for _ in range(num_morph):
        m = EftMeshAni()
        m.name = _str32(r)
        _str32(r)                 # unique name
        r.i32()                   # STB index
        m.mesh_path = _str32(r)
        m.motion_path = _str32(r)
        m.texture_path = _str32(r)
        m.use_alpha = r.i32()
        m.two_side = r.i32()
        r.i32()                   # alpha test
        r.i32()                   # z test
        r.i32()                   # z write
        m.src_blend = r.i32()
        m.dest_blend = r.i32()
        m.blend_op = r.i32()
        r.i32()                   # use_animation
        _str32(r)                 # animation path
        r.i32()                   # ani loop count
        r.i32()                   # STB index
        m.position = r.vec3()
        pitch, yaw, roll = r.f32(), r.f32(), r.f32()
        r.f32()
        m.rotation_deg = (pitch, yaw, roll)
        m.delay_ms = r.i32()
        m.loop_count = r.i32()
        m.is_link = r.i32()
        eft.meshes.append(m)

    return eft
