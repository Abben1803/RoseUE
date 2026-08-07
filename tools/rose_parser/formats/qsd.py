"""Parser for QSD (ROSE quest trigger data — 3DDATA/QUESTDATA/*.QSD).

Authority: src/common/shared/io_quest.{h,cpp} —
CQuestDATA::Client_LoadDATA + CQuestTRIGGER::Client_Load and the
STR_COND_*/STR_REWD_* structs (MSVC-aligned; uiSize in the file INCLUDES
the 8-byte {uint32 size, int32 type} header; type is masked & 0xffff).

File structure (little-endian):
  uint32  file_size(ish)          (ignored by the loader)
  uint32  pattern_count
  int16   name_len + bytes        (QSD name)
  per pattern:
    uint32  trigger_count
    int16   name_len + bytes      (pattern/group name)
    per trigger:
      uint8   check_next          (1 -> chains into the following trigger)
      uint32  cond_count
      uint32  rewd_count
      int16   name_len + bytes    (TRIGGER NAME — the hash key quests use)
      cond_count  × entity { uint32 size, int32 type, payload[size-8] }
      rewd_count  × entity { ... }

Entities decode into dicts with a 'type' key; unknown payloads keep raw hex.
Trigger names hash via StrToHashKey but the port keys by name string.
"""
from dataclasses import dataclass, field
from typing import Dict, List
import struct


@dataclass
class QsdTrigger:
    name: str = ""
    check_next: int = 0
    conditions: List[dict] = field(default_factory=list)
    rewards: List[dict] = field(default_factory=list)


@dataclass
class Qsd:
    name: str = ""
    triggers: List[QsdTrigger] = field(default_factory=list)


def _quest_data(raw: bytes, off: int) -> dict:
    """STR_QUEST_DATA — 8 bytes: u16 varNo, u16 varType, i16 value, u8 op."""
    var_no, var_type, value, op = struct.unpack_from("<HHhB", raw, off)
    return {"varNo": var_no, "varType": var_type, "value": value, "op": op}


def _abil_data(raw: bytes, off: int) -> dict:
    """STR_ABIL_DATA — 12 bytes: i32 type, i32 value, u8 op."""
    typ, value, op = struct.unpack_from("<iiB", raw, off)
    return {"abilType": typ, "value": value, "op": op}


def _item_data(raw: bytes, off: int) -> dict:
    """STR_ITEM_DATA — 16 bytes: u32 itemSN, i32 where, i32 count, u8 op."""
    sn, where, cnt = struct.unpack_from("<Iii", raw, off)
    return {"itemSN": sn, "where": where, "count": cnt, "op": raw[off + 12]}


