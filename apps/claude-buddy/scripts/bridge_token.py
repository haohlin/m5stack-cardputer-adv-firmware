#!/usr/bin/env python3

import re
import sys


TOKEN_CHARS = 32
TOKEN_PATTERN = re.compile(r"[A-Za-z0-9_-]{32}\Z")


def bridge_token_allowed(token: str) -> bool:
    if not TOKEN_PATTERN.fullmatch(token):
        return False
    for period in range(1, TOKEN_CHARS // 2 + 1):
        if TOKEN_CHARS % period == 0 and token == token[:period] * (TOKEN_CHARS // period):
            return False
    return True


if __name__ == "__main__":
    sys.exit(0 if bridge_token_allowed(sys.stdin.read()) else 1)
