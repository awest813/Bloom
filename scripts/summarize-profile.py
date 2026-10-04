#!/usr/bin/env python3
"""Summarize complete Flycast diagnostic intervals from one labeled scene."""
import argparse
import csv
import json

PARTS = ("other", "ps1", "draw", "sound", "video", "mdec", "disc",
         "interpreter", "geometry")


def summarize(rows):
    if not rows:
        raise ValueError("No intervals selected")
    parts = PARTS + (("io",) if "io_us" in rows[0] else ())
    previous = 0
    for row in rows:
        if ("io_us" in row) != ("io" in parts):
            raise ValueError("Mixed profile layouts")
        sequence = row["sequence"]
        if sequence <= previous or sequence % 2:
            raise ValueError("Repeated, reordered, or unfinished interval")
        if row["duration_us"] <= 0 or any(value < 0 for value in row.values()):
            raise ValueError("Invalid counter")
        if sum(row[name + "_us"] for name in parts) != row["duration_us"]:
            raise ValueError("Incomplete time partition")
        previous = sequence
    duration = sum(row["duration_us"] for row in rows)
    return {
        "intervals": len(rows),
        "sequence_range": [rows[0]["sequence"], rows[-1]["sequence"]],
        "emulated_seconds": duration / 1e6,
        "presentation_fps": sum(r["presentations"] for r in rows) * 1e6 / duration,
        "vblanks_per_second": sum(r["vblanks"] for r in rows) * 1e6 / duration,
        "exclusive_percent": {name: sum(r[name + "_us"] for r in rows) * 100 / duration
                              for name in parts},
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv")
    parser.add_argument("--first", type=int, required=True, help="First included sequence")
    parser.add_argument("--last", type=int, required=True, help="Last included sequence")
    parser.add_argument("--scene", required=True, help="Visually verified scene label")
    args = parser.parse_args()
    with open(args.csv, newline="", encoding="utf-8") as source:
        rows = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(source)]
    selected = [r for r in rows if args.first <= r["sequence"] <= args.last]
    result = summarize(selected)
    result.update(scene=args.scene, source=args.csv)
    print(json.dumps(result, indent=2))
