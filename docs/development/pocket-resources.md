<p align="right"><a href="pocket-resources.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Pocket visual resources and Flash budget

This implements real image bundles, not the old 16-byte theme index. A bundle
replaces fifteen 64×80 RGB565A8 character poses, four 16×16 icons, three room
colors and printable ASCII member/group/title labels. Existing songs, gameplay,
fonts and built-in fallback remain. Format 2 supports `member_visual` and
`group_visual`; neither kind claims to contain music or a firmware update.
Other members/groups require actual approved source artwork. Only the tracked
IAN artwork is available; no official catalog or production assets are fabricated.

## Budget and opt-in development layout

`python3 tools/h2h/resource_budget.py` counts actual tracked image/font arrays
and the audio catalog. The fourteen catalog audio binaries are missing, so the
3,411,595-byte embedded-data lower bound is not a measured application image.

| Item | Bytes |
| --- | ---: |
| ESP32-C3 Flash, no PSRAM | 8,388,608 |
| Default factory app; resource capacity zero | 8,323,072 |
| Catalog audio, seven full tracks plus seven choruses | 3,156,760 |
| Fifteen character frames; four icons; font bitmap | 230,400; 3,072; 21,363 |
| One visual bundle including 512-byte header | 233,984 |
| Slot including 4,096-byte metadata sector | 238,080 minimum |
| Development A/B slots, each | 262,144 |
| Unused bytes in each slot | 24,064 |
| Development factory app | 7,798,784 |
| Actual app limit preserving 512 KiB headroom | 7,274,496 |

`partitions.csv` and `sdkconfig.defaults` are unchanged. The opt-in
`config/partitions-pocket-visual.csv` preserves NVS at 0x9000 and PHY at 0xf000,
keeps factory at 0x10000, and puts pocket_a/pocket_b at 0x780000/0x7c0000.
Use a fresh isolated build with defaults in order:
`sdkconfig.defaults;config/sdkconfig-pocket-visual.defaults`. Then run
`python3 tools/h2h/resource_budget.py --app <actual-app.bin>`; excess size fails.
No flashing is authorized. A previous factory image might extend into these
new slots; a layout change requires a separate reviewed installation plan.

Two full audio+visual slots while retaining embedded fallback have a
10,266,811-byte lower bound, exceeding Flash by at least 1,878,203 bytes before
application code, alignment and runtime metadata. A full music switch needs
approved smaller assets or removal of embedded audio with a trusted preloaded
external fallback and measured app/RAM, or larger hardware. It is not delivered.
Firmware A/B OTA additionally needs otadata, two measured executable slots,
boot validation and rollback. Factory-only and this visual layout do not enable
firmware OTA. A 60 MB web bundle is incompatible with this hardware.

## Signed format and state transitions

The backend contract is `random-dance-platform/docs/POCKET_PASSPORT.md`, version
3.9.0. The signed UTF-8 JSON tuple is:

```text
["pocket-visual-v2",kind,"esp32c3-pocket-8m",2,resource_id,version,
 min_firmware,max_firmware,233984,15360,sha256,key_id]
```

Ed25519 verifies all fields plus SHA-256 of the bytes. IDs are 1–32 lowercase
ASCII letters, digits, underscore or hyphen; versions are positive uint32.
Labels use printable ASCII supported by the existing font. Binary layouts are
fixed little-endian, canonical zero padding; dimensions, sizes, offsets,
reserved bytes and palette ranges are checked before use. Unknown keys,
models, formats, firmware ranges, budget excess, modified signatures/bytes and
network version downgrades are rejected. A per-resource high-water table holds
sixteen IDs and persists across ownership changes. Equal-version reinstalls
must have the same hash; a seventeenth family fails before erasing a slot.

`h2h_resource_runtime` accepts only the exact opt-in layout. It advertises no
capacity in the default layout. `h2h_resource_key_at` defaults to no trusted
public keys; reviewed production key injection remains a prerequisite. The
backend catalog is read-only and empty without explicit configuration. No
production signing key, registration or upload endpoint is added.

