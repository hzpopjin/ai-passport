[简体中文](h2h-agent-handoff.zh_CN.md) · **English**

# H2H Pocket agent handoff

## Repository and scope

Continue on `feature/h2h-pocket` in [hzpopjin/ai-passport](https://github.com/hzpopjin/ai-passport). Its parent is [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport); the local application started from upstream commit `0b9e4c8`. Keep upstream `main` as the board/demo baseline. Read [AGENTS.md](../../AGENTS.md) first and use its task routing and five required skills. The original [application manual](h2h-pocket.md) remains authoritative for controls, persistence, audio and acceptance.

The requested product combines account/QR binding, website or MiniApp appearance selection, and phone NFC tap to a profile card. Current theme packs contain appearance indices for embedded assets; they do not rewrite firmware. A firmware update mechanism, rollback and arbitrary theme packaging are still open work.

## Implementation map

| Area | Files and behavior |
| --- | --- |
| Application | `main/main.c`, `main/h2h/h2h_model.c`, `h2h_motion.c`, `h2h_ui.c`: input, state, progress, animations and redesigned LVGL screens |
| Audio | `main/h2h/h2h_audio.c`, `assets/music/h2h/`: isolated Opus playback, seeking, full/chorus pairs and catalogs |
| Device account | `main/h2h/h2h_ble.c`, `h2h_cloud.c`: authenticated BLE, Wi-Fi credentials in NVS, TLS registration, five-minute QR pairing and physical confirmation |
| Themes | `main/h2h/h2h_theme.c`, `tools/h2h/build_theme_catalog.py`, `integrations/randomdance-backend/official-themes/`: three official 16-byte packs with SHA-256 verification |
| Local preview | `preview/h2h/`, `tools/h2h/preview.py`: shared C model, canvas and optional browser audio; account page is a visual placeholder |
| Website | `tools/h2h/web_server.py`, `passport_account_web.py`, `passport_ble.js`, `passport_card_web.py`, `package_web.py`, `deploy/`: visitor preview, SSO session, device controls and public/private NFC card route |
| Backend | [integration guide](../../integrations/randomdance-backend/README.md) and `passport.patch`: additive NestJS module, database migration, device authentication, pairing, ownership, theme and card APIs |
| MiniApp | [integration snapshot](../../integrations/randomdance-miniapp/README.md): six Passport files and a focused route/settings patch for the separate RandomDance MiniApp |
| NFC | `tools/h2h/nfc_ndef.py`: generate a URL-only NDEF image for the passive NTAG213 tag; an external writer must program the physical tag |

## Restore local music before the complete gate

The Git/source package excludes personal `.ogg` and `.bin` recordings, original MP3s, `build/`, local saves, firmware/debug binaries, `sdkconfig`, managed dependencies and credentials. Music metadata, generated C catalogs, images, fonts and the font license are included. The two font copies and generated images are required assets, not build caches.

For the exact catalog, obtain the matching `track_000` through `track_013` `.ogg`/`.bin` resources from the owner and place them in `assets/music/h2h/`. Check hashes against `catalog.json`. They are seven complete songs followed by the seven corresponding chorus clips, in the same order. Do not upload the recordings to GitHub.

Alternatively provide authorized local source files through the packer manifest and regenerate the catalogs:

```sh
python3 tools/h2h/pack_music.py --manifest /path/to/songs.json --song-count 7
```

The manifest format is documented in [Pack music](h2h-pocket.md#pack-music). Regeneration can change durations and hashes; review `catalog.json`, `tracks.c` and `music.cmake`. FFmpeg with libopus and ffprobe are required. Avoid silently replacing the owner's recordings with test audio.

Without restored audio, `python3 tools/h2h/preview.py` still supports visual navigation. Audio requests fail, asset integrity tests fail, and the firmware embed step cannot complete. Use pure model/theme/audio-worker tests and account/card/NFC tests for work that does not depend on the recordings; run the complete gate after restoring them.

## Validation and current acceptance

The handoff preparation runs `./tools/validate.sh` on the local checkout with its audio resources. The GitHub release notes record the result for the published commit. A new clone must restore audio before reproducing the complete gate.

```sh
source /path/to/esp-idf-v5.5.3/export.sh
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh
```

Report Build, Host tests, Device tests and Unverified separately. Physical acceptance of the current revision is pending: audio stability, Chinese glyphs, memory with BLE/Wi-Fi/TLS, reconnect/error paths, NVS persistence, account ownership confirmation, theme refresh and phone NFC tap. Obtain approval before flashing. No device consent is included in this GitHub upload.

## Continue account and NFC integration

1. Review and apply the staged backend patch to the current backend checkout; inspect schema, logging redaction and registration throttling. Follow that backend's release rules before migrating or restarting it.
2. Register a dedicated SSO client for `https://ai-passport.randomdance.cn/sso/callback`; configure private `PASSPORT_SSO_CLIENT_ID` and `PASSPORT_SSO_CLIENT_SECRET`, and deploy the official theme directory. Credentials belong outside this repository.
3. Package the Python site and perform SSO/ownership/error/session tests. The existing hosted preview is an older one-song release according to the application manual; this handoff does not update it. Inspect live state before deployment.
4. Integrate the focused MiniApp snapshot into the separate client; compile in WeChat Developer Tools and validate Bluetooth permissions, encrypted pairing, Wi-Fi provisioning and QR claim/physical confirmation with a formal account. The client has not been uploaded or published by this handoff.
5. Register the device, use its returned stable `card_url`, inspect the physical tag, and write the NDEF URL with an external NFC writer. Verify private/public card behavior, owner-controlled visibility, avatar/nickname rendering and real phone tap. The passive tag stores neither tokens nor profile data.

The public contract uses `https://apps.randomdance.cn/api/v1/passport` and `https://ai-passport.randomdance.cn`. Backend endpoints and SSO deployment for this feature remain pending; website layout success does not prove account binding works.
