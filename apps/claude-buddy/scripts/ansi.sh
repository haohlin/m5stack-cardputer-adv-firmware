#!/usr/bin/env bash

if [[ -t 2 && -z "${NO_COLOR:-}" ]]; then
  ANSI_RESET=$'\033[0m'
  ANSI_BOLD=$'\033[1m'
  ANSI_DIM=$'\033[2m'
  ANSI_RED=$'\033[31m'
  ANSI_GREEN=$'\033[32m'
  ANSI_YELLOW=$'\033[33m'
  ANSI_CYAN=$'\033[36m'
else
  ANSI_RESET=""
  ANSI_BOLD=""
  ANSI_DIM=""
  ANSI_RED=""
  ANSI_GREEN=""
  ANSI_YELLOW=""
  ANSI_CYAN=""
fi

ansi_info() {
  printf '%s==>%s %s\n' "${ANSI_CYAN}${ANSI_BOLD}" "$ANSI_RESET" "$*" >&2
}

ansi_ok() {
  printf '%s[SUCCESS]%s %s\n' "${ANSI_GREEN}${ANSI_BOLD}" "$ANSI_RESET" "$*" >&2
}

ansi_warn() {
  printf '%s[WARN]%s %s\n' "${ANSI_YELLOW}${ANSI_BOLD}" "$ANSI_RESET" "$*" >&2
}

ansi_fail() {
  printf '%s[ERROR]%s %s\n' "${ANSI_RED}${ANSI_BOLD}" "$ANSI_RESET" "$*" >&2
}
