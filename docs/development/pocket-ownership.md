<p align="right"><a href="pocket-ownership.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Pocket Room ownership protocol groundwork

This change adds a host-tested, SDK-independent `h2h_ownership` state machine.
It is intentionally **not yet connected** to `main.c`, `h2h_cloud.c`, or NVS.
The current firmware therefore does not implement the new activation gate,
offline unbind, epoch-aware cloud backup, or resource installer. Do not flash
this branch as a completed implementation of those features.

The authoritative cross-repository contract is
`random-dance-platform/docs/POCKET_PASSPORT.md` (2026-10-04).
One device has one owner; an account may have many devices with independent
progress and backups. Migration is explicit and replaces the target snapshot;
there is no automatic merge or last-writer-wins across devices.

The pure state machine locks an unactivated device, allows an already activated
device to remain playable offline, and locks immediately when local unbind is
requested. The pending operation ID and expected epoch survive a serialized
state round-trip. A matching server acknowledgment clears the pending request;
old responses and same-epoch owner changes are rejected. An epoch change keeps
play locked until local save/resource reset is durably acknowledged.

Integration requirements:

- Store validated, versioned state atomically in NVS, with corruption detection;
  fail closed on read/write errors. Do not trust the legacy `bound` byte.
- Keep the independent device identity while unbinding. Lock UI immediately;
  perform slow persistence/network operations in the worker. Report unbind as
  accepted only after the pending record is durable.
- Authenticate sync, commit pending unbind first, then reconcile ownership,
  then upload/download same-epoch saves. Never replace expected epoch on retry.
- Reset old progress/resource selection durably before acknowledging the epoch
  barrier. Isolate queued save/resource results by epoch and generation.
- Adapt the existing register/pairing_id HTTP flow to begin/claim/confirm and
  the new sync contract. A production provisioning trust flow is still needed;
  an unregistered self-generated credential must not authorize enrollment.
- Keep BLE aa00/aa01/aa02, 20-byte frames, opcodes 1–5 and SC/MITM/passkey.
  No phone JWT or shared long-lived key is sent to the card.
- The 16-byte theme index is not an asset pack. Current factory-only Flash has
  no safe OTA slot. Resource and firmware manifests, size/RAM budgets, verified
  inactive storage, atomic switch, power-loss recovery and rollback remain
  integration work. No production signing keys are created here.
- NTAG213 is a passive fixed random-slug HTTPS URL. It does not prove ownership
  and is not rewritten by the MCU. Writing/locking requires physical testing.

Validation: `tests/test_h2h_ownership.c` passes with C11 warnings-as-errors.
The full static gate currently fails on absent generated audio assets such as
`assets/music/h2h/track_000.ogg`; the repository check and preceding host suites
pass. Firmware build is blocked by missing ESP-IDF 5.5.3. No device was flashed.
