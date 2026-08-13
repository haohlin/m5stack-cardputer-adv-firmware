# Claude Buddy v1.3.0 Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recover Claude Buddy's complete v1.3.0 source history into one rewritten `main`, preserve historical binary provenance, then deliver all current security and Launcher work as Launcher-only v1.4.0.

**Architecture:** Commit the recoverable legacy working tree before collection tooling, tag it as historical v1.3.0 after a clean build and recorded binary provenance, then replay collection, security, and RFID2 commits with the recovered Buddy files retained. Final Buddy source stays under `apps/claude-buddy`; root `cardputer` remains sole normal build/stage/debug interface.

**Tech Stack:** Git history rewrite, Bash, PlatformIO/ESP32-S3 Arduino, Launcher OTA, C++17, Python unittest/Pillow, Node/TypeScript/MCPB.

## Global Constraints

- Preserve every reachable RFID2 commit and its legacy `v*` tags unchanged.
- Preserve all 12 legacy modified files and source-bearing untracked files; exclude `.pio`, `.venv`, `release`, `dist`, `desktop-bridge/node_modules`, `desktop-bridge/dist`, `.mcpb`, `claude-plugin/*.zip`, and `graphify-out` generated output.
- `claude-buddy-v1.3.0` is historical raw `cardputer-adv` only. Observed historical SHA-256: `e969dbfe9fd2f444c477e6df0d9bcd9c7d564ce203f80c2f4815d0e58bcb5051`; clean rebuild SHA may differ because legacy dependencies float.
- Never stage, install, or direct-flash v1.3.0. Direct Buddy flash/erase scripts remain recovery-only.
- Final normal workflow is `./cardputer build|release|stage|debug claude-buddy`, Launcher USB MSC, then Launcher OTA install to `ota_0`.
- Final released app version is `1.4.0`; final release tag is `claude-buddy-v1.4.0` only after physical Launcher-installed proof.
- Create remote backup refs before rewrite. Publish only with `git push --force-with-lease`; no unguarded force push.
- Do not move or modify `/Users/haohanl/dev/claude-desktop-buddy-m5stack-cardputer-adv` during migration.

---

## File structure

- `apps/claude-buddy/` — recovered firmware, bridge/config source, desktop bridge, plugin, app documentation, PlatformIO environments, recovery-only helpers.
- `apps/claude-buddy/src/bridge_config.{h,cpp}` — bounds-checked persisted bridge identity, Wi-Fi credentials, and pairing token.
- `apps/claude-buddy/src/bridge_wifi.{h,cpp}` — optional WebSocket/Wi-Fi transport; official BLE remains independently usable.
- `apps/claude-buddy/desktop-bridge/` — tracked Node/TypeScript MCPB source; dependencies and build output remain ignored.
- `apps/claude-buddy/claude-plugin/` — tracked Claude Code/Cowork extension source; packaged ZIP remains ignored.
- `apps/claude-buddy/scripts/recovery/` — direct flash/erase/merged-image helpers only.
- `tests/test-claude-buddy-v130.sh` — clean checkout historical source build and binary-provenance record check.
- `tests/test-claude-buddy-security.py` and `tests/claude-buddy-security-test.cpp` — existing security regression suite, expanded for recovered bridge boundaries.
- `tools/cardputer/common.sh`, `cardputer` — shared Launcher-only normal lifecycle.
- `VERSIONING.md`, `PROGRESS.md`, `design/firmware-suite.md` — release semantics, physical proof, and final architecture.

### Task 1: Freeze current history and prove legacy v1.3.0 input

**Files:**

- Create: local-and-remote tag `backup/pre-v130-recovery-main`
- Create: local-and-remote branch `backup/pre-v130-recovery-main`
- Create: `tests/test-claude-buddy-v130.sh`
- Test: `tests/test-claude-buddy-v130.sh`

**Interfaces:**

- Consumes: current `main` at `fce9d2c`, legacy working tree, mounted SD image SHA.
- Produces: immutable recovery point plus test command that confirms historical source builds and records both historical and clean-build hashes.

