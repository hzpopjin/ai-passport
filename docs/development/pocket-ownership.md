<p align="right"><a href="pocket-ownership.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Pocket Room ownership and cloud integration

The product application now connects `main.c`, `h2h_cloud.c`, the pure
`h2h_ownership`/`h2h_pocket` engines, and NVS. This is tested source, not a
released or board-validated firmware. The authoritative contract is
`random-dance-platform/docs/POCKET_PASSPORT.md` (2026-10-04, backend 3.8.1).
One device has one owner; an account may have many devices with independent
progress. Migration explicitly replaces the target, without merging.

The actual main loop opts into the activation gate before input or audio.
Unpaired devices cannot leave the account screen to play. The gate covers all
key events, countdown/game timers, listened rewards, seeking, audio completion
and playback requests. Activated devices remain playable offline. Playback and
local saves check both ownership epoch and model generation, so queued work
cannot use a previous owner's model.

## Durable state and unlinking

`h2h_cloud/pocket_v2` is one 384-byte, version 2, little-endian NVS blob with a
corruption checksum. It includes ownership, pending unlink ID/expected epoch,
48-byte schema-1 save, revision, conflict/restore acknowledgment and one local
recovery snapshot. The checksum is not cryptographic tamper protection.
Ownership changes and previous-period save reset use the same commit. No-op
syncs do not rewrite NVS. Corrupt, truncated or unreadable records fail closed.
Legacy bound bytes and unscoped saves remain untouched and cannot unlock or
be automatically imported. Device identity stays separate and survives unlink.

Unbind and conflict Restore require two physical clicks within five seconds;
movement cancels confirmation. Button callbacks only enqueue input. The main
application task commits the local unlink before reporting it accepted; the
network worker then submits the original pending ID/epoch. Successful commit
locks immediately and stays locked after reboot until reconciliation. Failed
commit locks the current runtime and reports failure. If the old record remains
in Flash, reboot can reload that old activation: an unsuccessful persistence
operation is not a durable unlink. Actual NVS power-loss behavior requires
board tests. Remote unlink locks on the next successful sync.

## HTTP and save synchronization

The anonymous register and old pairing_id HTTP flow are removed. Requests use
TLS certificate verification and per-device authentication headers, never a
phone JWT or shared secret. Begin returns a five-minute QR; logged-in claim
only selects a claimant; a physical card confirmation captures that exact
code/account/epoch. Sync and atomic NVS reset must succeed before unlocking.
The existing identity generator cannot enroll itself: trusted server-side
provisioning/identity injection remains a separate production prerequisite.

Sync runs every 15 seconds when connected and on manual Sync. Pending unlink
is settled before ownership and saves. Obsolete unlink IDs are explicitly
rejected without rebasing or writing the current owner's save. Epoch changes
clear old progress/recovery before adopting only the current-period cloud
snapshot. Record loss can recover that snapshot without uploading defaults.
Old or fractional epochs, wrong schema/length/hash and invalid saves are rejected.

Dirty saves upload with revision CAS. Lost committed-upload responses retry
idempotently. Delayed acknowledgments do not overwrite newer local input.
Ordinary conflicts stop play and retain both snapshots until explicit Restore.
A phone-confirmed migration first persists the supplied snapshot and then sends
its revision/hash acknowledgment; fresh uploads remain blocked until it is
accepted. Offline target progress is retained in one recovery slot. There is
no recovery export/UI or version history yet; another restore can replace this
slot, and a new ownership period clears it.

BLE aa00/aa01/aa02, 20-byte frames, opcodes 1–5 and SC/MITM/passkey are unchanged.
Opcode 5 requests sync; it does not install a resource pack. The account screen
also exposes Forget Wi-Fi, Unbind and Restore. QR/PIN/action layout needs real
240×320 display and camera readability tests.

## Resources, NFC and validation boundaries

The 16-byte theme index is not an asset pack. Factory-only Flash has no safe
OTA slot. Separate resource/firmware manifests, measured Flash/RAM budgets,
verified inactive storage, atomic switch, power-loss recovery and rollback
remain implementation work. No partition or production signing root changed.
NTAG213 carries a fixed random-slug HTTPS URL, not ownership proof or a secret;
the MCU cannot rewrite it. Server public fields are opt-in; domain routing and
physical tag writing/locking remain unverified.

Host tests for ownership, activation and durable pocket state pass with C11
warnings-as-errors. `tests/build_h2h_cloud_host.py` builds actual main/cloud C
against a test-only SDK/NVS/HTTP adapter using pinned cJSON and real host crypto.
Run its binary normally and with `--activated-main`; backend
`test:passport-firmware` exercises six C-to-HTTP/JWT/NFC scenarios. The adapter
does not validate actual SDK APIs, task concurrency, radio, TLS or Flash hardware.

Build: NOT RUN — ESP-IDF 5.5.3 is unavailable.
Host tests: relevant suites PASS; complete static gate FAIL at missing existing
Opus assets such as `assets/music/h2h/track_000.ogg`.
Device tests: NOT RUN — flashing was explicitly prohibited.
Unverified: full SDK build/capacity, NVS power loss, BLE secure pairing and
permissions, network failures, rendering, audio, NFC and resource/OTA installation.
