[简体中文](README.zh_CN.md) · **English**

# RandomDance AI Passport integration

This directory is a staged implementation for the existing `apps.randomdance.cn` backend. It has **not** been applied to production.

- `passport.patch` adds the NestJS Passport module, device authentication, five-minute QR claim plus physical confirmation, account-owned theme selection, and public/private NFC card metadata. It also makes Passport API logging metadata-only so device secrets and pairing codes are excluded from request and response logs.
- `official-themes/` contains a catalog and three SHA-256-addressed 16-byte theme packs that select assets already embedded in firmware. The server reads them from `PASSPORT_THEME_DIR` (default `/var/lib/random-dance/passport-themes`). They do not contain executable firmware.
- The patch includes `backend/scripts/migrations/passport-devices.sql`. It creates only `passport_devices` and `passport_pairings`; inspect existing schema and take a rollback point before running it. Do not run it as part of a local build.

## Review and release preparation

Apply the patch to a checkout matching the inspected backend baseline, then review it against any intervening production changes. Run `tsc --noEmit`, backend tests and the backend's own release gate. Before rebuilding or restarting production, follow its AGENTS instructions: bump the feature version in the package files, update `docs/api.md` and related bilingual release documentation, and include a changelog entry. Configure registration throttling at the ingress, secure `PASSPORT_THEME_DIR` ownership, and verify Passport paths never write credentials or pairing URLs to access logs. Never copy production `.env` or database contents into this repository.

The website needs a dedicated WordPress SSO client for `https://ai-passport.randomdance.cn/sso/callback`. Put `PASSPORT_SSO_CLIENT_ID` and `PASSPORT_SSO_CLIENT_SECRET` in the site service's private `shared/passport-sso.env` (mode 0600; accessible to the `www` service account), then test login, logout and revoked sessions. Publish the website bundle, backend module, migration and official themes as one coordinated release. No production mutation is part of this patch.

## NFC

Once a device has registered, use its server-assigned `card_url` to generate an NTAG213 NDEF user-memory image with `tools/h2h/nfc_ndef.py`. The card's tag is passive, so an external writer must program it. The stable tag URL contains no personal data or token. The account owner controls whether nickname, avatar and applied theme are public; default visibility is private. Verify the existing tag memory and phone tap behavior before writing it.