- [ ] **Step 1: Record refs and remote lease before any rewrite**

  Run:

  ```bash
  git fetch origin --prune --tags
  git rev-parse main origin/main
  git tag -a backup/pre-v130-recovery-main main -m 'Backup before Claude Buddy v1.3.0 recovery rewrite'
  git branch backup/pre-v130-recovery-main main
  git push origin refs/tags/backup/pre-v130-recovery-main refs/heads/backup/pre-v130-recovery-main
  ```

  Expected: local `main` and `origin/main` are recorded separately; backup tag and branch resolve to current local tip.

- [ ] **Step 2: Write failing reproducibility test**

  Create `tests/test-claude-buddy-v130.sh` with these fixed values and behavior:

  ```bash
  #!/usr/bin/env bash
  set -euo pipefail
  ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
  TAG="claude-buddy-v1.3.0"
  HISTORICAL="e969dbfe9fd2f444c477e6df0d9bcd9c7d564ce203f80c2f4815d0e58bcb5051"
  TMP="$(mktemp -d)"
  trap 'rm -rf "$TMP"' EXIT
  git -C "$ROOT" archive "$TAG:apps/claude-buddy" | tar -x -C "$TMP"
  (
    cd "$TMP"
    ./scripts/pio_local.sh run -e cardputer-adv -t clean
    ./scripts/pio_local.sh run -e cardputer-adv
  )
  ACTUAL="$(shasum -a 256 "$TMP/.pio/build/cardputer-adv/firmware.bin" | awk '{print $1}')"
  printf 'historical_sha256=%s\nclean_build_sha256=%s\n' "$HISTORICAL" "$ACTUAL"
  ```

  Do not add the file to Git until the tag exists.

- [ ] **Step 3: Run test before tag creation**

  Run:

  ```bash
  bash tests/test-claude-buddy-v130.sh
  ```

  Expected: fail because `claude-buddy-v1.3.0` does not exist yet.

- [ ] **Step 4: Rebuild the unchanged legacy working tree in a disposable copy**

  Run:

  ```bash
  LEGACY=/Users/haohanl/dev/claude-desktop-buddy-m5stack-cardputer-adv
  TMP="$(mktemp -d)"
  rsync -a --delete \
    --exclude=.git --exclude=.pio --exclude=.venv --exclude=release --exclude=data \
    --exclude=desktop-bridge/node_modules --exclude=desktop-bridge/dist \
    --exclude=graphify-out "$LEGACY/" "$TMP/"
  (cd "$TMP" && ./scripts/pio_local.sh run -e cardputer-adv -t clean && ./scripts/pio_local.sh run -e cardputer-adv)
  shasum -a 256 "$TMP/.pio/build/cardputer-adv/firmware.bin"
  rm -rf "$TMP"
  ```

  Expected: a successful clean image build. Record both observed historical
  and clean-build SHA-256 values; a difference documents floating dependency
  drift and does not block source recovery.

- [ ] **Step 5: Carry reproducibility test into migration worktree after tag exists**

  After task 2 creates `claude-buddy-v1.3.0`, create the test in
  `.worktrees/claude-buddy-v130-history/tests/` using step 2 content. Run:

  ```bash
  cd .worktrees/claude-buddy-v130-history
  bash tests/test-claude-buddy-v130.sh
  ```

  Expected: exit zero. Commit it with task 3's collection replay, after the
  historical tag it verifies is already immutable.

### Task 2: Create historical v1.3.0 source commit

**Files:**

- Create: `apps/claude-buddy/` from legacy source-bearing files
- Create: `docs/history/claude-buddy-v1.3.0.md`
- Create: annotated tag `claude-buddy-v1.3.0`

**Interfaces:**

- Consumes: verified legacy working tree that builds, plus observed artifact provenance.
- Produces: `claude-buddy-v1.3.0:apps/claude-buddy/platformio.ini` with legacy `cardputer-adv`/`no_ota.csv`, and no normal deployment claim.

- [ ] **Step 1: Create migration worktree from RFID2 pre-collection parent**

  Run:

  ```bash
  git worktree add .worktrees/claude-buddy-v130-history -b migrate/claude-buddy-v130 b43d460319f6e5572d9ed11c453ae8dc37590bc0
  cd .worktrees/claude-buddy-v130-history
  ```

  Expected: worktree contains pre-collection RFID2 source and no `apps/claude-buddy` directory.

