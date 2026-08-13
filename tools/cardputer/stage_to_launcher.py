#!/usr/bin/env python3
"""Contain firmware staging inside one validated Launcher USB MSC tools directory."""

from __future__ import annotations

import argparse
import errno
import hashlib
import os
from pathlib import Path
import re
import secrets
import stat
import sys
from typing import Callable


MAX_FIRMWARE_BYTES = 0x4F0000
SAFE_COMPONENT = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*\Z")


def _component(value: str, label: str) -> str:
    if not SAFE_COMPONENT.fullmatch(value) or Path(value).name != value or value in {".", ".."}:
        raise ValueError(f"{label} must be one safe path component")
    return value


def _firmware(path: Path) -> Path:
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise ValueError(f"firmware must be one regular non-symlink file: {path}")
    resolved = path.resolve(strict=True)
    if info.st_size <= 0 or info.st_size > MAX_FIRMWARE_BYTES:
        raise ValueError("firmware exceeds Launcher OTA size limit")
    with resolved.open("rb") as stream:
        if stream.read(1) != b"\xe9":
            raise ValueError("firmware is not an ESP application image")
    return resolved


def _launcher_tools(volume: Path, tools_name: str) -> tuple[Path, int]:
    _component(tools_name, "Launcher tools directory")
    volume_info = volume.lstat()
    if stat.S_ISLNK(volume_info.st_mode) or not stat.S_ISDIR(volume_info.st_mode):
        raise ValueError(f"Launcher root must be one real directory: {volume}")
    resolved_volume = volume.resolve(strict=True)
    tools = resolved_volume / tools_name
    tools_info = tools.lstat()
    if stat.S_ISLNK(tools_info.st_mode) or not stat.S_ISDIR(tools_info.st_mode):
        raise ValueError(f"Launcher tools must be one real directory: {tools}")
    resolved_tools = tools.resolve(strict=True)
    if resolved_tools.parent != resolved_volume:
        raise ValueError("Launcher tools must stay directly inside selected root")
    if not hasattr(os, "O_NOFOLLOW") or not hasattr(os, "O_DIRECTORY"):
        raise OSError("platform lacks no-follow directory staging support")
    directory_fd = os.open(resolved_tools, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    opened = os.fstat(directory_fd)
    if (opened.st_dev, opened.st_ino) != (tools_info.st_dev, tools_info.st_ino):
        os.close(directory_fd)
        raise OSError("Launcher tools changed during validation")
    return resolved_tools, directory_fd


def _entry(directory_fd: int, name: str):
    try:
        return os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
    except FileNotFoundError:
        return None


def _exclusive_partial(
    directory_fd: int,
    target_name: str,
    nonce_factory: Callable[[], str],
) -> tuple[str, int]:
    nonce = _component(nonce_factory(), "temporary nonce")
    name = f".{target_name}.{nonce}.partial"
    flags = os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW
    try:
        descriptor = os.open(name, flags, 0o600, dir_fd=directory_fd)
    except FileExistsError as error:
        existing = _entry(directory_fd, name)
        if existing is not None and stat.S_ISLNK(existing.st_mode):
            raise ValueError("refusing symlinked Launcher temporary target") from error
        raise OSError("exclusive Launcher temporary target already exists") from error
    created = os.fstat(descriptor)
    if not stat.S_ISREG(created.st_mode) or created.st_nlink != 1:
        os.close(descriptor)
        os.unlink(name, dir_fd=directory_fd)
        raise OSError("Launcher temporary target is not one regular file")
    return name, descriptor


def _fsync_directory(directory_fd: int) -> None:
    try:
        os.fsync(directory_fd)
    except OSError as error:
        if error.errno not in {errno.EINVAL, getattr(errno, "ENOTSUP", errno.EINVAL)}:
            raise


def stage_artifact(
    firmware: Path,
    volume: Path,
    tools_name: str,
    target_name: str,
    app_prefix: str,
    *,
    nonce_factory: Callable[[], str] = secrets.token_hex,
) -> Path:
    """Stage one image with no-follow dirfd operations and family-scoped cleanup."""
    source = _firmware(Path(firmware))
    _component(target_name, "artifact filename")
    _component(app_prefix, "artifact prefix")
    if not target_name.startswith(f"{app_prefix}-v") or not target_name.endswith(".bin"):
        raise ValueError("artifact filename does not match selected app family")

    tools, directory_fd = _launcher_tools(Path(volume), tools_name)
    partial_name = ""
    partial_fd = -1
    try:
        target_info = _entry(directory_fd, target_name)
        if target_info is not None and (
            stat.S_ISLNK(target_info.st_mode) or not stat.S_ISREG(target_info.st_mode)
        ):
            raise ValueError("refusing non-regular or symlinked Launcher target")

        partial_name, partial_fd = _exclusive_partial(
            directory_fd, target_name, lambda: nonce_factory() if nonce_factory is not secrets.token_hex else secrets.token_hex(8)
        )
        written = hashlib.sha256()
        with source.open("rb") as input_stream:
            while chunk := input_stream.read(1024 * 1024):
                written.update(chunk)
                view = memoryview(chunk)
                while view:
                    count = os.write(partial_fd, view)
                    if count <= 0:
                        raise OSError("short write while staging Launcher artifact")
                    view = view[count:]
        os.fsync(partial_fd)
        os.lseek(partial_fd, 0, os.SEEK_SET)
        verified = hashlib.sha256()
        while chunk := os.read(partial_fd, 1024 * 1024):
            verified.update(chunk)
        if written.digest() != verified.digest():
            raise OSError("copied artifact checksum mismatch")

        os.replace(partial_name, target_name, src_dir_fd=directory_fd, dst_dir_fd=directory_fd)
        partial_name = ""
        _fsync_directory(directory_fd)

        family = f"{app_prefix}-v"
        for old_name in os.listdir(directory_fd):
            if old_name == target_name or not old_name.startswith(family) or not old_name.endswith(".bin"):
                continue
            old_info = _entry(directory_fd, old_name)
            if old_info is not None and stat.S_ISREG(old_info.st_mode) and not stat.S_ISLNK(old_info.st_mode):
                os.unlink(old_name, dir_fd=directory_fd)
        _fsync_directory(directory_fd)
        return tools / target_name
    finally:
        if partial_fd >= 0:
            os.close(partial_fd)
        if partial_name:
            existing = _entry(directory_fd, partial_name)
            if existing is not None and stat.S_ISREG(existing.st_mode) and not stat.S_ISLNK(existing.st_mode):
                os.unlink(partial_name, dir_fd=directory_fd)
        os.close(directory_fd)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware")
    parser.add_argument("volume")
    parser.add_argument("tools_name")
    parser.add_argument("target_name")
    parser.add_argument("app_prefix")
    args = parser.parse_args()
    try:
        target = stage_artifact(
            Path(args.firmware),
            Path(args.volume),
            args.tools_name,
            args.target_name,
            args.app_prefix,
        )
    except (OSError, ValueError) as error:
        print(f"cardputer: {error}", file=sys.stderr)
        return 1
    print(target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
