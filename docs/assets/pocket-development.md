[简体中文](pocket-development.zh_CN.md) · **English**

# Pocket development guide

Pocket is the application name; groups and members are user-selectable content. The current local build inputs use a specific group set. The inventory retains its real paths for validation and rights review; file identifiers are not the project name. The current card implementation does not establish arbitrary runtime content-pack switching. The current application branch is `feature/pocket-paired-firmware`; `main` is the upstream board-test baseline. Start with [AGENTS.md](../../AGENTS.md), the [user guide](pocket.md), [copyright notice](copyright-notice.md) and [release-readiness review](../contribution/open-source-review.md). Historical H2H instructions describe a different application and transport.

## Public source and local prerequisites

The public branch excludes materials whose redistribution permission is unresolved. Keep separately authorized inputs at their original local paths:

| Local-only input | Required purpose |
| --- | --- |
| `assets/images/stayc/` | Original image atlases/reference modules, generated `actions.c`, `sprites.c`, catalog and `stayc_visuals.bin` |
| `tools/h2h/reference/v15/` | Actual reference WASM, JS modules and integrity fixtures |
| `tests/stayc_v15_*.json` | Recorded gameplay comparison traces |
| `main/h2h/stayc_game.{c,h}`, `stayc_game_motion.{c,h}`, `stayc_game_life.{c,h}`, `stayc_game_decor.{c,h}`, `stayc_game_reaction.{c,h}` | Adapted original gameplay/scene implementation and interfaces |
| `assets/music/stayc/track_000`–`track_011`, `.ogg` and `.bin` | Six full-song/chorus pairs matching the committed catalog hashes |

These paths are Git-ignored. They are not an offered download or licensed asset package. Do not download or reconstruct copyrighted inputs without permission, commit them with `git add -f`, or publish a firmware embedding them. The current source is not independently reproducible from GitHub. The complete gate deliberately fails on missing inputs instead of silently skipping coverage. `python3 tools/check_repo.py` remains available for the public repository and documentation checks.

For an authorized local music library, `tools/h2h/pack_music.py` supports `--manifest`, `--song-count 6` and `--output assets/music/stayc`. List six full songs followed by six matching chorus clips, with identical paired titles and artist `STAYC`. Use a separate ignored staging directory when experimenting; the shipped integrity tests require the exact original catalog, so replacement recordings require an explicit catalog/test/font update. Packing requires FFmpeg/ffprobe with libopus. Reindexing existing authorized Ogg inputs with `--reindex --output assets/music/stayc` extracts packets without re-encoding. Existing files should be verified before any regeneration.

## Environment and verification

Use ESP-IDF **5.5.3**, ESP32-C3, 8 MiB Flash and no PSRAM. Follow [environment setup](../development/engineering/environment-setup.md) and [build and test](../development/engineering/build-and-test.md). Keep the pinned Managed Components and `dependencies.lock`. Host tests also need Python, Node.js, a C compiler, verified actionlint and OpenSSL development headers/libraries. `H2H_CJSON_DIR` can point to verified cJSON 1.7.19; otherwise the main host helper fills `build/host-dependencies/cjson-v1.7.19` from upstream and verifies SHA-256.

```sh
source <path-to-esp-idf-v5.5.3>/export.sh
idf.py --version
python3 tools/check_repo.py
./tools/validate.sh --static
./tools/validate.sh --all --segmented
```

If Homebrew OpenSSL is already installed on macOS, command-scoped `CPATH="$(brew --prefix openssl@3)/include"` and `LIBRARY_PATH="$(brew --prefix openssl@3)/lib"` provide its headers/libraries. Do not replace the user's shell configuration. The segmented gate builds from isolated tracked defaults and retains app, bootloader, partition table and matching ELF/MAP under ignored `build/firmware-segmented/<app-sha256>/`. Verify with `python3 tools/archive_segmented_firmware.py verify <archive-directory>`. It does not flash a device.

## Current firmware contract

The normal build includes `h2h_phone` via `h2h_phone_bridge.c`. Account JWTs and backend requests remain on the phone. `h2h_cloud_legacy.c` is selected only by explicit legacy host-test configuration; it is not the default card transport. See the [phone protocol](../development/pocket-phone-jwt.md) and [native storage contract](pocket-native-port-contract.md).

The default partitions remain NVS `0x9000/0x6000`, PHY `0xF000/0x1000` and factory `0x10000/0x7F0000`. Candidate resource/profile/log tables are review inputs and are not enabled by default. Do not confuse remaining application headroom with available resource capacity. Follow the [profile](../development/engineering/pocket-account-profile.md) and [diagnostics](../development/engineering/pocket-public-logs.md) documents for their data effects.

All non-LVGL-task rendering must hold the BSP LVGL lock. Button callbacks stay nonblocking; audio, protocol and persistence use their owning workers. Invalid/future records stay protected, restore is explicit, ownership epochs isolate saves, and purchases become visible only after durable commit. Development preview is a temporary sandbox, not activation or account entitlement.

## Hardware acceptance and delivery

A local build and host simulations do not establish real card/phone/account interoperability. Test startup, Chinese glyphs, button timing, all game/scene branches, audible playback/seek, BLE reconnect/PIN, activation, full-save conflict/restore, owner change, power loss and runtime heap/stack. The older browser preview targets H2H, not the current Pocket application.

Before any flash, identify the exact device and verified image, check the existing layout, state its data impact and obtain approval. For a compatible existing minimal layout, a reviewed app-only segment at `0x10000` may preserve NVS/PHY; do not substitute an unverified image or padded `0x0` image. Follow the authoritative [flashing policy](../development/engineering/firmware-layout.md#flashing-and-stored-data). No firmware readback is a prerequisite and no full-chip erase is authorized by a build command.

Report Build, Host tests, Device tests and Unverified separately. Public distribution remains blocked until excluded inputs are licensed or replaced and a fresh clone passes the complete gate. Publishing services, enabling candidate layouts and distributing firmware are separate actions.
