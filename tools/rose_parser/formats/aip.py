"""AIP — ROSE monster AI script (little-endian binary).

Authority: src/common/shared/cai_file.h (struct layouts, natural alignment) +
cai_file.cpp (read order).  A file is:

    AI_FILE_HEADER { int numPatterns, second, attackMoveChance, numTitle }
    <numTitle bytes of title text — skipped>
    numPatterns × pattern:
        char szName[32]; int numEvents
        numEvents × event:
            char szEventName[32]; int numConds
            numConds × record; int numActs; numActs × record

Every condition/action record is self-describing: DWORD dwSize (total record
bytes incl. this head), DWORD Type (opcode: 0x040000nn cond / 0x0B0000nn act,
dispatch by low byte).  Unknown opcodes are skipped by dwSize.

Patterns are the 6 fixed triggers: 0 CREATED, 1 STOP (throttled by header
`second`), 2 ATTACKMOVE, 3 DAMAGED (%-gated by `attackMoveChance` when already
fighting), 4 KILL, 5 DEAD.  Event semantics: AND all conditions; first event
whose conditions all pass runs ALL its actions, then the pattern stops.

Distances are METERS in-file; the client multiplies by 100 at load (cm) for
cond low-bytes {3,4,5,9,19} and act low-bytes {4,5,7,9,12,15,17,19,21} — this
parser applies the same ×100 so consumers get engine units (cm).
"""
import struct
from dataclasses import dataclass, field
from typing import Dict, List

_X100_COND = {3, 4, 5, 9, 19}    # AICOND 02,03,04,08,18 (low byte = n+1)
_X100_ACT = {4, 5, 7, 9, 12, 15, 17, 19, 21}  # AIACT 03,04,06,08,11,14,16,18,20


@dataclass
class AipRecord:
    op: int                      # low-byte opcode (1-based table slot)
    fields: Dict = field(default_factory=dict)


@dataclass
class AipEvent:
    name: str
    conds: List[AipRecord] = field(default_factory=list)
    acts: List[AipRecord] = field(default_factory=list)


@dataclass
class AipPattern:
    name: str
    events: List[AipEvent] = field(default_factory=list)


@dataclass
class AIP:
    idle_sec: int = 5
    damaged_pct: int = 0
    patterns: List[AipPattern] = field(default_factory=list)


def _cstr(b: bytes) -> str:
    return b.split(b"\x00", 1)[0].decode("latin-1", errors="replace")


def _u8(b, o): return b[o]
def _i16(b, o): return struct.unpack_from("<h", b, o)[0]
def _u16(b, o): return struct.unpack_from("<H", b, o)[0]
def _i32(b, o): return struct.unpack_from("<i", b, o)[0]
def _u32(b, o): return struct.unpack_from("<I", b, o)[0]


def _decode_cond(op, b):
    f = {}
    try:
        if op == 1:                      # AICOND00 fight-state
            f = {"not_fight": _u8(b, 8)}
        elif op == 2:                    # 01 damage amount
            f = {"damage": _i32(b, 8), "give": _u8(b, 12)}
        elif op == 3:                    # 02 nearby char count (+select)
            f = {"dist": _i32(b, 8), "allied": _u8(b, 12),
                 "lvl_min": _i16(b, 14), "lvl_max": _i16(b, 16), "count": _u16(b, 18)}
        elif op == 4:                    # 03 self moved distance
            f = {"dist": _i32(b, 8)}
        elif op == 5:                    # 04 target distance
            f = {"dist": _i32(b, 8), "less": _u8(b, 12)}
        elif op == 6:                    # 05 ability diff vs target
            f = {"ab": _u8(b, 8), "diff": _i32(b, 12), "less": _u8(b, 16)}
        elif op == 7:                    # 06 self HP%
            f = {"hp_pct": _u32(b, 8), "less": _u8(b, 12)}
        elif op == 8:                    # 07 random %
            f = {"pct": _u8(b, 8)}
        elif op == 9:                    # 08 nearest char in range (+select)
            f = {"dist": _i32(b, 8), "lvl_min": _i16(b, 12),
                 "lvl_max": _i16(b, 14), "allied": _u8(b, 16)}
        elif op == 10:                   # 09 has target
            f = {}
        elif op == 11:                   # 10 ability compare vs target
            f = {"ab": _u8(b, 8), "less": _u8(b, 9)}
        elif op == 12:                   # 11 attacker ability vs value
            f = {"ab": _u8(b, 8), "value": _i32(b, 12), "less": _u8(b, 16)}
        elif op in (15, 16, 17, 29):     # 14/15/16/28 var compares (tagValueAI)
            f = {"var": _i16(b, 8), "value": _i32(b, 12), "cmp": _u8(b, 16)}
        elif op == 18:                   # 17 select local npc
            f = {"npc": _i32(b, 8)}
        elif op == 19:                   # 18 owner distance
            f = {"dist": _i32(b, 8), "cmp": _u8(b, 12)}
        elif op == 21:                   # 20 own ability check
            f = {"ab": _u8(b, 8), "value": _i32(b, 12), "cmp": _u8(b, 16)}
    except (struct.error, IndexError):
        f = {}
    if "dist" in f and op in _X100_COND:
        f["dist"] *= 100
    return f