def _parse_cond(typ: int, raw: bytes) -> dict:
    """raw = full entity buffer (payload starts at offset 8)."""
    d: Dict = {"type": typ}
    u = struct.unpack_from
    if typ == 0:      # select quest by SN
        d["questSN"], = u("<i", raw, 8)
    elif typ in (1, 2):   # quest var checks (1: selected quest, 2: any)
        n, = u("<i", raw, 8)
        d["vars"] = [_quest_data(raw, 12 + i * 8) for i in range(n)]
    elif typ == 3:    # ability checks
        n, = u("<i", raw, 8)
        d["abils"] = [_abil_data(raw, 12 + i * 12) for i in range(n)]
    elif typ == 4:    # item checks
        n, = u("<i", raw, 8)
        d["items"] = [_item_data(raw, 12 + i * 16) for i in range(n)]
    elif typ == 5:    # party leader/level
        d["isLeader"] = raw[8]
        d["level"], = u("<i", raw, 12)
        d["reversed"] = raw[16]
    elif typ == 6:    # position within radius
        d["zone"], d["x"], d["y"], d["z"], d["radius"] = u("<5i", raw, 8)
    elif typ == 7:    # world time band
        d["time"], d["endTime"] = u("<II", raw, 8)
    elif typ == 8:    # quest elapsed time
        d["time"], = u("<I", raw, 8)
        d["op"] = raw[12]
    elif typ == 9:    # skill range
        d["skill1"], d["skill2"] = u("<ii", raw, 8)
        d["op"] = raw[16]
    elif typ == 10:   # random percent band
        d["lowPct"], d["highPct"] = raw[8], raw[9]
    elif typ == 11:   # npc/eventobj object var
        d["who"] = raw[8]
        d["varNo"], = u("<h", raw, 10)
        d["value"], = u("<i", raw, 12)
        d["op"] = raw[16]
    elif typ == 12:   # object position
        d["zone"], = u("<h", raw, 8)
        d["x"], d["y"], d["eventID"] = u("<iii", raw, 12)
    elif typ == 13:   # selected npc is
        d["npcNo"], = u("<i", raw, 8)
    elif typ == 14:   # quest switch
        d["sn"], = u("<h", raw, 8)
        d["op"] = raw[10]
    elif typ == 15:   # party member count band
        d["n1"], d["n2"] = u("<hh", raw, 8)
    elif typ == 16:   # object distance? (who + time band)
        d["who"] = raw[8]
        d["time"], d["endTime"] = u("<II", raw, 12)
    elif typ == 17:   # compare two npc object vars
        d["npc1"], d["var1"] = u("<ih", raw, 8)
        d["npc2"], d["var2"] = u("<ih", raw, 16)
        d["op"] = raw[24]
    elif typ in (18, 19):  # month-day / week-day time band
        d["day"] = raw[8]
        d["hour1"], d["min1"], d["hour2"], d["min2"] = raw[9], raw[10], raw[11], raw[12]
    elif typ == 20:   # team number band
        d["n1"], d["n2"] = u("<ii", raw, 8)
    elif typ == 21:   # object variable within radius
        d["selObjType"] = raw[8]
        d["radius"], = u("<i", raw, 12)
    elif typ == 22:   # random x/y ?
        d["x"], d["y"] = u("<HH", raw, 8)
    elif typ == 23:   # registered clan
        d["reg"] = raw[8]
    elif typ == 24:   # clan position
        d["pos"], = u("<h", raw, 8)
        d["op"] = raw[10]
    elif typ == 25:   # clan contribution
        d["cont"], = u("<h", raw, 8)
        d["op"] = raw[10]
    elif typ == 26:   # clan grade
        d["grade"], = u("<h", raw, 8)
        d["op"] = raw[10]
    elif typ == 27:   # clan point
        d["point"], = u("<h", raw, 8)
        d["op"] = raw[10]
    elif typ == 28:   # clan money
        d["money"], = u("<i", raw, 8)
        d["op"] = raw[12]
    elif typ == 29:   # clan member count
        d["count"], = u("<h", raw, 8)
        d["op"] = raw[10]
    elif typ == 30:   # clan skill range
        d["skill1"], d["skill2"] = u("<hh", raw, 8)
        d["op"] = raw[12]
    else:
        d["raw"] = raw[8:].hex()
    return d