- [ ] **Step 2: Copy only legacy source-bearing material**

  Run:

  ```bash
  LEGACY=/Users/haohanl/dev/claude-desktop-buddy-m5stack-cardputer-adv
  mkdir -p apps/claude-buddy
  rsync -a --delete \
    --exclude=.git --exclude=.pio --exclude=.venv --exclude=release --exclude=data \
    --exclude=desktop-bridge/node_modules --exclude=desktop-bridge/dist \
    --exclude='desktop-bridge/*.mcpb' --exclude='claude-plugin/*.zip' \
    --exclude=graphify-out "$LEGACY/" apps/claude-buddy/
  ```

  Preserve: all tracked modifications, `src/bridge_*`, `desktop-bridge`, `claude-plugin`, docs, `scripts/*.sh`, `scripts/*.py`, package manifests, and `src/bridge_config.local.example.h`.

- [ ] **Step 3: Write historical provenance record**

  Create `docs/history/claude-buddy-v1.3.0.md`:

  ```markdown
  # Claude Buddy v1.3.0 historical baseline

  Source recovered from `claude-desktop-buddy-m5stack-cardputer-adv` working tree.
  Build only with `apps/claude-buddy` environment `cardputer-adv`.
  Observed historical raw image SHA-256:
  `e969dbfe9fd2f444c477e6df0d9bcd9c7d564ce203f80c2f4815d0e58bcb5051`.
  Clean rebuild SHA-256: `3cff66a91a7efdea96eb94272ba84bf0b435c9bcd52450925b1a00ce0e15b7b1`.
  Difference is expected: historical source used floating M5Unified/M5GFX
  dependencies; clean build resolves newer compatible versions.
  This predates Launcher OTA support. It is preserved for source and binary
  reproducibility only; never stage, install, or direct-flash it.
  ```

- [ ] **Step 4: Verify imported source builds exact raw binary**

  Run:

  ```bash
  (cd apps/claude-buddy && ./scripts/pio_local.sh run -e cardputer-adv -t clean && ./scripts/pio_local.sh run -e cardputer-adv)
  shasum -a 256 apps/claude-buddy/.pio/build/cardputer-adv/firmware.bin
  ```

  Expected: successful build. Record clean-build SHA with historical SHA; do
  not alter source, dependency constraints, or compiler flags to force a match.

- [ ] **Step 5: Commit and tag exact baseline**

  Run:

  ```bash
  git add apps/claude-buddy docs/history/claude-buddy-v1.3.0.md
  git diff --cached --check
  git commit -m 'feat(claude-buddy): recover v1.3.0 source baseline'
  git tag -a claude-buddy-v1.3.0 -m 'Historical raw v1.3.0 source baseline; not Launcher-installable'
  ```

  Expected: tag contains complete recoverable source, not generated output.

### Task 3: Replay collection structure without replacing recovered Buddy source

**Files:**

- Modify: root `cardputer`, `tools/cardputer/common.sh`, `launcher.lock`, `contracts/launcher/*`
- Modify: `apps/rfid2/**`
- Modify: `apps/claude-buddy/app.env`, `apps/claude-buddy/platformio.ini`, `apps/claude-buddy/scripts/build_cardputer_adv.sh`
- Modify: `apps/claude-buddy/AGENTS.md`
- Modify: root `README.md`, `VERSIONING.md`, `docs/development.md`, `design/firmware-suite.md`

**Interfaces:**

- Consumes: historical source at `apps/claude-buddy` and original collection commit `c0b9f917`.
- Produces: `./cardputer build|release|stage|debug claude-buddy`, while legacy `cardputer-adv` remains available exclusively to the historical SHA test.

- [ ] **Step 1: Apply non-Buddy portion of original collection commit**

  Run:

  ```bash
  git diff --binary c0b9f917^ c0b9f917 -- . ':(exclude)apps/claude-buddy' | git apply --index
  git checkout c0b9f917 -- apps/claude-buddy/app.env
  ```

  Expected: root Launcher tooling and RFID2 relocation are staged; recovered Buddy code remains untouched.

