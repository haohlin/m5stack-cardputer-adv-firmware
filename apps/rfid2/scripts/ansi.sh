#!/usr/bin/env bash

if [[ -t 2 ]]; then
  _ansi_bold=$'\033[1m'
  _ansi_green=$'\033[32m'
  _ansi_blue=$'\033[34m'
  _ansi_red=$'\033[31m'
  _ansi_reset=$'\033[0m'
else
  _ansi_bold=""
  _ansi_green=""
  _ansi_blue=""
  _ansi_red=""
  _ansi_reset=""
fi

ansi_info() { printf '%s==>%s %s\n' "$_ansi_blue" "$_ansi_reset" "$*" >&2; }
ansi_ok() { printf '%s[SUCCESS]%s %s\n' "$_ansi_green" "$_ansi_reset" "$*" >&2; }
ansi_fail() { printf '%s[ERROR]%s %s\n' "$_ansi_red" "$_ansi_reset" "$*" >&2; }