def _parse_rewd(typ: int, raw: bytes) -> dict:
    d: Dict = {"type": typ}
    u = struct.unpack_from
    size = len(raw)
    if typ == 0:      # remove/add quest slot
        d["questSN"], = u("<i", raw, 8)
        d["op"] = raw[12]
    elif typ == 1:    # add/remove item
        d["itemSN"], = u("<I", raw, 8)
        d["op"] = raw[12]
        d["count"], = u("<h", raw, 14)
        d["partyOpt"] = raw[16]
    elif typ in (2, 4):   # set quest vars
        n, = u("<i", raw, 8)
        d["vars"] = [_quest_data(raw, 12 + i * 8) for i in range(n)]
    elif typ == 3:    # set abilities
        n, = u("<i", raw, 8)
        d["abils"] = [_abil_data(raw, 12 + i * 12) for i in range(n)]
        d["partyOpt"] = raw[12 + n * 12] if size > 12 + n * 12 else 0
    elif typ == 5:    # calculated exp/money/item reward (btEquation)
        d["target"] = raw[8]
        d["equation"] = raw[9]
        d["value"], d["itemSN"] = u("<ii", raw, 12)
        d["partyOpt"] = raw[20]
        d["itemOpt"], = u("<h", raw, 22)
    elif typ == 6:    # heal HP/MP percent
        d["hpPct"], d["mpPct"] = u("<ii", raw, 8)
        d["partyOpt"] = raw[16]
    elif typ == 7:    # teleport
        d["zone"], d["x"], d["y"] = u("<iii", raw, 8)
        d["partyOpt"] = raw[20]
    elif typ == 8:    # spawn monster
        (d["monsterSN"], d["howMany"]) = u("<ii", raw, 8)
        d["who"] = raw[16]
        (d["zone"], d["x"], d["y"], d["range"], d["team"]) = u("<5i", raw, 20)
    elif typ == 9:    # run next trigger by name
        n, = u("<h", raw, 8)
        d["nextTrigger"] = raw[10:10 + n].split(b"\0")[0].decode("ascii", "replace")
    elif typ == 10:   # reset quest switches group? (header only)
        pass
    elif typ == 11:   # set npc/eventobj object var
        d["who"] = raw[8]
        d["varNo"], = u("<h", raw, 10)
        d["value"], = u("<i", raw, 12)
        d["op"] = raw[16]
    elif typ == 12:   # message (str id into quest lang STB)
        d["msgType"] = raw[8]
        d["strID"], = u("<i", raw, 12)
    elif typ == 13:   # delayed trigger
        d["who"] = raw[8]
        d["sec"], = u("<i", raw, 12)
        n, = u("<h", raw, 16)
        d["trigger"] = raw[18:18 + n].split(b"\0")[0].decode("ascii", "replace")
    elif typ == 14:   # learn/forget skill
        d["op"] = raw[8]
        d["skillNo"], = u("<i", raw, 12)
    elif typ == 15:   # set quest switch
        d["sn"], = u("<h", raw, 8)
        d["op"] = raw[10]
    elif typ == 16:   # clear switch group
        d["groupSN"], = u("<h", raw, 8)
    elif typ == 17:   # server-side (give exp?) header only
        pass
    elif typ == 18:   # formatted message
        d["strID"], = u("<i", raw, 8)
        d["cnt"], = u("<h", raw, 12)
        d["data"] = raw[14:size].hex()
    elif typ == 19:   # zone team trigger
        d["zone"], d["team"] = u("<hh", raw, 8)
        n, = u("<h", raw, 12)
        d["trigger"] = raw[14:14 + n].split(b"\0")[0].decode("ascii", "replace")
    elif typ == 20:   # pvp zone flag
        d["noType"] = raw[8]
    elif typ == 21:   # revive position
        if size >= 16:
            d["x"], d["y"] = u("<ii", raw, 8)
        else:
            d["x"], d["y"] = u("<hh", raw, 8)
    elif typ == 22:   # set revive zone
        d["zone"], = u("<h", raw, 8)
        d["op"] = raw[10]
    elif typ == 23:   # header only
        pass
    elif typ == 24:   # clan money
        d["money"], = u("<i", raw, 8)
        d["op"] = raw[12]
    elif typ == 25:   # clan point
        d["point"], = u("<h", raw, 8)
        d["op"] = raw[10]
    elif typ == 26:   # clan skill
        d["skillNo"], = u("<h", raw, 8)
        d["op"] = raw[10]
    elif typ == 27:   # clan contribution
        d["cont"], = u("<h", raw, 8)
        d["op"] = raw[10]
    elif typ == 28:   # teleport nearby
        d["range"], = u("<i", raw, 8)
        d["zone"], = u("<h", raw, 12)
        d["x"], d["y"] = u("<ii", raw, 16)
    elif typ == 29:   # call script
        n, = u("<h", raw, 8)
        d["script"] = raw[10:10 + n].split(b"\0")[0].decode("ascii", "replace")
    elif typ == 30:   # header only
        pass
    elif typ == 31:   # monster kill counter var
        d["monsterSN"], d["compare"] = u("<ii", raw, 8)
        d["var"] = _quest_data(raw, 16)
    elif typ == 32:   # quest item drop chance
        d["itemSN"], = u("<I", raw, 8)
        d["compare"], = u("<i", raw, 12)
        d["partyOpt"] = raw[16]
    elif typ == 33:   # reward splitter (select-reward)
        d["splitter"], = u("<h", raw, 8)
    elif typ == 34:   # hide/show
        d["hide"] = raw[8]
    else:
        d["raw"] = raw[8:].hex()
    return d


def _pascal16(r_data: bytes, off: int):
    n, = struct.unpack_from("<h", r_data, off)
    s = r_data[off + 2:off + 2 + n].split(b"\0")[0].decode("ascii", "replace")
    return s, off + 2 + n


def parse(path: str) -> Qsd:
    with open(path, "rb") as f:
        raw = f.read()

    qsd = Qsd()
    _size, pattern_cnt = struct.unpack_from("<II", raw, 0)
    qsd.name, off = _pascal16(raw, 8)

    for _p in range(pattern_cnt):
        trigger_cnt, = struct.unpack_from("<I", raw, off)
        _group, off = _pascal16(raw, off + 4)

        for _t in range(trigger_cnt):
            trg = QsdTrigger()
            trg.check_next = raw[off]
            cond_cnt, rewd_cnt = struct.unpack_from("<II", raw, off + 1)
            trg.name, off = _pascal16(raw, off + 9)

            for _c in range(cond_cnt):
                esize, etype = struct.unpack_from("<Ii", raw, off)
                trg.conditions.append(
                    _parse_cond(etype & 0xFFFF, raw[off:off + esize]))
                off += esize
            for _r in range(rewd_cnt):
                esize, etype = struct.unpack_from("<Ii", raw, off)
                trg.rewards.append(
                    _parse_rewd(etype & 0xFFFF, raw[off:off + esize]))
                off += esize

            qsd.triggers.append(trg)

    return qsd