- [ ] **Step 2: Add Launcher environment while retaining historical environment**

  In `apps/claude-buddy/platformio.ini`, retain legacy block exactly:

  ```ini
  [env:cardputer-adv]
  board_build.partitions = no_ota.csv
  ```

  Add final normal block:

  ```ini
  [env:cardputer-adv-launcher-ota]
  platform = espressif32@6.7.0
  board = esp32-s3-devkitc-1
  framework = arduino
  monitor_speed = 115200
  upload_speed = 1500000
  board_build.filesystem = littlefs
  board_build.partitions = ../../contracts/launcher/cardputer-adv-8mb.csv
  build_flags =
      -DCORE_DEBUG_LEVEL=0
      -DESP32S3
      -DCARDPUTER_ADV
      -DM5GFX_BOARD=board_M5CardputerADV
      -DARDUINO_USB_CDC_ON_BOOT=1
      -DARDUINO_USB_MODE=1
  build_src_filter = +<*> +<buddies/>
  lib_deps =
      m5stack/M5Unified @ ^0.2.0
      m5stack/M5Cardputer @ ^1.1.1
      bitbank2/AnimatedGIF @ ^2.1.1
      bblanchon/ArduinoJson @ ^7.0.0
      links2004/WebSockets @ ^2.6.1
  ```

  `apps/claude-buddy/scripts/build_cardputer_adv.sh` must default to
  `cardputer-adv-launcher-ota`; it may accept `CARDPUTER_ADV_ENV=cardputer-adv`
  only for the historical reproducibility test.

  In `apps/claude-buddy/AGENTS.md`, replace all normal direct-flash guidance
  with: `Normal delivery and runtime debugging use root ./cardputer plus
  Launcher USB MSC and Launcher OTA. scripts/recovery is explicit recovery
  only.`

- [ ] **Step 3: Make app metadata truthful during transition**

  Set `apps/claude-buddy/app.env` to:

  ```bash
  APP_ID="claude-buddy"
  APP_TITLE="Claude Desktop Buddy"
  APP_VERSION="1.3.0"
  APP_PLATFORMIO_ENV="cardputer-adv-launcher-ota"
  APP_FIRMWARE_REL=".pio/build/cardputer-adv-launcher-ota/firmware.bin"
  APP_ARTIFACT_PREFIX="Claude-Desktop-Buddy"
  ```

  This metadata identifies migrated source. It does not make the historical
  raw v1.3.0 binary Launcher-installable.

- [ ] **Step 4: Run focused static checks before commit**

  Run:

  ```bash
  ./tests/test-cardputer-tools.sh
  bash -n cardputer tools/cardputer/common.sh apps/claude-buddy/scripts/build_cardputer_adv.sh
  git diff --cached --check
  ```

  Expected: all commands exit zero.

- [ ] **Step 5: Commit collection replay**

  Run:

  ```bash
  git add tests/test-claude-buddy-v130.sh
  git commit -m 'feat: unify Cardputer firmware workspace'
  ```

### Task 4: Reapply Claude Buddy security changes onto recovered bridge source

**Files:**

- Modify: `apps/claude-buddy/src/ble_bridge.{h,cpp}`, `character.cpp`, `data.h`, `main.cpp`, `xfer.h`
- Create: `apps/claude-buddy/src/security_utils.h`
- Modify: `apps/claude-buddy/tools/flash_character.py`, `prep_character.py`, `test_xfer.py`
- Modify: `tests/test-claude-buddy-security.py`, `tests/claude-buddy-security-test.cpp`

**Interfaces:**

- Consumes: recovered BLE/bridge/Wi-Fi code and original secure patch `5edbd9e7`.
- Produces: BLE radio state controls official BLE and bridge path; unsafe character transfer data never reaches LittleFS.

- [ ] **Step 1: Add failing structural checks for recovered bridge safety**

  Add to `tests/test-claude-buddy-security.py`:

  ```python
  def test_bridge_config_is_not_logged_or_exposed_in_status(self):
      config = (BUDDY / "src" / "bridge_config.cpp").read_text()
      bundle = (BUDDY / "scripts" / "collect_debug_bundle.sh").read_text()
      self.assertIn('prefs.putString("br_tok", cfg.token);', config)
      self.assertIn('out[key] = "<redacted>"', bundle)
      self.assertNotIn('print(cfg.token)', config)

  def test_launcher_environment_includes_websocket_dependency(self):
      ini = (BUDDY / "platformio.ini").read_text()
      self.assertIn('[env:cardputer-adv-launcher-ota]', ini)
      self.assertIn('links2004/WebSockets @ ^2.6.1', ini)
  ```

