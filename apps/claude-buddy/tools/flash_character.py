#!/usr/bin/env python3
"""
Flash a prepped character pack via USB (pio run -t uploadfs).
Faster than the BLE drop target when you're iterating on a character.

Usage:
  python3 tools/flash_character.py characters/bufo
"""
import json, sys, shutil, subprocess
from pathlib import Path
from prep_character import safe_child

PROJECT = Path(__file__).resolve().parent.parent
DATA    = PROJECT / "data" / "characters"
CAP     = 1_800_000


def flash(src: Path) -> None:
    if not src.is_dir() or src.is_symlink():
        sys.exit(f"character pack must be one real directory: {src}")
    files = []
    for entry in src.iterdir():
        if entry.is_symlink() or not entry.is_file():
            sys.exit(f"character pack must be flat regular files: {entry.name}")
        safe_child(src, entry.name, "character filename")
        files.append(entry)
    manifest = src / "manifest.json"
    if manifest not in files:
        sys.exit(f"no manifest.json in {src} — run tools/prep_character.py first")
    name = json.loads(manifest.read_text())["name"]
    dst = safe_child(DATA, name)

    total = sum(file.stat().st_size for file in files)
    if total > CAP:
        sys.exit(f"{total:,} bytes — over the {CAP:,} LittleFS cap")

    # uploadfs flashes everything under data/; the firmware only reads one
    # character at a time, so a stale sibling just wastes partition space.
    if DATA.exists():
        if DATA.is_symlink():
            sys.exit(f"refusing symlinked staging directory: {DATA}")
        shutil.rmtree(DATA)
    dst.mkdir(parents=True)
    for file in files:
        shutil.copy2(file, dst / file.name)
    print(f"staged {name}: {total:,} bytes -> {dst}")

    subprocess.run(["pio", "run", "-t", "uploadfs"], cwd=PROJECT, check=True)
    print(f"\nflashed. on the stick: hold A -> settings -> species -> GIF")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    flash(Path(sys.argv[1]).resolve())
