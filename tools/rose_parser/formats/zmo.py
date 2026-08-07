"""Parser for ZMO (ROSE animation/motion format).

From src/engine/src/zz_motion.cpp:
  null-str magic "ZMO0002"
  uint32 fps
  uint32 num_frames
  uint32 num_channels
  per channel header:
    uint32 channel_type  (bitmask from ZZ_CTYPE_* constants)
    uint32 refer_id      (bone index or vertex group index)
  per frame (0..num_frames-1):
    per channel:
      POSITION/NORMAL: float3
      ROTATION:        float4 (w,x,y,z)
      ALPHA/TEXTUREANIM/SCALE: float
      UV0..UV3:        float2

Channel type constants (src/engine/include/zz_channel.h):
  ZZ_CTYPE_POSITION    = 1 << 1  = 2
  ZZ_CTYPE_ROTATION    = 1 << 2  = 4
  ZZ_CTYPE_NORMAL      = 1 << 3  = 8
  ZZ_CTYPE_ALPHA       = 1 << 4  = 16
  ZZ_CTYPE_UV0         = 1 << 5  = 32
  ZZ_CTYPE_UV1         = 1 << 6  = 64
  ZZ_CTYPE_UV2         = 1 << 7  = 128
  ZZ_CTYPE_UV3         = 1 << 8  = 256
  ZZ_CTYPE_TEXTUREANIM = 1 << 9  = 512
  ZZ_CTYPE_SCALE       = 1 << 10 = 1024
"""
from dataclasses import dataclass, field
from typing import List, Tuple, Union
from ..reader import BinaryReader

CTYPE_POSITION    = 1 << 1
CTYPE_ROTATION    = 1 << 2
CTYPE_NORMAL      = 1 << 3
CTYPE_ALPHA       = 1 << 4
CTYPE_UV0         = 1 << 5
CTYPE_UV1         = 1 << 6
CTYPE_UV2         = 1 << 7
CTYPE_UV3         = 1 << 8
CTYPE_TEXTUREANIM = 1 << 9
CTYPE_SCALE       = 1 << 10

CTYPE_NAMES = {
    CTYPE_POSITION:    "POSITION",
    CTYPE_ROTATION:    "ROTATION",
    CTYPE_NORMAL:      "NORMAL",
    CTYPE_ALPHA:       "ALPHA",
    CTYPE_UV0:         "UV0",
    CTYPE_UV1:         "UV1",
    CTYPE_UV2:         "UV2",
    CTYPE_UV3:         "UV3",
    CTYPE_TEXTUREANIM: "TEXTUREANIM",
    CTYPE_SCALE:       "SCALE",
}

FrameData = Union[
    Tuple[float, float, float],        # position / normal
    Tuple[float, float, float, float], # rotation (w,x,y,z)
    Tuple[float, float],               # uv
    float,                             # alpha / textureanim / scale
]


@dataclass
class Channel:
    channel_type: int
    refer_id: int
    frames: List[FrameData] = field(default_factory=list)

    @property
    def type_name(self) -> str:
        return CTYPE_NAMES.get(self.channel_type, f"UNKNOWN({self.channel_type})")


@dataclass
class ZMO:
    fps: int
    num_frames: int
    channels: List[Channel] = field(default_factory=list)

    def duration_seconds(self) -> float:
        return self.num_frames / max(self.fps, 1)

    def channels_for_bone(self, bone_id: int) -> List[Channel]:
        return [c for c in self.channels if c.refer_id == bone_id]


def _read_frame_data(r: BinaryReader, ctype: int) -> FrameData:
    if ctype == CTYPE_POSITION or ctype == CTYPE_NORMAL:
        return r.vec3()
    elif ctype == CTYPE_ROTATION:
        return r.quat()
    elif ctype in (CTYPE_UV0, CTYPE_UV1, CTYPE_UV2, CTYPE_UV3):
        return r.vec2()
    elif ctype in (CTYPE_ALPHA, CTYPE_TEXTUREANIM, CTYPE_SCALE):
        return r.f32()
    else:
        raise ValueError(f"Unknown channel type: {ctype}")


def parse(path: str) -> ZMO:
    r = BinaryReader.from_file(path)

    magic = r.null_str()
    if magic != "ZMO0002":
        raise ValueError(f"Unknown ZMO version: '{magic}' in {path}")

    fps = r.u32()
    num_frames = r.u32()
    num_channels = r.u32()

    channels: List[Channel] = []
    for _ in range(num_channels):
        ctype = r.u32()
        refer_id = r.u32()
        channels.append(Channel(channel_type=ctype, refer_id=refer_id))

    for _frame in range(num_frames):
        for ch in channels:
            ch.frames.append(_read_frame_data(r, ch.channel_type))

    return ZMO(fps=fps, num_frames=num_frames, channels=channels)