- [ ] **Step 2: Run tests to establish failure**

  Run:

  ```bash
  apps/claude-buddy/.venv/bin/python tests/test-claude-buddy-security.py
  ```

  Expected: new bridge/environment assertions fail before reconciliation.

- [ ] **Step 3: Apply original security patch as a three-way merge**

  Run:

  ```bash
  git diff --binary 5edbd9e7^ 5edbd9e7 -- apps/claude-buddy | git apply --3way
  ```

  Resolve each conflict by preserving both: recovered bridge imports/calls and
  security behavior. Required final security calls include:

  ```cpp
  dataSetBleEnabled(s.bt);
  rxTail = rxHead;
  if (!radioEnabled) return 0;
  if (!radioEnabled) return -1;
  ```

  Required `security_utils.h` interface remains:

  ```cpp
  bool buddySafePathComponent(const char* value, size_t capacity);
  bool buddyAppendPath(char (*paths)[32], uint8_t& count, const char* path);
  bool buddyFormattedLengthFits(int written, size_t capacity);
  bool buddyTransferFits(uint32_t received, uint32_t total, uint32_t maxBytes);
  bool buddyChunkFits(uint32_t received, uint32_t total, uint32_t maxBytes, uint32_t offset, uint32_t length);
  uint32_t buddyPairingPasskey(uint32_t randomValue);
  ```

- [ ] **Step 4: Run security regression suite and Launcher build**

  Run:

  ```bash
  apps/claude-buddy/.venv/bin/python tests/test-claude-buddy-security.py
  ./cardputer build claude-buddy
  ```

  Expected: all Python/C++ checks pass; firmware includes WebSockets and fits Launcher OTA limit.

- [ ] **Step 5: Commit rebase result**

  Run:

  ```bash
  git add apps/claude-buddy tests/test-claude-buddy-security.py tests/claude-buddy-security-test.cpp
  git diff --cached --check
  git commit -m 'fix(claude-buddy): harden BLE and character handling'
  ```

### Task 5: Reapply RFID2 hardening and preserve collection documentation

**Files:**

- Modify: all paths changed by original RFID2 hardening `ca196681`
- Modify: `README.md`, `VERSIONING.md`, `PROGRESS.md`, `docs/development.md`, `design/firmware-suite.md`
- Modify: `docs/superpowers/specs/2026-08-13-claude-buddy-v130-migration-design.md`
- Modify: `docs/superpowers/plans/2026-08-13-claude-buddy-v130-migration.md`

**Interfaces:**

- Consumes: current recovery migration and original RFID2 security patch.
- Produces: same RFID2 behavior as published `ca19668`, corrected Buddy history/docs.

- [ ] **Step 1: Apply RFID2 patch without changing its payload**

  Run:

  ```bash
  git diff --binary ca196681653fa36d9fe7b09c33472651e85044a3^ ca196681653fa36d9fe7b09c33472651e85044a3 -- apps/rfid2 tests tools/cardputer/common.sh | git apply --3way
  ./tests/test-cardputer-tools.sh
  ./cardputer build rfid2
  ```

  Expected: RFID2 build and root checks match current published hardening behavior.

- [ ] **Step 2: Reapply branding and migration design documents in correct order**

  Run:

  ```bash
  git show 3ae4e4a2baa01640c478af39abc855b07087aef2 -- README.md VERSIONING.md PROGRESS.md docs design | git apply --3way
  git show fce9d2c -- docs/superpowers/specs/2026-08-13-claude-buddy-v130-migration-design.md | git apply --index
  ```

  Update versioning text to state that `claude-buddy-v1.3.0` is historical
  raw-source provenance and `claude-buddy-v1.4.0` is first Launcher release.

- [ ] **Step 3: Commit original-equivalent RFID2 and docs changes separately**

  Run:

  ```bash
  git add apps/rfid2 tests tools/cardputer/common.sh
  git commit -m 'fix(rfid2): harden writes, persistence, and staging'
  git add README.md VERSIONING.md PROGRESS.md docs design
  git commit -m 'docs: record Claude Buddy v1.3.0 recovery history'
  ```

### Task 6: Produce Launcher-only Claude Buddy v1.4.0

**Files:**

