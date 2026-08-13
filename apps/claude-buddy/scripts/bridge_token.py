#!/usr/bin/env python3

import re
import sys
import json
from pathlib import Path


TOKEN_CHARS = 32
TOKEN_PATTERN = re.compile(r"[A-Za-z0-9_-]{32}\Z")
BRIDGE_CREDENTIAL_PROVENANCE = "bridge-csprng-v1"


def bridge_token_allowed(token: str) -> bool:
    if not TOKEN_PATTERN.fullmatch(token):
        return False
    if re.search(r"(.)\1{7}", token):
        return False
    for period in range(1, TOKEN_CHARS // 2 + 1):
        if all(token[index] == token[index % period] for index in range(period, TOKEN_CHARS)):
            return False
    return True


def load_bridge_token(path: Path) -> str:
    data = json.loads(Path(path).read_text())
    if data.get("credentialProvenance") != BRIDGE_CREDENTIAL_PROVENANCE:
        raise ValueError("bridge config lacks bridge-generated provenance; start bridge to rotate it")
    token = data.get("token", "")
    if not bridge_token_allowed(token):
        raise ValueError("bridge-generated token is malformed")
    return token


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--config":
        try:
            sys.stdout.write(load_bridge_token(Path(sys.argv[2])))
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            print(f"[ERROR] {exc}", file=sys.stderr)
            sys.exit(1)
    else:
        sys.exit(0 if bridge_token_allowed(sys.stdin.read()) else 1)
