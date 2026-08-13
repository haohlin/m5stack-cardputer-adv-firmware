#!/usr/bin/env python3
"""
Prep a character pack: downscale GIFs to 96px with a CONSISTENT crop
across all states, so the character is the same size in every animation.
Writes to characters/<name>/ ready to drag onto the Hardware Buddy window.

Usage:
  python3 tools/prep_character.py <character-dir-or-zip>
"""
import json, re, stat, sys, shutil, tempfile, warnings, zipfile
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
MAX_MANIFEST_BYTES = 64 * 1024
MAX_SOURCE_IMAGE_BYTES = 4 * 1024 * 1024
MAX_IMAGE_WIDTH = 4096
MAX_IMAGE_HEIGHT = 4096
MAX_FRAMES_PER_IMAGE = 256
MAX_TOTAL_FRAMES = 512
MAX_TOTAL_SOURCE_PIXELS = 64 * 1024 * 1024
MAX_RETAINED_NORMALIZED_PIXELS = 32 * 1024 * 1024
MAX_OUTPUT_BYTES = 1800 * 1024
MAX_OUTPUT_HEIGHT = 512
SUPPORTED_STATES = frozenset({"sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart"})
MAX_STATE_COUNT = len(SUPPORTED_STATES)
MAX_STATE_IMAGES = 32
Image.MAX_IMAGE_PIXELS = MAX_IMAGE_WIDTH * MAX_IMAGE_HEIGHT


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
    if src.is_symlink() or not src.is_file() or src.stat().st_size > MAX_ZIP_COMPRESSED_BYTES:
        raise ValueError("zip exceeds byte limit")
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
            if member_path.name == "manifest.json" and member.file_size > MAX_MANIFEST_BYTES:
                raise ValueError("manifest exceeds byte limit")
            if member_path.suffix.lower() == ".gif" and member.file_size > MAX_SOURCE_IMAGE_BYTES:
                raise ValueError("source image byte limit exceeded")
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