- Modify: `apps/claude-buddy/app.env`, `apps/claude-buddy/README.md`, `apps/claude-buddy/docs/BUILDING.md`
- Modify: `docs/development.md`, `VERSIONING.md`, `PROGRESS.md`, `design/firmware-suite.md`
- Modify: `cardputer`, `tools/cardputer/common.sh` only if a normal path could invoke direct flash.

**Interfaces:**

- Consumes: migrated secure Buddy source and root Launcher tooling.
- Produces: raw OTA app provenance version `1.4.0`, staged only by `./cardputer stage claude-buddy`.

- [ ] **Step 1: Write failing version/deployment assertions**

  Add to `tests/test-cardputer-tools.sh`:

  ```bash
  grep -qx 'APP_VERSION="1.4.0"' apps/claude-buddy/app.env
  grep -qx 'APP_PLATFORMIO_ENV="cardputer-adv-launcher-ota"' apps/claude-buddy/app.env
  ! grep -R -nE 'flash_cardputer_adv|erase_cardputer_adv|write_flash' cardputer tools/cardputer docs/development.md README.md
  ```

- [ ] **Step 2: Run checks before metadata/doc update**

  Run:

  ```bash
  ./tests/test-cardputer-tools.sh
  ```

  Expected: fail because Buddy still reports `1.3.0`.

- [ ] **Step 3: Set version and normal installation language**

  Update app metadata:

  ```bash
  APP_VERSION="1.4.0"
  APP_PLATFORMIO_ENV="cardputer-adv-launcher-ota"
  APP_FIRMWARE_REL=".pio/build/cardputer-adv-launcher-ota/firmware.bin"
  ```

  Every user-facing build/install command must be one of:

  ```bash
  ./cardputer build claude-buddy
  ./cardputer release claude-buddy
  ./cardputer stage claude-buddy
  ./cardputer debug claude-buddy serial
  ./cardputer debug claude-buddy ble
  ```

  Move any direct flash/erase invocation examples to clearly marked recovery-only
  sections under `apps/claude-buddy/scripts/recovery/`.

- [ ] **Step 4: Verify new artifact provenance**

  Run:

  ```bash
  ./tests/test-cardputer-tools.sh
  apps/claude-buddy/.venv/bin/python tests/test-claude-buddy-security.py
  ./cardputer release claude-buddy
  MANIFEST=$(ls -t dist/claude-buddy/Claude-Desktop-Buddy-v1.4.0-*.json | head -n 1)
  jq -e '.version == "1.4.0" and .git_dirty == false and .platformio_environment == "cardputer-adv-launcher-ota"' "$MANIFEST"
  ```

  Expected: all checks pass; manifest reports Launcher environment and clean provenance.

- [ ] **Step 5: Commit v1.4.0 preparation**

  Run:

  ```bash
  git add apps/claude-buddy README.md VERSIONING.md PROGRESS.md docs/development.md design/firmware-suite.md tests/test-cardputer-tools.sh cardputer tools/cardputer/common.sh
  git diff --cached --check
  git commit -m 'feat(claude-buddy): prepare v1.4.0 Launcher release'
  ```

### Task 7: Validate full rewritten graph, security, and remote publication

**Files:**

- Test: Git refs/history, `tests/test-claude-buddy-v130.sh`, root tools, Buddy security suite, RFID2 build, Buddy release, desktop bridge build, Codex Security full scan.

**Interfaces:**

- Consumes: completed migration branch and protected remote backup refs.
- Produces: verified single `main` history, published with guarded lease; no v1.4 release tag until device proof.

- [ ] **Step 1: Validate history and exact baseline**

  Run:

  ```bash
  git log --graph --decorate --oneline b43d460..HEAD
  git merge-base --is-ancestor claude-buddy-v1.3.0 HEAD
  bash tests/test-claude-buddy-v130.sh
  git diff --exit-code ca196681653fa36d9fe7b09c33472651e85044a3 -- apps/rfid2
  ```

  Expected: historical tag precedes Launcher/security commits; historical
  source builds and records known dependency drift; RFID2 final tree matches
  previous published hardened tree.

