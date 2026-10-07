[简体中文](stayc-native-port-contract.zh_CN.md) · **English**

# STAYC native gameplay and storage contract

This document describes the authorized local development implementation. The final original V4 model source was not recovered; local comparisons use the retained v15 WASM/JS and older C scene sources. Those references, directly adapted game/model, motion, life, decoration and reaction modules, trace fixtures and supplied art are excluded from the public snapshot pending redistribution permission. No complete original-source recovery or independently buildable public release is claimed. See the [development guide](stayc-development.md) and [copyright notice](copyright-notice.md).

## Gameplay boundaries

The local target has six members, two actual outfits each, 384 atlas poses, three rooms, eight rich interactions and six full-song/chorus pairs. Native member/wardrobe navigation surrounds the retained gameplay contract. A legacy third outfit value is preserved; the interface offers only the two actual STAYC outfits and describes the legacy value rather than inventing new art.

Life actions, finite card reactions, catch/echo/journey rules, four decoration slots and wallet/purchase behavior are compared against the pinned local reference. Pause freezes scene clocks, cancel gives no completion award, first-completion rewards do not repeat, and purchases use prepare → validate → durable commit → finish. Preview/cancel leaves committed balances unchanged. Host differential agreement is evidence for the covered traces, not complete physical timing or feature parity.

Player controls compare the actual local JS/WASM player against the native wrapper. Sequence/random/single modes and full/chorus sets remain separate. Previous/next and seek preserve pause state; seeking/countdown/decoder pre-roll do not earn listening rewards. Session history is bounded to 256 entries, discards older entries and is not persistent. UI scene commands do not mutate gameplay state while rendering.

## Storage and phone transport

The default card transport is `pocket-phone-jwt-v1`. Account JWTs and backend HTTP stay on the phone. `h2h_cloud.c` includes the phone bridge unless `H2H_CLOUD_HOST_LEGACY_TEST` explicitly selects the legacy host path. Earlier Wi-Fi/signed-relay and cloud48 descriptions are historical contracts, not the current normal build. See the [phone protocol](../development/pocket-phone-jwt.md).

| Record | Contract |
| --- | --- |
| Legacy base progress | 48 bytes; preserved as its own projection, never reinterpreted as V4 |
| Ownership | Existing 384-byte record and ownership epochs |
| Raw V4 gameplay | 92 bytes, version 4; explicit checksum and legacy migration validation |
| Native complete save | 164 bytes, schema/version checks, little-endian fields, base48 at offset 16, gameplay92 at 64, paid-journey mask at 156, checksum at 160 |
| Member preference | Existing 24-byte, epoch-scoped selection record |

Full-save bank/pointer journaling binds complete progress to ownership and supports reboot recovery and immutable in-flight acknowledgements. Older 160-byte local wrappers and 48/72/76-byte gameplay records migrate only through validated codecs. Invalid, future or unreadable records remain protected from overwrite. The active protocol synchronizes the complete 164-byte record; clients must not substitute the old 48-byte save example.

A conflict retains complete local and remote records and locks gameplay. Explicit physical restore adopts the remote record while preserving local recovery data; reconnect completes the restore acknowledgement. Newer local bytes remain dirty after an older in-flight receipt. A different ownership epoch does not inherit the former owner's wallet, gameplay or paid claims. Unbind and owner changes are generation guarded. Development sandbox mode restores normal progress on exit and does not confer account rights or persist ordinary gameplay.

## Partition and runtime boundaries

The default table remains NVS `0x9000/0x6000`, PHY `0xF000/0x1000`, factory `0x10000/0x7F0000`. External resource/profile/log banks are absent. Optional candidate tables do not authorize changing a device layout. Application headroom is not external resource capacity; two full-sized OTA slots are not provided.

The LVGL pool is 48 KiB; one decoded atlas frame is 15,360 bytes. Linker DIRAM headroom is not boot/runtime heap. Measure heap minima, largest blocks and task stack minima during real concurrent audio, display, BLE and phone synchronization. UI access outside the LVGL task requires the BSP lock, and slow operations stay out of button callbacks.

## Validation and device handoff

With separately authorized local inputs, run `./tools/validate.sh --all --segmented`. Verify the retained archive using `tools/archive_segmented_firmware.py verify <archive-directory>`. The gate builds from tracked defaults, checks the 8 MiB minimal layout, ELF/app identity and application headroom, and runs actual source against host stubs and local reference fixtures. It creates no padded merged image in this mode and writes no device.

Real-card startup, Chinese text, repeated gestures, acoustic playback, BLE reconnect/activation, complete save restore, ownership changes, power-loss persistence and runtime memory remain separate acceptance checks. Identify the exact artifact, device, compatible layout and data impact before approved flashing. Use the [firmware data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data). Do not publish firmware embedding the excluded inputs.
