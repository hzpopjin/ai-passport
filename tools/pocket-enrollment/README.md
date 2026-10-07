<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Trusted Pocket relay enrollment

This tool fills the single-key enrollment gap for the ESP32-C3 / 8 MiB card.
It is a temporary headless application, a secure local-terminal client, and a
server-local CLI. It does not modify the normal R6 application or add an HTTP
enrollment endpoint. Manufacturing access to the Mac and SSH server is trusted;
the receipt proves agreement with the supplied new key, not hardware attestation.

## Exact data effects

The temporary application reads the existing public `h2h_cloud/id`, factory
MAC and `relay_key` length/presence. It reads neither the legacy `secret` nor
the existing relay key. It refuses an absent/malformed public ID, any existing
relay key, NVS initialization error, stale ADC sample, target mismatch, missed
button transition or expired operation. The user must release all buttons,
hold OK for at least three seconds, then release within sixty seconds.

The only explicit NVS insertion is the missing 32-byte
`h2h_cloud/relay_key`, followed by commit. There is no erase API, partition dump,
key replacement, resource partition, eFuse or security-setting operation.
The helper turns off the backlight so a retained old QR image cannot look active.
Normal NVS initialization can perform SDK recovery; this is not a byte-for-byte
preservation guarantee under storage corruption or interrupted power.

The server inserts one missing `passport_relay_keys` row with purpose
`independent-enrolled-relay-key`, revision 1 and `revoked=false`. Existing device
state, owner, epoch and saves are not selected or rewritten. The reviewed owner
must still match at commit. Existing unknown enrollment refuses; a server-only
retry compares the operator's own new key inside SQL without selecting old key
bytes. Logging or unreviewed triggers block the operation without changing DB
settings. Administrator changes to logging during the operation remain outside
the tool's control. Normal protected database storage, binary logs and backups
may persist the newly inserted key and must be included in approval.

If the public device record is missing, a separate explicit
`allow_create_relay_only` approval can create an unbound record with no save,
owner or pairing, and `credentialHash=null`. Existing R6 HTTPS authentication
rejects the non-string verifier. This neither registers the old HTTPS token
nor repairs that authentication path. Relay registration is distinct from
account binding and activation.

## Review and synthetic validation

Use the already installed ESP-IDF 5.5.3 Python (cryptography and pyserial),
CMake/Ninja, Node.js, host compiler and OpenSSL development library. No command
here installs tools or changes system configuration. Run from this directory:

```sh
python -B validate.py --idf "$IDF_PATH" --openssl /path/to/openssl
IDF_COMPONENT_MANAGER=0 idf.py -C device -B /absolute/ignored/build/enrollment \
  -DPOCKET_BOARD_REPO=/absolute/ai-passport \
  -DSDKCONFIG=/absolute/ignored/build/enrollment/sdkconfig build
python -B archive_helper.py create /absolute/ignored/build/enrollment \
  --board-repo /absolute/ai-passport \
  --archive-root /absolute/ignored/build/enrollment-segmented
```

The focused gate uses fixed, clearly public synthetic vectors; it creates no
live key, SSH connection or USB operation. It compiles the actual device adapter
with genuine cJSON/HMAC and metadata-only NVS probes, plus sanitizer state and
host/server fault tests. Build hashes bind application/ELF/MAP and the unchanged
NVS/PHY/factory layout; neither compilation nor simulation is device acceptance.
The regular repository gate must also pass for the preserved normal application.

## Action-time approval and secure entry

Approval must name the unique USB fingerprint and factory MAC, existing public
device ID, reviewed helper BIN/ELF hashes, restored R6 hash, exact server/source
hash, expected registry owner, optional relay-only identity and an owner-only
recovery directory. Flash only the reviewed application at `0x10000` after
matching the existing layout; exclude NVS and PHY erase sectors. A temporary
helper flash is a separate artifact from the normal R6 flash and must be included
in the approval. Keep Secure Boot / Flash Encryption as observed; this tool does
not make an unencrypted device hardware protected.

The reviewed JSON approval is a local operator gate, not a signed authorization
service. Keep it mode 600 and mark `approval` as `approved` only after explicit
user authorization. Its exact schema is enforced by `check_approval()` in
[`host.py`](host.py). Use an existing directory owned by the operator with mode
700. Place the reviewed [`server.cjs`](server.cjs) in the approved backend's
ignored `build/pocket-enrollment` directory; no application restart is required
for this standalone CLI. Preserve other files and verify its SHA before use.

```sh
python -B host.py apply --approval /private/approved.json --port /dev/cu.usbmodem101
```

Run this command in the user's own secure Mac Terminal. Enter a new recovery
passphrase there through `getpass`, never in chat, argv, environment variables or
Codex tool input. A matching 32-byte key is generated only after explicit
approval, unique-device checks and secure terminal entry. An AES-GCM capsule
using scrypt is written exclusively and file/directory-fsynced before any key is
sent to the card. The capsule binds public identity and operation as AAD. It
contains only this operation's new key, never an old device secret. Keep the
passphrase and capsule separately. Python/JS immutable objects and operating
system memory do not provide a guaranteed complete RAM scrub; the mutable key
buffer and card input buffers are cleared, and secrets never intentionally enter
logs, command arguments or plaintext files.

The public operation approval crosses existing strict-known-host SSH. The new
key crosses encrypted SSH stdin to parameterized SQL after a valid device write
receipt. No stored secret is exported and no cookie or account token is extracted.
The account must log in through the supported App/MiniApp flow separately.

## Interrupted-operation boundaries

| Interruption | Result and allowed recovery |
| --- | --- |
| Capsule persistence fails | No arm or NVS write; correct the storage problem. |
| Physical confirmation or identity fails | No key write; inspect the refused target. |
| Set/commit/USB ACK is lost | Device write is indeterminate; preserve capsule, refuse overwrite. |
| Valid device receipt cannot be persisted | Preserve capsule; no automatic server registration. |
| Valid receipt exists, server unavailable | Server-only replay with the same capsule and key. |
| DB commit/response is lost | Same-key server replay resolves committed/absent state without rotation. |
| Existing unknown key or owner changed | Stop; no overwrite, owner reset or save mutation. |

Only with a durable valid device receipt may the user decrypt the new capsule
in their secure terminal and run:

```sh
python -B host.py resume-server --approval /private/approved.json \
  --capsule /private/operation.capsule.json
```

The replay does not open or write the card. If power is lost after the NVS write
but before a durable receipt, this tool deliberately cannot infer success from
key length or rewrite the key. Restore the reviewed normal R6 application and
stop enrollment for separate, supervised reconciliation using its normal signed
relay protocol. A valid server row alone is not proof that the physical key is
matching. The present tool offers no automatic lost-device-ACK reconciliation.

After R6 restoration, use the intended account's normal login, BLE pairing,
on-card account confirmation, QR exit and reconnect/save checks. Do not enable
the local debug bypass as an activation test. Enabling v2 and restarting the
backend are separate reversible configuration/version steps; resource gates,
trust roots and partition migrations are outside this tool.