- [ ] **Step 2: Validate firmware, bridge, and plugin sources**

  Run:

  ```bash
  ./tests/test-cardputer-tools.sh
  apps/claude-buddy/.venv/bin/python tests/test-claude-buddy-security.py
  ./cardputer build rfid2
  ./cardputer release claude-buddy
  (cd apps/claude-buddy/desktop-bridge && npm ci && npm run build)
  node --check apps/claude-buddy/claude-plugin/hooks/relay.mjs
  git diff --check backup/pre-v130-recovery-main...HEAD
  ```

  Expected: each command exits zero; Node output stays ignored.

- [ ] **Step 3: Run full Codex Security scan**

  Use `codex-security:security-scan` against rewritten repository. Record
  validated findings in `PROGRESS.md`; fix reportable findings before publish.

- [ ] **Step 4: Replace `main` using exact remote lease**

  Run:

  ```bash
  EXPECTED_REMOTE=$(git ls-remote origin refs/heads/main | awk '{print $1}')
  MIGRATION=$(git rev-parse HEAD)
  git -C /Users/haohanl/dev/m5stack-cardputer-adv-firmware switch --detach
  git branch -f main "$MIGRATION"
  git -C /Users/haohanl/dev/m5stack-cardputer-adv-firmware switch main
  git push origin "$MIGRATION":main --force-with-lease=refs/heads/main:"$EXPECTED_REMOTE"
  git fetch origin --prune --tags
  test "$(git rev-parse main)" = "$(git rev-parse origin/main)"
  ```

  Expected: no overwrite if another writer changed remote `main`; backup refs remain on origin.

- [ ] **Step 5: Publish historical tag only**

  Run:

  ```bash
  git push origin refs/tags/claude-buddy-v1.3.0
  ```

  Do not create `claude-buddy-v1.4.0` yet.

### Task 8: Launcher-installed v1.4.0 runtime proof and release tag

**Files:**

- Modify: `PROGRESS.md` only after observed hardware results.
- Create: annotated tag `claude-buddy-v1.4.0` only after all acceptance checks pass.

**Interfaces:**

- Consumes: clean published `main`, mounted Launcher USB MSC Cardputer ADV.
- Produces: actual OTA runtime proof, not merely staging proof.

- [ ] **Step 1: Stage using only Launcher USB MSC**

  With device in Launcher Settings → USB MSC, run:

  ```bash
  CARDPUTER_SD_ROOT='/Volumes/NO NAME' ./cardputer stage claude-buddy
  ```

  Expected: staged raw v1.4.0 image SHA matches manifest; no direct serial flash command runs.

- [ ] **Step 2: Install using Launcher UI**

  On device: exit USB MSC, open `tools/Claude-Desktop-Buddy-v1.4.0-*.bin`, choose **Install**, then let Launcher boot `ota_0`.

  Expected: Launcher reports install success and Buddy boots.

- [ ] **Step 3: Prove installed runtime**

  Run, one transport at a time:

  ```bash
  ./cardputer debug claude-buddy serial
  ./cardputer debug claude-buddy ble
  ```

  Check: boot/USB identity, official BLE advertising/pairing, permission prompt, bridge configuration redaction, and optional bridge connect. Do not open USB CDC while diagnosing stable BLE/Wi-Fi unless serial evidence is required; it may reset/re-enumerate the ESP32-S3.

- [ ] **Step 4: Record outcome and tag release**

  If all observed checks pass:

  ```bash
  git add PROGRESS.md
  git commit -m 'docs: record Claude Buddy v1.4.0 Launcher smoke test'
  git tag -a claude-buddy-v1.4.0 -m 'Claude Buddy v1.4.0 Launcher OTA release'
  git push origin main refs/tags/claude-buddy-v1.4.0
  ```

  If any check fails: record exact observed failure in `PROGRESS.md`; do not create or publish v1.4.0 tag.

## Plan self-review

- Spec coverage: task 1 proves historical binary; task 2 preserves all real legacy source; tasks 3–6 replay Launcher/security/RFID2 work and establish v1.4.0; task 7 verifies/re-writes/publishes safely; task 8 proves Launcher-installed runtime separately.
- No placeholder terms remain. Every code-bearing task defines exact paths, required interfaces, commands, or assertions.
- Names are consistent: historical environment `cardputer-adv`; final environment `cardputer-adv-launcher-ota`; historical tag `claude-buddy-v1.3.0`; final tag `claude-buddy-v1.4.0`.
