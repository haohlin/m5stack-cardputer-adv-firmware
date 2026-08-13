#!/usr/bin/env python3
"""
Prep a character pack: downscale GIFs to 96px with a CONSISTENT crop
across all states, so the character is the same size in every animation.
Writes to characters/<name>/ ready to drag onto the Hardware Buddy window.

Usage:
  python3 tools/prep_character.py <character-dir-or-zip>
"""
import json, re, stat, sys, shutil, tempfile, zipfile
from pathlib import Path
from PIL import Image, ImageSequence

TARGET_W = 96
REF_W    = 1000   # normalize to this before computing the cross-state bbox
PROJECT  = Path(__file__).resolve().parent.parent
OUT_ROOT = PROJECT / "characters"
SAFE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*\Z")
MAX_ZIP_MEMBERS = 128
MAX_ZIP_COMPRESSED_BYTES = 4 * 1024 * 1024
MAX_ZIP_EXPANDED_BYTES = 16 * 1024 * 1024
MAX_ZIP_RATIO = 100


def safe_component(name: str, label: str) -> str:
    if not isinstance(name, str) or not SAFE_NAME.fullmatch(name) or name in (".", ".."):
        raise ValueError(f"{label} must be one safe path component")
    return name


def safe_child(root: Path, name: str, label: str = "character name") -> Path:
    safe_component(name, label)
    resolved_root = root.resolve()
    candidate = resolved_root / name
    if candidate.is_symlink():
        raise ValueError(f"{label} must not be a symlink")
    if candidate.resolve().parent != resolved_root:
        raise ValueError(f"{label} must stay inside {resolved_root}")
    return candidate


def extract_character_zip(src: Path, destination: Path) -> Path:
    """Extract a bounded archive without `extractall` traversal/symlink rules."""
    with zipfile.ZipFile(src) as archive:
        members = archive.infolist()
        if len(members) > MAX_ZIP_MEMBERS:
            raise ValueError("zip has too many members")
        compressed = sum(member.compress_size for member in members)
        expanded = sum(member.file_size for member in members)
        if compressed > MAX_ZIP_COMPRESSED_BYTES or expanded > MAX_ZIP_EXPANDED_BYTES:
            raise ValueError("zip exceeds byte limit")
        for member in members:
            member_path = Path(member.filename)
            mode = member.external_attr >> 16
            file_type = stat.S_IFMT(mode)
            parts = member_path.parts
            if (not parts or member_path.is_absolute() or ".." in parts or
                    any(not SAFE_NAME.fullmatch(part) for part in parts) or
                    stat.S_ISLNK(mode) or
                    (file_type and not member.is_dir() and file_type != stat.S_IFREG)):
                raise ValueError("zip contains unsafe member")
            if member.file_size and member.compress_size == 0:
                raise ValueError("zip member has invalid compression size")
            if member.compress_size and member.file_size > member.compress_size * MAX_ZIP_RATIO:
                raise ValueError("zip member expansion ratio too high")
            target = (destination / member_path).resolve()
            if target != destination.resolve() and destination.resolve() not in target.parents:
                raise ValueError("zip member escapes destination")
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(member, "r") as source, target.open("xb") as output:
                while data := source.read(64 * 1024):
                    output.write(data)
    manifests = list(destination.rglob("manifest.json"))
    if len(manifests) != 1:
        raise ValueError("zip must contain exactly one manifest.json")
    return manifests[0].parent


def _load_normalized(src_path: Path) -> tuple[list[Image.Image], list[int]]:
    """All frames at REF_W width, RGBA, with durations."""
    frames, durations = [], []
    with Image.open(src_path) as im:
        for f in ImageSequence.Iterator(im):
            durations.append(f.info.get("duration", 100))
            rgba = f.convert("RGBA").copy()
            scale = REF_W / rgba.width
            frames.append(rgba.resize((REF_W, round(rgba.height * scale)), Image.LANCZOS))
    return frames, durations


def _union(a, b):
    if a is None: return b
    if b is None: return a
    return (min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]))


