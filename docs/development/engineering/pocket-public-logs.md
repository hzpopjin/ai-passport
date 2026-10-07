<p align="right"><a href="pocket-public-logs.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# STAYC public circular diagnostics candidate

This is a deliberate product candidate, not the repository default layout and
not evidence of a device migration. Keep `partitions.csv` unchanged. Build in
an isolated directory with `sdkconfig.defaults;sdkconfig.pocket-logs-candidate`.
The adapter does no Flash I/O and creates no task unless all four partition
descriptors match the candidate exactly, are unencrypted and writable.

## Partition and data impact

| Region | Existing offset/size | Candidate offset/size |
| --- | --- | --- |
| NVS | `0x9000 / 0x6000` | same |
| PHY | `0xF000 / 0x1000` | same |
| factory | `0x10000 / 0x7F0000` | `0x10000 / 0x7D0000` |
| pocket_logs, data subtype `0x40` | absent | `0x7E0000 / 0x20000` |

An approved migration writes the partition-table sector `0x8000–0x8FFF` and the
verified application at `0x10000`. Obtain the exact image length, rounded sector
range and SHA from its matching build evidence. When no valid log history exists, the logger erases
`0x7E0000–0x7E0FFF`, then writes a 64-byte header and a 64-byte BOOT record. Over
time it erases and overwrites all of `0x7E0000–0x7FFFFF`. This range previously
belonged to factory: do not assume arbitrary tail data is disposable, or read
and erase it without approval of this complete range. Existing valid history
(such as after re-enabling following rollback) is recovered and appended, so an
initial rotation may use another sector within the same owned range.

These operations do not change NVS/PHY offsets, read NVS secrets, or deliberately
erase saves, ownership, relay keys or BLE bonds. Normal application/NVS updates
continue. Never use whole-chip erase or a padded merged image for this migration.
Rollback restores the matching previous factory application and minimal table,
leaving NVS intact; log bytes can remain inert in the restored factory tail.
Any extra data/security change needs a separately described scope. A power loss
during a single-factory app/table update may require USB recovery; it is not a
dual-slot atomic update. Retain both matched rollback segments.

The candidate reserves no external resource partition. Subtract the exact new
app length from `0x7D0000` to report remaining app space; this is not resource
capacity. Wi-Fi resource ingress is not implemented by this change.

## Public record schema and retention

The ring has 32 sectors of 4096 bytes. Each sector has a 64-byte header and 63
64-byte records. Validated sector/record sequence numbers and IEEE CRC32 select
the durable prefix; no global head pointer is rewritten. A torn slot forces
rotation, and any uncertain I/O disables this instance until a fresh boot/open.
The latest committed sector survives an interrupted next-sector erase/header.

Fields are version/kind, record sequence, boot ordinal, uptime, an eight-byte
public firmware ELF prefix, reset reason, page/game/key/event/cloud/result/error
enums, saturating error count, heap metrics in KiB, and main/logger stack minima
in 64-byte buckets. Page/cloud `255` means unknown; key/event zero means none.
Cloud phase/result are local state summaries, not proof of server persistence.
BOOT carries the previous validated context with the current reset reason and
firmware prefix. RTC context is opportunistic after a warm reset; power-on or
invalid RTC falls back to the last durable Flash record. A hard reset may lose
the most recent volatile context.

No strings, account/device identifiers, QR URLs, PIN, JWT, credentials, relay or
BLE keys, addresses, opaque payloads, save contents, raw UART or memory dumps are
accepted by the schema. No Flash write runs in a panic/ISR or BSP callback.
Exact exception PCs remain a separately bounded serial diagnostic.

The low-priority worker requests a 4096-byte stack. Static service, RTC and lock
have a compile-time budget of 1024 bytes; report the actual target ELF sizes.
Startup requires at least 16 KiB free heap and an 8 KiB largest block. Allocation,
layout and storage failures disable only logging. Main setters update RAM/RTC;
the worker writes BOOT once, then at most one record per 60 seconds, coalescing
errors. Flash/cache stalls still require physical gameplay/audio testing.

After wrapping, 1954–2016 records remain at sector granularity: about 32.6–33.6
hours at one checkpoint/minute. Boots and errors consume the same bounded
history. This is not an indefinite archive. There is no automatic log upload or
new UI/USB export command. The portable visitor returns validated public records;
`tools/h2h/decode_pocket_log.py` decodes an explicitly supplied offline partition
capture and never emits raw sectors. Obtaining a capture is a separate authorized
operation and must first verify the exact dedicated partition.

## UI terminology and Bluetooth

The top-bar heart and the reward/collection term “hearts” use the same cumulative
`hearts` field. Each five hearts unlock a card; shop purchases spend separate
`star_balance`, not hearts. The heart cushion is a decoration, displayed with consistent heart terminology
without changing the saved index, layout, version or canonical model contract.
The Bluetooth glyph uses the compiled Montserrat 14 symbol font and the actual
GAP-connected state. Advertising alone does not show it; it does not certify
authentication or cloud sync. Its reserved slot prevents reconnect from shifting
the heart/battery digits. Long titles stay on one truncated header line.

## Validation and release boundary

Run `tools/validate.sh --static`, the default segmented firmware gate, and the
isolated candidate build/layout/8 MB/RAM checks. Host tests cover NOR bounds,
CRC corruption, wrap/wear, 4227 byte-cut power failures, RTC fallback, write rate,
layout rejection and resource/I/O faults. Real C/LVGL render tests separately
cover connected/disconnected/reconnected header states and maximum counts.
Report build, host and device results separately. Candidate migration and
physical log/recovery/audio tests remain held until their accurate data-impact
scope is approved by the coordinating task.
