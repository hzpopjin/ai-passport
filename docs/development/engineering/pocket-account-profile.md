<p align="right"><a href="pocket-account-profile.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Pocket account profile

This candidate adds a bound account information page with a 64 x 64 avatar,
username, canonical public account UID, and website account points. The page
shows the last phone-synchronized snapshot. Game hearts and stars remain
independent save fields. A profile failure after the durable game ACK leaves
the completed game sync intact and marks profile sync pending.

The native home includes **User information** as an independent seventh tab,
after Settings. Up/Down cycle all seven tabs; only the selected tab's label is
shown, with no persistent account shortcut under other tabs. OK opens the tab.
Settings retains its account entry. Long OK returns to the originating
home tab or Settings item. The title and entry do not depend on a profile snapshot
or backend availability: a bound card without a snapshot shows
**Waiting for phone profile sync**. The native labels use the Chinese text given
in the paired document. Unbound cards retain the pairing gate; DEV remains an offline
preview and does not expose account rights. Navigation origin is transient and
does not change a saved record.

Header title, hearts and percentage use the same16px font and shared baseline.
The percentage has a fixed width measured from `100%`. Bluetooth reserves its
slot while disconnected. The explicit charging-icon layout is tested separately;
the current BSP only provides SOC/voltage, so SOC100 or USB presence never enables
that icon as a guessed charging state.

## Account and transport contract

The phone reuses its existing WordPress user-center adapters and authenticated
cache: iOS `WPAuthManager.getUserInfo()` and `WPAPIClient.fetchPointsBalance()`,
and MiniApp `api.getWPUserInfo({persist:false})` and `api.getWPPointsBalance()`.
These use `/wp-user-center/v1/user/info` and `/wp-user-center/v1/points/balance`.
No new `GET /api/v1/passport/phone/profile` deployment is required. Existing
phone activation, signed ownership and durable game synchronization remain
required; this change only replaces the source of public profile metadata.

The canonical UID is the authenticated WordPress account ID, from 1 through
UINT32_MAX, never a QR/card ID. Responses must match that actor; logout,
account/session changes, rejected credentials and deleted accounts stop profile
writes. The phone normalizes names to NFC, removes controls/bidi overrides and
incomplete surrogate scalars, and truncates whole code points to 63 UTF-8 bytes.
Empty names use `UID <account_id>`. Only public username, UID, avatar and points
are copied; email, phone number and credentials stay on the phone.

Existing user-center reads refresh metadata and points where available. Network
failure can retain the same authenticated actor's cached public metadata and
previous valid balance. The profile cache is fenced by account and session;
a new session does not inherit another actor's points. Missing/invalid points
are JSON null, never fabricated zero or game currency. A valid zero is retained.
The existing wire value `points_status: fresh` means a numeric snapshot is
available, including cached snapshots; it is not a realtime freshness guarantee.
The card explicitly labels it **Last-synced points** and shows that all metadata
is the last phone-synchronized snapshot. No offline points spending is enabled.

The phone downloads a public HTTPS avatar without forwarding JWTs or cookies.
The source is at most 2 MiB, at most 4096 pixels per side and 4,000,000 pixels
in total, and uses static PNG/JPEG/WebP. Each phone centers an aspect-fill crop,
composites transparency on white, resizes to 64 x 64 and encodes RGB565
little-endian. The card accepts exactly 8192 raw bytes and does no URL fetching
or image decoding. A missing avatar uses the common placeholder SHA256
`aa86924dfd5550307ff3e8840c21df0248b2af0784f2580b262210877e6f0c3b`.
Native image resampling can produce different pixels on the two platforms;
no cross-platform bit-identical resampling is promised. SHA equality always
refers to the normalized bytes, not the URL.

The encrypted, authenticated BLE phone RPC keeps outer protocol
`pocket-phone-jwt-v1`, a 32-character lowercase hex `request_id`, current
`expected_epoch`, `action`, and one `profile` object. No account JWT, avatar
URL, email, or telephone number is sent to the card. All profile actions reject
an unbound/locked card, pending unbind, wrong account or wrong epoch.

| Action | Exact profile keys |
| --- | --- |
| `profile_begin` | `account_id`, `sha256`, `format`, `width`, `height`, `bytes`, `username`, `uid`, `points_status`, `points` |
| `profile_chunk` | `account_id`, `sha256`, `offset`, `data` |
| `profile_commit` | `account_id`, `sha256` |

