#!/usr/bin/env python3
"""Таблица замеров памяти на ПК (x86), не на AD5M/AD5X."""
from pathlib import Path
import json
import sys

if len(sys.argv) != 3:
    sys.exit("Usage: python3 ci/compare-ui-memory.py <before-dir> <after-dir>")
before, after = map(Path, sys.argv[1:])
names = sorted({p.name for p in before.glob("*.json")} & {p.name for p in after.glob("*.json")})
if not names:
    sys.exit("No matching memory measurements")
print("### Память тестового процесса (ПК, x86, KB; не показатели принтера)")
print("| Сценарий | Показатель | До | После | Разница |")
print("|---|---|---:|---:|---:|")
for name in names:
    a = json.loads((before / name).read_text())
    b = json.loads((after / name).read_text())
    for key in ("VmRSS", "VmHWM", "RssAnon", "RssFile", "VmData", "smaps_Pss"):
        if key in a and key in b:
            print(f"| {name.removesuffix('.json')} | {key} | {a[key]} | {b[key]} | {b[key]-a[key]:+} |")
