# Claude Buddy v1.3.0 recovery and Launcher migration

## Decision

Keep one `main` branch. Rewrite its published sequence so Claude Buddy's
recoverable v1.3.0 source comes before collection, security, and Launcher
work. Preserve every existing RFID2 commit and final capability.

The rewrite is intentionally reversible. Before moving `main`, create an
annotated local-and-remote backup ref for current `ca19668` history. Publish
the final rewritten `main` only with `git push --force-with-lease`; never use
an unguarded force push.

## Target history

1. Existing pre-collection RFID2 history remains unchanged.
2. `feat(claude-buddy): import v1.3.0 source baseline`
   - Imports legacy Buddy source from
     `/Users/haohanl/dev/claude-desktop-buddy-m5stack-cardputer-adv`.
   - Includes the 12 modified tracked files and real untracked development:
     Wi-Fi bridge/configuration, desktop bridge, Claude plugin, provisioning
     scripts, and supporting documentation.
   - Excludes generated `.pio`, release binaries, `dist`, and `graphify-out`.
3. Annotated `claude-buddy-v1.3.0` tag.
   - Records an historical source/binary baseline, not a Launcher release.
   - Its raw `cardputer-adv` build must SHA-256-match
     `e969dbfe9fd2f444c477e6df0d9bcd9c7d564ce203f80c2f4815d0e58bcb5051`.
4. Collection/Launcher commit.
   - Introduces root `./cardputer`, app isolation, pinned Launcher contract,
     and RFID2 relocation without losing the imported Buddy code.
5. Claude Buddy security-hardening commit.
   - Reapplies and reconciles current BLE, transfer, character, and host-tool
     protections against the imported Wi-Fi/bridge source.
6. RFID2 hardening commit remains present and unchanged in behavior.
7. `feat(claude-buddy): prepare v1.4.0 Launcher release`.
   - Sets Buddy metadata to `1.4.0`.
   - Makes Launcher-OTA the only normal build, package, install, and runtime
     debug route.

## Source ownership

`apps/claude-buddy` owns firmware, PlatformIO configuration, bridge code,
host provisioning helpers, desktop bridge, Claude plugin, and app-specific
documentation. Root owns only app selection, provenance packaging, Launcher
USB-MSC staging, and shared version policy.

Legacy direct-flash and erase scripts remain recovery-only under the app's
`scripts/recovery/` boundary. Normal documentation and entrypoints never
invoke them.

Generated build products and graph reports stay ignored. Their source scripts
may be imported when they are required to regenerate diagnostic evidence.

## Build and artifact rules

The historical verification target uses the legacy `cardputer-adv` environment
with its original `no_ota.csv` layout only to reproduce the known v1.3.0 raw
application hash. It must not be staged, installed, or flashed.

The v1.4.0 target uses `cardputer-adv-launcher-ota`, the pinned Launcher 8 MB
partition contract, and root `./cardputer build|release|stage claude-buddy`.
Only raw application images enter Launcher USB-MSC `tools/`; merged images are
recovery artifacts and never enter normal staging.

## Runtime path

Hardware proof is mandatory and separate from build proof:

1. Boot Launcher and enable USB MSC.
2. Stage v1.4.0 using `./cardputer stage claude-buddy`.
3. Leave USB MSC; select the raw image in Launcher and install it to `ota_0`.
4. Debug only installed v1.4.0 with `./cardputer debug claude-buddy serial`
   and the Buddy BLE pairing/bridge checks.

The v1.3.0 checksum target receives no deployment or hardware-release claim.

## Failure handling

- If a clean v1.3.0 rebuild differs from recorded SHA-256, stop before tagging
  or rewriting remote history; pin and document the missing build input.
- If security patches conflict with bridge code, preserve security invariant
  first, add focused regression coverage, and do not silently drop a feature.
- If v1.4.0 exceeds Launcher OTA capacity or its raw-image check fails, do not
  package or stage it.
- If device smoke testing fails, retain source commits but do not create or
  publish a `claude-buddy-v1.4.0` release tag.

## Verification

Before remote publication:

1. Clean rebuild v1.3.0 source baseline and compare its SHA-256 exactly.
2. Run root tool checks and Buddy security tests using app-local Python.
3. Build Launcher v1.4.0; verify raw image header, partition-size limit,
   manifest SHA-256, and clean Git provenance.
4. Build/test the desktop bridge with its locked Node dependencies.
5. Run full Codex Security scan over rewritten main and resolve findings.
6. Confirm rewritten history, tags, authors, and remote refs before guarded
   force push.
7. Complete Launcher-installed serial, BLE, and bridge smoke proof separately.