def _decode_act(op, b):
    f = {}
    try:
        if op == 2:                      # 01 emotion
            f = {"action": _u8(b, 8)}
        elif op == 3:                    # 02 say message (inline string on disk)
            f = {"text": _cstr(b[8:])}
        elif op in (4, 5):               # 03/04 random move (cur/born)
            f = {"dist": _i32(b, 8), "run": _u8(b, 12)}
        elif op == 6:                    # 05 move to found char
            f = {"run": _u8(b, 8)}
        elif op == 7:                    # 06 attack by ability in range
            f = {"dist": _i32(b, 8), "ab": _u8(b, 12), "less": _u8(b, 13)}
        elif op == 9:                    # 08 flee by distance
            f = {"dist": _i32(b, 8), "run": _u8(b, 12)}
        elif op in (10, 11):             # 09/10 transform / summon pet
            f = {"monster": _u16(b, 8)}
        elif op == 12:                   # 11 call allies to gang target
            f = {"dist": _i32(b, 8), "count": _i32(b, 12)}
        elif op == 15:                   # 14 call same-type allies
            f = {"dist": _i32(b, 8)}
        elif op == 17:                   # 16 run away persistent
            f = {"dist": _i32(b, 8)}
        elif op == 18:                   # 17 drop item
            f = {"items": [_i16(b, 8 + 2 * i) for i in range(5)],
                 "to_owner": _i32(b, 20)}
        elif op == 19:                   # 18 call specific monster
            f = {"monster": _u16(b, 8), "count": _u16(b, 10), "dist": _i32(b, 12)}
        elif op == 21:                   # 20 summon monster near pos
            f = {"monster": _u16(b, 8), "pos": _u8(b, 10), "dist": _i32(b, 12)}
        elif op == 25:                   # 24 USE SKILL
            f = {"target": _u8(b, 8), "skill": _i16(b, 10), "motion": _i16(b, 12)}
        elif op in (26, 27, 28):         # 25/26/27 set vars (tagValueAI)
            f = {"var": _i16(b, 8), "value": _i32(b, 12), "cmp": _u8(b, 16)}
        elif op == 29:                   # 28 shout (inline string on disk)
            f = {"msg_type": _u8(b, 8), "text": _cstr(b[9:])}
        elif op == 31:                   # 30 named trigger
            n = _i16(b, 8)
            f = {"trigger": _cstr(b[10:10 + max(0, n)])}
        elif op == 33:                   # 32 zone PK
            f = {"zone": _i16(b, 8), "on": _u8(b, 10)}
    except (struct.error, IndexError):
        f = {}
    if "dist" in f and op in _X100_ACT:
        f["dist"] *= 100
    return f


def parse(path: str) -> AIP:
    data = open(path, "rb").read()
    o = 0
    n_pat, sec, dmg_pct, n_title = struct.unpack_from("<4i", data, o)
    o += 16 + n_title                    # skip the title blob

    aip = AIP(idle_sec=max(1, sec), damaged_pct=dmg_pct)
    for _ in range(n_pat):
        name = _cstr(data[o:o + 32]); o += 32
        (n_ev,) = struct.unpack_from("<i", data, o); o += 4
        pat = AipPattern(name=name)
        for _ in range(n_ev):
            ev_name = _cstr(data[o:o + 32]); o += 32
            (n_cond,) = struct.unpack_from("<i", data, o); o += 4
            ev = AipEvent(name=ev_name)
            for _ in range(n_cond):
                size, typ = struct.unpack_from("<II", data, o)
                rec = data[o:o + size]
                ev.conds.append(AipRecord(op=typ & 0xFF, fields=_decode_cond(typ & 0xFF, rec)))
                o += size
            (n_act,) = struct.unpack_from("<i", data, o); o += 4
            for _ in range(n_act):
                size, typ = struct.unpack_from("<II", data, o)
                rec = data[o:o + size]
                ev.acts.append(AipRecord(op=typ & 0xFF, fields=_decode_act(typ & 0xFF, rec)))
                o += size
            pat.events.append(ev)
        aip.patterns.append(pat)
    return aip
