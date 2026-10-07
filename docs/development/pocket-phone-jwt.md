<p align="right"><a href="pocket-phone-jwt.zh_CN.md">简体中文</a> · English</p>

# Phone JWT activation candidate

The phone holds the business JWT and calls backend 3.17.0 phone endpoints. The card receives no JWT, reads or writes no relay key, and never accesses the backend. The authenticated BLE full ID is a locator; the toy policy trusts the phone report rather than proving factory identity.

Normal flow: QR, system BLE PIN, phone_activate via JWT, phone_apply via BLE, atomic owner/full-save journal, main UI. Same-account retries preserve epoch. Full 164-byte records retain schema4/version2/checksums, CAS revisions, local generation fences, explicit conflict restore, verified durable ACK and reconnect recovery. Local pending unbind uses a JWT owner receipt and phone_unbind; old retries never touch a newer server owner.

Existing NVS keys, settings, saves, full-bank layout, secure boot and flash encryption settings are retained. Production builds compile h2h_phone only. h2h_cloud_legacy remains solely for explicitly selected legacy host tests and rollback; no device key, signed relay or card-to-backend HTTP is compiled into the candidate.

On conflict the card locks and keeps both local and remote saves. From the QR page open the diagnostic menu, select Restore and click OK twice to adopt the remote save explicitly. Movement, timeout or owner/model generation changes cancel confirmation. The former local record stays in recovery; reconnecting the phone sends the full-record restore ACK.

The 8MB default partition has no external resource banks. It reports zero capacity; Wi-Fi resource transfer is unavailable in this candidate. Do not label resource selection as installed or use application-partition space. Direct phone-to-card resource ingress requires follow-up implementation and an approved compatible resource layout.

Build and host tests do not prove hardware acceptance. Flashing and production enablement are paused. Reconcile existing owner/epoch first if local state is newer than backend; do not erase or pretend an old bound card is factory-new. Basic cloud restore must complete before promotion. R6 rollback preserves NVS but JWT-only registered devices have no old relay key and cannot use R6 online relay.