def _save_state(frames, durations, dst: Path, bbox, bg_rgb):
    out = []
    for f in frames:
        cropped = f.crop(bbox)
        w, h = cropped.size
        new_h = max(1, round(h * TARGET_W / w))
        resized = cropped.resize((TARGET_W, new_h), Image.LANCZOS)
        flat = Image.new("RGB", resized.size, bg_rgb)
        flat.paste(resized, mask=resized.split()[-1])
        out.append(flat.convert("P", palette=Image.ADAPTIVE, colors=64))
    out[0].save(
        dst, save_all=True, append_images=out[1:],
        duration=durations, loop=0, optimize=False, disposal=1,
    )
    return dst.stat().st_size


def install(src: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="cardputer-character-") as temporary:
        if src.suffix == ".zip":
            src = extract_character_zip(src, Path(temporary))

        manifest = json.loads((src / "manifest.json").read_text())
        name = manifest["name"]
        out = safe_child(OUT_ROOT, name)
        bg_hex = manifest.get("colors", {}).get("bg", "#000000").lstrip("#")
        bg_rgb = tuple(int(bg_hex[i:i+2], 16) for i in (0, 2, 4))

        # Pass 1: load every state (single or list), normalize, compute one bbox across all
        loaded = []   # (out_name, state_key, frames, durations, src_bytes)
        global_bbox = None
        for state, cfg in manifest["states"].items():
            safe_component(state, "state name")
            entries = cfg if isinstance(cfg, list) else [cfg]
            for i, entry in enumerate(entries):
                gif_src = safe_child(src, entry, "state filename")
                if not gif_src.exists():
                    print(f"  skip {state}[{i}]: {entry} not found")
                    continue
                frames, durations = _load_normalized(gif_src)
                out_name = f"{state}_{i}.gif" if len(entries) > 1 else f"{state}.gif"
                safe_component(out_name, "state output filename")
                loaded.append((out_name, state, frames, durations, gif_src.stat().st_size))
                for f in frames:
                    global_bbox = _union(global_bbox, f.getbbox())

        if not loaded or global_bbox is None:
            raise ValueError("character pack contains no usable state images")
        cw, ch = global_bbox[2] - global_bbox[0], global_bbox[3] - global_bbox[1]
        if cw <= 0 or ch <= 0:
            raise ValueError("character images have empty bounds")
        out_h = round(ch * TARGET_W / cw)
        print(f"  global crop: {global_bbox} from {REF_W}-wide reference -> {TARGET_W}x{out_h} on device\n")

        # Pass 2: write
        if out.exists():
            shutil.rmtree(out)
        out.mkdir(parents=True)

        device_states, total = {}, 0
        for out_name, state, frames, durations, src_bytes in loaded:
            dst = safe_child(out, out_name, "state output filename")
            after = _save_state(frames, durations, dst, global_bbox, bg_rgb)
            total += after
            device_states.setdefault(state, []).append(out_name)
            print(f"  {out_name:14s} {src_bytes:>10,}b -> {after:>7,}b  ({len(frames)} frames)")
        # Collapse single-entry lists back to strings for the common case
        device_states = {k: (v[0] if len(v) == 1 else v) for k, v in device_states.items()}

        (out / "manifest.json").write_text(json.dumps({
            "name": name,
            "colors": manifest.get("colors", {}),
            "states": device_states,
        }, indent=2))

        cap_kb = 1800
        print(f"\nwrote {name}: {total:,} bytes -> {out}")
        if total > cap_kb * 1024:
            print(f"  warning: over {cap_kb}KB — desktop install will reject it")
            if not shutil.which("gifsicle"):
                hint = {
                    "darwin": "brew install gifsicle",
                    "win32":  "winget install LCDF.Gifsicle",
                }.get(sys.platform, "apt install gifsicle")
                print(f"  gifsicle not found: {hint}")
            gifs = " ".join(f'"{g}"' for g in out.glob("*.gif"))
            print(f"  shrink: gifsicle --batch --lossy=80 -O3 --colors 64 {gifs}")
        print("next: drag that folder onto the Hardware Buddy window")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    install(Path(sys.argv[1]))
