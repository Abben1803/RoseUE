"""Export STB game data tables to CSV and JSON for Unreal Engine DataTable import."""
import csv
import json
import os
from typing import Dict, List, Optional
from ..formats.stb import STB


def to_csv(stb: STB, out_path: str) -> None:
    """Write an STB to CSV. Row 0 becomes the header row."""
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        for row in stb.data_rows:
            writer.writerow(row)


def to_json(stb: STB, out_path: str, row_id_col: int = 0) -> None:
    """Write an STB to JSON array with column headers as keys.

    Args:
        stb: parsed STB instance
        out_path: output .json path
        row_id_col: which column to use as the row 'Name' (UE DataTable row ID)
    """
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    header = stb.header or []
    col_names = [header[i] if i < len(header) else f"Col_{i}"
                 for i in range(stb.num_cols())]

    if not col_names[0]:
        col_names[0] = "ID"

    records: List[Dict] = []
    for row in stb.data_rows:
        record: Dict = {}
        for i, val in enumerate(row):
            col = col_names[i] if i < len(col_names) else f"Col_{i}"
            record[col] = val
        records.append(record)

    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(records, f, indent=2, ensure_ascii=False)


def batch_export(stb_dir: str, out_dir: str, fmt: str = "csv") -> List[str]:
    """Export all STB files in a directory.

    Returns list of output paths created.
    """
    from ..formats import stb as stb_mod

    outputs = []
    for filename in os.listdir(stb_dir):
        if not filename.upper().endswith(".STB"):
            continue
        in_path  = os.path.join(stb_dir, filename)
        out_name = os.path.splitext(filename)[0] + ("." + fmt)
        out_path = os.path.join(out_dir, out_name)
        try:
            data = stb_mod.parse(in_path)
            if fmt == "csv":
                to_csv(data, out_path)
            else:
                to_json(data, out_path)
            outputs.append(out_path)
        except Exception as e:
            print(f"  [warn] {filename}: {e}")
    return outputs
