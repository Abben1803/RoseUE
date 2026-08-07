"""Binary reader utility for ROSE Online file formats."""
import struct


class BinaryReader:
    def __init__(self, data: bytes):
        self._data = data
        self._pos = 0

    @classmethod
    def from_file(cls, path: str) -> "BinaryReader":
        with open(path, "rb") as f:
            return cls(f.read())

    def tell(self) -> int:
        return self._pos

    def seek(self, pos: int) -> None:
        self._pos = pos

    def skip(self, n: int) -> None:
        self._pos += n

    def remaining(self) -> int:
        return len(self._data) - self._pos

    def at_end(self) -> bool:
        return self._pos >= len(self._data)

    def peek_bytes(self, n: int) -> bytes:
        return self._data[self._pos: self._pos + n]

    # ---- primitive reads ----

    def read_bytes(self, n: int) -> bytes:
        v = self._data[self._pos: self._pos + n]
        self._pos += n
        return v

    def u8(self) -> int:
        v = self._data[self._pos]
        self._pos += 1
        return v

    def i8(self) -> int:
        v = struct.unpack_from("<b", self._data, self._pos)[0]
        self._pos += 1
        return v

    def u16(self) -> int:
        v = struct.unpack_from("<H", self._data, self._pos)[0]
        self._pos += 2
        return v

    def i16(self) -> int:
        v = struct.unpack_from("<h", self._data, self._pos)[0]
        self._pos += 2
        return v

    def u32(self) -> int:
        v = struct.unpack_from("<I", self._data, self._pos)[0]
        self._pos += 4
        return v

    def i32(self) -> int:
        v = struct.unpack_from("<i", self._data, self._pos)[0]
        self._pos += 4
        return v

    def f32(self) -> float:
        v = struct.unpack_from("<f", self._data, self._pos)[0]
        self._pos += 4
        return v

    def vec2(self) -> tuple:
        x, y = struct.unpack_from("<ff", self._data, self._pos)
        self._pos += 8
        return (x, y)

    def vec3(self) -> tuple:
        x, y, z = struct.unpack_from("<fff", self._data, self._pos)
        self._pos += 12
        return (x, y, z)

    def vec4(self) -> tuple:
        x, y, z, w = struct.unpack_from("<ffff", self._data, self._pos)
        self._pos += 16
        return (x, y, z, w)

    def quat(self) -> tuple:
        """Read quaternion as (w, x, y, z)."""
        w, x, y, z = struct.unpack_from("<ffff", self._data, self._pos)
        self._pos += 16
        return (w, x, y, z)

    def uvec4_u16(self) -> tuple:
        a, b, c, d = struct.unpack_from("<HHHH", self._data, self._pos)
        self._pos += 8
        return (a, b, c, d)

    def uvec4_u32(self) -> tuple:
        a, b, c, d = struct.unpack_from("<IIII", self._data, self._pos)
        self._pos += 16
        return (a, b, c, d)

    def uvec3_u16(self) -> tuple:
        a, b, c = struct.unpack_from("<HHH", self._data, self._pos)
        self._pos += 6
        return (a, b, c)

    def uvec3_u32(self) -> tuple:
        a, b, c = struct.unpack_from("<III", self._data, self._pos)
        self._pos += 12
        return (a, b, c)

    # ---- string reads ----

    def null_str(self, encoding: str = "latin-1") -> str:
        """Read a null-terminated string."""
        end = self._data.index(b"\x00", self._pos)
        s = self._data[self._pos: end].decode(encoding, errors="replace")
        self._pos = end + 1
        return s

    def pascal_str_u8(self, encoding: str = "latin-1") -> str:
        """Read a string prefixed with a uint8 length."""
        n = self.u8()
        raw = self._data[self._pos: self._pos + n]
        self._pos += n
        return raw.decode(encoding, errors="replace")

    def pascal_str_u16(self, encoding: str = "latin-1") -> str:
        """Read a string prefixed with a uint16 length."""
        n = self.u16()
        raw = self._data[self._pos: self._pos + n]
        self._pos += n
        return raw.decode(encoding, errors="replace")

    def fixed_str(self, n: int, encoding: str = "latin-1") -> str:
        """Read exactly n bytes and decode as string (strips trailing nulls)."""
        raw = self._data[self._pos: self._pos + n]
        self._pos += n
        return raw.rstrip(b"\x00").decode(encoding, errors="replace")

    def chr_str(self, encoding: str = "latin-1") -> str:
        """Read a CHR-style string: CStr::ReadString(fp, bIgnoreWhiteSpace=true)
        keeps interior whitespace (only leading whitespace and '"' are skipped)
        and terminates on the null byte.  zsc_str (which breaks on interior
        whitespace, i.e. bIgnoreWhiteSpace=false) desyncs on names with spaces
        — LIST_NPC.CHR has those."""
        chars = []
        got_char = False
        while not self.at_end():
            c = self.u8()
            if c == ord('"'):
                continue
            if c in (0x20, 0x09, 0x0D, 0x0A) and not got_char:
                continue
            if c == 0:
                break
            chars.append(c)
            got_char = True
        return bytes(chars).decode(encoding, errors="replace")

    def zsc_str(self, encoding: str = "latin-1") -> str:
        """Read a ZSC/CHR style string (null-terminated, skipping whitespace
        control chars the same way CGameStr::ReadString does)."""
        chars = []
        got_char = False
        while not self.at_end():
            c = self.u8()
            if c == ord('"'):
                continue
            if c in (0x20, 0x09, 0x0D, 0x0A):
                if got_char:
                    break
                continue
            if c == 0:
                break
            chars.append(c)
            got_char = True
        return bytes(chars).decode(encoding, errors="replace")
