"""Parser for STL (ROSE localized String Table) files.

Authority: rose-tools/rose-lib/src/files/stl.rs (and src/client/gamecommon/
stringmanager.cpp).  The on-disk layout is a key table followed by per-language
offset tables that point at the actual strings:

    string_u8                identifier   "NRST01" | "ITST01" | "QEST01"
    u32  row_count                        (number of keys)
    repeat row_count:
        string_u8  key.name
        u32        key.id
    u32  language_count
    repeat language_count:
        u32  language_offset   -> seek here for this language's row-offset table
        repeat row_count:
            u32  row_offset    -> seek here for this row's string(s)
            <strings, varbyte-length-prefixed; count depends on format>

Formats: Normal=1 string (text); Item=2 (text, description); Quest=4.
String length is varbyte: low byte; if high bit set, a second byte extends it
((b1 << 7) | (b0 & 0x7f)); the stored bytes include a trailing NUL.

The item-name tables we care about (LIST_<TYPE>_S.STL) are the Item format:
text = item name, description = tooltip text.
"""
from dataclasses import dataclass, field
from typing import Dict, List
from ..reader import BinaryReader

# Language index -> name (rose-lib StringTableLanguage).
_LANGS = {0: "Korean", 1: "English", 2: "Japanese",
          3: "ChineseTraditional", 4: "ChineseSimplified"}

# How many strings per row, by format identifier.
_FORMAT_FIELDS = {"NRST01": 1, "ITST01": 2, "QEST01": 4}


@dataclass
class STLRow:
    key: str
    values: Dict[str, str] = field(default_factory=dict)    # lang name -> text
    comments: Dict[str, str] = field(default_factory=dict)  # lang name -> description
    # Quest (QEST01) fields 3/4 — start/end quest messages.
    start_msgs: Dict[str, str] = field(default_factory=dict)
    end_msgs: Dict[str, str] = field(default_factory=dict)


@dataclass
class STL:
    format: str = ""
    languages: List[str] = field(default_factory=list)
    rows: List[STLRow] = field(default_factory=list)
    _key_index: Dict[str, int] = field(default_factory=dict)

    def _lookup(self, store: str, key: str, lang: str) -> str:
        idx = self._key_index.get(key)
        if idx is None:
            return ""
        row = self.rows[idx]
        d = getattr(row, store)
        if lang:
            return d.get(lang, "")
        # Prefer English, then the first non-empty language.
        if d.get("English"):
            return d["English"]
        for l in self.languages:
            if d.get(l):
                return d[l]
        return ""

    def get(self, key: str, lang: str = "") -> str:
        """Localized name (text) for a key; prefers English."""
        return self._lookup("values", key, lang)

    def get_desc(self, key: str, lang: str = "") -> str:
        """Localized description for a key; prefers English. Empty for Normal STLs."""
        return self._lookup("comments", key, lang)

    def get_start_msg(self, key: str, lang: str = "") -> str:
        """Quest STL only: the quest-started message."""
        return self._lookup("start_msgs", key, lang)

    def get_end_msg(self, key: str, lang: str = "") -> str:
        """Quest STL only: the quest-completed message."""
        return self._lookup("end_msgs", key, lang)


def _read_varbyte(r: BinaryReader, encoding: str = "utf-8") -> str:
    b0 = r.u8()
    if b0 & 0x80:
        b1 = r.u8()
        n = (b1 << 7) | (b0 & 0x7f)
    else:
        n = b0
    raw = r.read_bytes(n)
    if raw.endswith(b"\x00"):
        raw = raw[:-1]
    try:
        return raw.decode(encoding)
    except UnicodeDecodeError:
        return raw.decode("latin-1", errors="replace")


def parse(path: str) -> STL:
    r = BinaryReader.from_file(path)
    stl = STL()

    stl.format = r.pascal_str_u8()
    nfields = _FORMAT_FIELDS.get(stl.format, 1)

    row_count = r.u32()
    keys: List[str] = []
    for _ in range(row_count):
        name = r.pascal_str_u8()
        _id = r.u32()
        keys.append(name)

    for i, name in enumerate(keys):
        stl.rows.append(STLRow(key=name))
        stl._key_index[name] = i

    lang_count = r.u32()
    for lang_idx in range(lang_count):
        lang_name = _LANGS.get(lang_idx, f"Lang{lang_idx}")
        stl.languages.append(lang_name)

        language_offset = r.u32()
        next_language_pos = r.tell()
        r.seek(language_offset)

        for row_idx in range(row_count):
            row_offset = r.u32()
            next_row_pos = r.tell()
            r.seek(row_offset)

            text = _read_varbyte(r)
            stl.rows[row_idx].values[lang_name] = text
            if nfields >= 2:
                stl.rows[row_idx].comments[lang_name] = _read_varbyte(r)
            if nfields >= 4:
                stl.rows[row_idx].start_msgs[lang_name] = _read_varbyte(r)
                stl.rows[row_idx].end_msgs[lang_name] = _read_varbyte(r)

            if row_idx < row_count - 1:
                r.seek(next_row_pos)

        if lang_idx < lang_count - 1:
            r.seek(next_language_pos)

    return stl