The network worker downloads authenticated 1 KiB ranges into the inactive
slot, with exact Content-Range/length checks and an ownership generation guard.
Interrupted downloads restart from zero on a later sync, at most one attempt
per sync; no partial content is activated. SHA, signature and the binary header
are checked before writing slot metadata last. A server `verified` ACK approves
only the current selection revision/epoch. It does not mean displayed.

The renderer maps validated Flash, holds the LVGL/resource locks, and atomically
commits a 1,536-byte `h2h_resources/visual_v2` NVS active/previous record before
rebuilding images. The old mapping stays alive through screen deletion, then
is unmapped. Downloads cannot erase/write an active or mapped slot. No full
bundle is copied into heap; the largest image is 15,360 bytes. The opt-in defaults reserve a 12 KiB main-task stack; a smaller configured
stack disables installation. The worker stack increases from 8 to 16 KiB; engine copy, hash buffers, TLS, LVGL and mappings
still need measured peak RAM and timing on the board.

Commit or mapping failure keeps the old view, reports failed and requires
manual Sync/retry. `installed` is ACKed after persistence and rendering; it is
still not instrumented display proof. Boot validates the active slot, falls
back to a verified previous slot or built-in visuals, without lowering network
version high-water marks. Corrupt NVS disables resource installation instead
of erasing or resetting it. Epoch changes hide previous-owner visuals; pending
work cannot cross ownership generations. Offline activated use retains the
last valid local visuals until the next authenticated sync.

## Reproduction and boundaries

`python3 tools/h2h/build_visual_resource.py /tmp/pocket-visual` produces an
**unsigned** bundle from tracked IAN arrays, never a trusted release. Backend
resource tests sign those real bytes with temporary in-memory fixture keys.
`tests/test_h2h_resource.c` checks signature/capacity/CAS/rollback and every one
of 230 payload/metadata writes with a torn write and reboot. Actual main/cloud
C is also run through the test SDK/NVS/HTTP adapter against real Nest routes,
including integrity, ownership changes, mapping/storage failures and retries.

Build: NOT RUN — ESP-IDF 5.5.3 and original Opus files are absent.
Host tests: resource suites PASS; complete static gate fails at missing original
audio assets. Tests do not replace ESP-IDF or Flash hardware validation.
Device tests: NOT RUN — flashing is prohibited.
Unverified: actual image size/headroom, peak RAM, task/Flash concurrency, mmap
and LVGL lifetime, long-label layout, radio/TLS/BLE, real power interruption,
production artwork/trust roots, full music switching and firmware OTA.

## Vendored cryptography attribution

`components/pocket_crypto` contains unmodified Monocypher **4.0.2** core and
optional Ed25519 files from [the upstream release](https://github.com/LoupVaillant/Monocypher/tree/4.0.2).
The original dual BSD-2-Clause/CC0 license is retained as `LICENCE.md`.
Only the project CMake integration is added. The component's upstream Markdown
is explicitly registered in `tools/check_repo.py`; first-party documentation
and all sensitive-content/conflict scans remain active. API references:
[Monocypher Ed25519](https://monocypher.org/manual/ed25519),
[ESP-IDF 5.5.3 partition API](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/storage/partition.html).

| Upstream file | SHA-256 |
| --- | --- |
| monocypher.c | 02174117935699d418443c75a558a287deb06ef8cf7c1adced61d9047d2f323d |
| monocypher.h | fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3 |
| monocypher-ed25519.c | 97d581639dfa72be08a6d57deb7d79b736be001cb416819cab196d22559d242b |
| monocypher-ed25519.h | 3a3035181f991a158d0e1c7567258f0bae8ba0f1f23c5512b4a1db1b3c9730ce |
| LICENCE.md | a5781770269d2516e52ba4863f790c10a16da4089a1e81823aee19ff1e9026b0 |
