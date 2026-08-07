"""Parser for CON (ROSE conversation script — 3DDATA/EVENT/*.CON).

Authority: src/client/event/cevent.cpp (CEvent::Load + Decode) and the
struct definitions at the top of that file.

File structure (little-endian, MSVC-aligned):
  SSC_FILE_HEADER (524 bytes):
    int16   event_mask               } bit i set -> event_funcs[i] valid
    char    func_names[16][32]       } Lua function per event index
    <2 bytes struct padding>
    uint32  conv_off                 } absolute offset of conversation data
    uint32  script_off               } absolute offset of Lua chunk
  SSC_CONV_HEADER (16 bytes, immediately after the file header):
    int32   msg_count,  uint32 msg_start_off
    int32   menu_count, uint32 menu_start_off
  Messages  (at conv_off+msg_start_off):  uint32 offsets[msg_count], each ->
    SSC_msg (80 bytes): int32 sn, int32 type, int32 value,
                        char func1[32], char func2[32], int32 str_id
  Menus     (at conv_off+menu_start_off): uint32 offsets[menu_count], each ->
    SSC_MENU_COLL: int32 length, int32 num_sub, then XOR-encoded body =
      uint32 sub_offsets[num_sub] (from collection start) -> SSC_msg items
    body XOR key: (num_sub if odd else length) & 0xff
  Lua chunk (at script_off): int32 len + XOR-encoded bytes,
    key: (len if odd else file_size) & 0xff.  Content = Lua 4.0 bytecode.

Item types (SC_MSG_*): 0 close, 1 next-message, 2 npc-say,
3 player-select, 4 jump-select.  Message text = str_id row of
ULNGTB_CON.LTB (see ltb.py).
"""
from dataclasses import dataclass, field
from typing import List
import struct

SC_MSG_CLOSE = 0
SC_MSG_NEXTMSG = 1
SC_MSG_NPCSAY = 2
SC_MSG_PLAYERSELECT = 3
SC_MSG_JUMPSELECT = 4

_HDR_SIZE = 524          # sizeof(SSC_FILE_HEADER) incl. 2-byte tail padding
_MSG_SIZE = 80           # sizeof(SSC_msg)


def _decode(buf: bytearray, v1: int, v2: int) -> bytearray:
    """CEvent::Decode — single-byte XOR (the loop key math is dead code)."""
    key = (v1 if (v1 & 1) else v2) & 0xFF
    for i in range(len(buf)):
        buf[i] ^= key
    return buf


def _cstr(raw: bytes) -> str:
    return raw.split(b"\0")[0].decode("ascii", "replace")


@dataclass
class ConItem:
    type: int = 0
    child: int = -1          # child menu index (dwValue)
    check_func: str = ""     # Lua gate — item shown only if it returns >=1
    click_func: str = ""     # Lua action on click
    str_id: int = 0          # row into ULNGTB_CON.LTB


@dataclass
class Con:
    event_funcs: List[str] = field(default_factory=lambda: [""] * 16)
    # message 0's check func gates the whole conversation; its click func
    # is the End() restart hook (CEvent::Start / CEvent::End).
    messages: List[ConItem] = field(default_factory=list)
    menus: List[List[ConItem]] = field(default_factory=list)
    lua_chunk: bytes = b""   # decoded Lua 4.0 bytecode


def _read_item(raw: bytes, base: int) -> ConItem:
    sn, typ, val = struct.unpack_from("<iii", raw, base)
    return ConItem(
        type=typ, child=val,
        check_func=_cstr(raw[base + 12:base + 44]),
        click_func=_cstr(raw[base + 44:base + 76]),
        str_id=struct.unpack_from("<i", raw, base + 76)[0],
    )


def parse(path: str) -> Con:
    with open(path, "rb") as f:
        raw = f.read()

    con = Con()
    event_mask, = struct.unpack_from("<h", raw, 0)
    for i in range(16):
        if event_mask & (1 << i):
            con.event_funcs[i] = _cstr(raw[2 + i * 32:2 + (i + 1) * 32])
    conv_off, script_off = struct.unpack_from("<II", raw, 516)

    msg_n, msg_start, menu_n, menu_start = struct.unpack_from(
        "<iIiI", raw, _HDR_SIZE)

    # messages
    offs = struct.unpack_from(f"<{msg_n}I", raw, conv_off + msg_start)
    for mo in offs:
        con.messages.append(_read_item(raw, conv_off + msg_start + mo))

    # menus
    offs = struct.unpack_from(f"<{menu_n}I", raw, conv_off + menu_start)
    for mo in offs:
        base = conv_off + menu_start + mo
        length, nsub = struct.unpack_from("<ii", raw, base)
        body = _decode(bytearray(raw[base + 8:base + length]), nsub, length)
        items: List[ConItem] = []
        subs = struct.unpack_from(f"<{nsub}I", body, 0)
        for so in subs:
            items.append(_read_item(bytes(body), so - 8))
        con.menus.append(items)

    # lua chunk
    lua_len, = struct.unpack_from("<i", raw, script_off)
    chunk = bytearray(raw[script_off + 4:script_off + 4 + lua_len])
    # EM62-020.CON stores its chunk UNENCODED — it already starts with the Lua
    # 4.0 signature, so applying the XOR would destroy it.  The client has the
    # same flaw (CEvent::Load calls Decode unconditionally), meaning ROSE itself
    # cannot run that script; sniffing the magic costs nothing and recovers it.
    if chunk[:5] != b"\x1bLua\x40":
        _decode(chunk, lua_len, len(raw))
    con.lua_chunk = bytes(chunk)

    return con