def _inspect_source(src_path: Path, budget: dict[str, int]) -> tuple[int, int, int]:
    if src_path.is_symlink() or not src_path.is_file():
        raise ValueError(f"state image must be one regular file: {src_path.name}")
    byte_count = src_path.stat().st_size
    if byte_count <= 0 or byte_count > MAX_SOURCE_IMAGE_BYTES:
        raise ValueError("source image byte limit exceeded")
    with warnings.catch_warnings():
        warnings.simplefilter("error", Image.DecompressionBombWarning)
        with Image.open(src_path) as image:
            if image.format != "GIF":
                raise ValueError("state image must be a GIF")
            width, height = image.size
            frame_count = getattr(image, "n_frames", 1)
    if width <= 0 or height <= 0 or width > MAX_IMAGE_WIDTH or height > MAX_IMAGE_HEIGHT:
        raise ValueError("image dimensions exceed limit")
    if frame_count <= 0 or frame_count > MAX_FRAMES_PER_IMAGE:
        raise ValueError("image frame count exceeds limit")
    normalized_height = round(height * REF_W / width)
    retained = REF_W * normalized_height * frame_count
    if normalized_height <= 0 or normalized_height > MAX_IMAGE_HEIGHT or retained > MAX_RETAINED_NORMALIZED_PIXELS:
        raise ValueError("normalized image retention exceeds limit")
    budget["frames"] += frame_count
    budget["pixels"] += width * height * frame_count
    if budget["frames"] > MAX_TOTAL_FRAMES:
        raise ValueError("aggregate frame count exceeds limit")
    if budget["pixels"] > MAX_TOTAL_SOURCE_PIXELS:
        raise ValueError("aggregate decoded pixel limit exceeded")
    return byte_count, width, height


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
        if src.suffix.lower() == ".zip":
            src = extract_character_zip(src, Path(temporary))

        if src.is_symlink() or not src.is_dir():
            raise ValueError("character source must be one real directory or bounded ZIP")
        manifest_path = src / "manifest.json"
        if manifest_path.is_symlink() or not manifest_path.is_file() or manifest_path.stat().st_size > MAX_MANIFEST_BYTES:
            raise ValueError("manifest exceeds byte limit")

        manifest = json.loads(manifest_path.read_text())
        name = manifest["name"]
        out = safe_child(OUT_ROOT, name)
        bg_hex = manifest.get("colors", {}).get("bg", "#000000").lstrip("#")
        bg_rgb = tuple(int(bg_hex[i:i+2], 16) for i in (0, 2, 4))

        states = manifest.get("states")
        if not isinstance(states, dict) or not states:
            raise ValueError("character pack contains no supported states")
        if len(states) > MAX_STATE_COUNT:
            raise ValueError("character state count exceeds limit")

        # Pass 1: validate metadata and compute one bbox while retaining one source GIF at a time.
        sources = []   # (out_name, state_key, source_path, source_bytes)
        budget = {"frames": 0, "pixels": 0}
        global_bbox = None
        for state, cfg in states.items():
            safe_component(state, "state name")
            if state not in SUPPORTED_STATES:
                raise ValueError(f"unsupported character state: {state}")
            entries = cfg if isinstance(cfg, list) else [cfg]
            if not entries:
                raise ValueError(f"state {state} has no images")
            for i, entry in enumerate(entries):
                if not isinstance(entry, str):
                    raise ValueError(f"state {state} filename must be text")
                if len(sources) >= MAX_STATE_IMAGES:
                    raise ValueError("character state image count exceeds limit")
                gif_src = safe_child(src, entry, "state filename")
                src_bytes, _, _ = _inspect_source(gif_src, budget)
                frames, durations = _load_normalized(gif_src)
                out_name = f"{state}_{i}.gif" if len(entries) > 1 else f"{state}.gif"
                safe_component(out_name, "state output filename")
                sources.append((out_name, state, gif_src, src_bytes))
                for f in frames:
                    global_bbox = _union(global_bbox, f.getbbox())

        if not sources or global_bbox is None:
            raise ValueError("character pack contains no usable state images")
        cw, ch = global_bbox[2] - global_bbox[0], global_bbox[3] - global_bbox[1]
        if cw <= 0 or ch <= 0:
            raise ValueError("character images have empty bounds")
        out_h = round(ch * TARGET_W / cw)
        if out_h <= 0 or out_h > MAX_OUTPUT_HEIGHT:
            raise ValueError("normalized output dimensions exceed limit")
        print(f"  global crop: {global_bbox} from {REF_W}-wide reference -> {TARGET_W}x{out_h} on device\n")

        # Pass 2: write to private temporary output. Existing prepared pack stays
        # untouched until every output file and total byte limit has passed.
        prepared_root = Path(temporary) / "prepared"
        prepared_root.mkdir()
        staged = safe_child(prepared_root, name, "character name")
        if staged.exists():
            shutil.rmtree(staged)
        staged.mkdir(parents=True)

        device_states, total = {}, 0
        for out_name, state, gif_src, src_bytes in sources:
            frames, durations = _load_normalized(gif_src)
            dst = safe_child(staged, out_name, "state output filename")
            after = _save_state(frames, durations, dst, global_bbox, bg_rgb)
            total += after
            if total > MAX_OUTPUT_BYTES:
                raise ValueError("character output byte limit exceeded")
            device_states.setdefault(state, []).append(out_name)
            print(f"  {out_name:14s} {src_bytes:>10,}b -> {after:>7,}b  ({len(frames)} frames)")
        # Collapse single-entry lists back to strings for the common case
        device_states = {k: (v[0] if len(v) == 1 else v) for k, v in device_states.items()}

        (staged / "manifest.json").write_text(json.dumps({
            "name": name,
            "colors": manifest.get("colors", {}),
            "states": device_states,
        }, indent=2))

        if out.exists():
            shutil.rmtree(out)
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(staged, out)
        print(f"\nwrote {name}: {total:,} bytes -> {out}")
        print("next: drag that folder onto the Hardware Buddy window")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    install(Path(sys.argv[1]))