`format` is `rgb565-le`, dimensions are 64 x 64, and `bytes` is 8192.
Each chunk is 512 bytes encoded as 1024 lowercase hex characters; offsets are
512-byte aligned from 0 through 7680. The existing 4096-byte RPC limit stays
unchanged. Status advertises optional `profile` with `enabled`, fixed format
and dimensions, committed `sha256`/`revision`, pending `upload_sha256` and
`next_offset`, and committed public metadata. Old cards omit it; a disabled
capability is skipped. Same complete metadata/hash writes nothing. Same avatar
hash with changed metadata copies verified pixels locally, with no BLE pixel
chunks. Phones verify the final hash and metadata after commit.

## Flash persistence and ownership

The repository default partition table remains the original minimal layout.
Profile and persistent logging are disabled there without data-partition I/O.
The combined opt-in candidate is defined by
`partitions-pocket-profile-candidate.csv` and
`sdkconfig.pocket-profile-candidate`, used after `sdkconfig.defaults` in an
isolated build.

| Partition | Offset | Size |
| --- | ---: | ---: |
| `nvs` | `0x9000` | `0x6000` |
| `phy_init` | `0xf000` | `0x1000` |
| `factory` | `0x10000` | `0x7c8000` |
| `pocket_profile` (data/0x41) | `0x7d8000` | `0x8000` |
| `pocket_logs` (data/0x40) | `0x7e0000` | `0x20000` |

The adapter checks all five exact partition identities, bounds and flags before
any profile I/O. Profile storage has two 16 KiB banks. Each has a 256-byte
versioned/CRC header, pixels at +0x100, sixteen CRC-protected progress markers
at +0x3000, and a CRC footer at +0x3fc0. Only an inactive bank is erased.
Each chunk is read back before its progress marker is acknowledged. Recovery
checks the persisted prefix; complete pixels must pass SHA256 before a footer
commits a new revision. Boot chooses a fully valid committed bank, preserving
the old avatar on interrupted writes and recovering a lost final ACK.
Storage uncertainty forces reread before choosing a bank to erase. Reads and
writes are bounded by the 32 KiB profile partition; streaming SHA uses 512 bytes.

Metadata is tied to both account and ownership epoch. Unbind immediately hides
the previous profile, pending status, and avatar from the current UI/RPC view.
Old bytes can remain physically in the banks; this is logical account isolation,
not secure erasure. Profile storage does not change game/V4 save formats, NVS
layout, Bluetooth bonds, or security settings. Its work uses the existing
8192-byte phone worker; no additional profile task is created. The UI holds one
8192-byte pixel view with small metadata and invalidates the LVGL image cache
when the committed revision changes. LVGL access remains under the BSP lock.
Dedicated PIN UI takes priority and hides account information and management.

## Validation and remaining checks

Host tests cover byte-prefix failures through erase, headers, every chunk and
marker, commit, reboot resume, lost ACK, owner/epoch isolation, UTF-8, null points,
and unchanged hash. Actual C phone RPC interoperates with Swift and MiniApp.
The ESP adapter is tested for zero I/O on absent/mismatched layouts. Actual
C/LVGL rendering checks avatar replacement, maximum UID/points, unavailable
points, owner isolation and dedicated PIN priority with a 48 KiB host pool.
Xcode simulator tests build the real App. These are not physical card or
account end-to-end acceptance.

All static UI strings are covered by the regenerated 492-glyph subset. Arbitrary
Unicode usernames are stored correctly but may contain glyphs not in the
embedded font; those characters show a placeholder. Current public Latin
usernames are covered. Avatar decoding, Bluetooth coexistence, flash stalls,
heap/stack minima and power interruption still need physical acceptance after
explicit authorization. MiniApp source tests do not replace WeChat base-library,
permissions, domain allowlist, or device decoder checks.

This source preparation requires no new profile backend deployment and does
not itself flash a partition migration. Reclaiming former factory tail
`0x7d8000..0x7fffff` requires a separate exact artifact/data-impact approval.
The old tail has not been read or proved blank. Use the final matching app and
partition table handoff; do not use a padded full image, bootloader replacement,
whole-chip erase or broad `flash_args`. Rollback uses the retained previous app
and minimal table while preserving NVS/PHY/bonds; profile/log bytes may remain
inert. A single-factory interrupted update can require USB recovery.
