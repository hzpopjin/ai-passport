**English** · [简体中文](h2h-pocket.zh_CN.md)

# H2H Pocket — IAN edition

An AI Passport companion: a pixel room, seven Hearts2Hearts songs with full-song and chorus playback, a lemon-catching game, eight collectible cards, a moving-heart fan badge, and optional RandomDance account binding. Music, hearts, cards and settings remain local. The account page starts BLE for phone provisioning and uses Wi-Fi for account binding and official theme downloads. The original RandomDance iOS application and its public interfaces are unchanged.

## Controls and collection

| Page | Up / Down | Short OK | Long OK |
| --- | --- | --- | --- |
| Home and menus | Previous / next, wraps | Open selection | Back |
| Room | Greeting / heart / outfit / background | Perform action or cycle unlocked item | Home |
| Player | One/two short presses: volume +5 / -5 per press; three short presses: previous / next song; long: forward / back 10 seconds | Play / pause; cancel countdown; retry after failure | Music menu; music continues, pending countdown cancels |
| Music menu | Choose player, song, full/chorus, repeat mode or countdown | Apply selection | Home |
| Game | Move left / right | Pause / resume | Leave without a reward |
| Collection | Eight cards, then outfit and background | View owned card or cycle equipped item | Home |
| Card | — | Use as fan badge | Collection |
| Result | — | Another round | Home |
| Badge | — | — | Home |
| Settings | Volume / countdown / mode / AI Passport | Cycle value or open account | Home |
| AI Passport | Confirm / refresh / forget Wi-Fi | Confirm a scanned account, request the selected official theme, or clear Wi-Fi credentials | Settings |

## Account, official themes and NFC

Open **Settings → AI PASSPORT**, then use the RandomDance MiniApp or a compatible Web Bluetooth browser to connect to `RDP-<last eight device ID characters>`. Confirm the six-digit BLE code on the card and phone. The MiniApp sends a 2.4 GHz Wi-Fi SSID and password. The card stores credentials in NVS and connects over TLS to the RandomDance apps API. Scan the five-minute QR code from the card using the MiniApp or website, sign in with a formal RandomDance account, then press OK on the card to approve binding. A QR claim alone does not bind the card. Clearing Wi-Fi is available from the account page.

The account website and MiniApp offer only official sky, lemon and pink theme packs. Select a theme there, connect via BLE, then manually request **Refresh**. The card downloads a 16-byte package over Wi-Fi, verifies SHA-256 and format, then applies and persists the embedded outfit and room indices. This is an appearance selection, not a firmware rewrite or music update. Local progress and settings are not uploaded. BLE, Wi-Fi and TLS coexistence and on-device display require physical validation.

The product has a passive NTAG213 tag. The ESP32-C3 cannot write it through this firmware. Set `CARD_URL` to the registered device's `card_url`; `python3 tools/h2h/nfc_ndef.py "$CARD_URL" new-ntag213.bin` prepares an NDEF user-memory image containing only `https://ai-passport.randomdance.cn/card/<slug>`. Writing it to a physical tag is a separate step using an NFC writer. The webpage reads live visibility from the apps API. A card is private by default; its owner may publish nickname, avatar and applied theme via the website or MiniApp. No account token or personal data is stored in the tag. Existing tag contents and phone tap behavior require physical inspection.

The backend implementation is staged as [a reviewable patch](../../integrations/randomdance-backend/passport.patch), with an additive database migration. The site needs a dedicated SSO client for `https://ai-passport.randomdance.cn/sso/callback`, plus `PASSPORT_SSO_CLIENT_ID`, `PASSPORT_SSO_CLIENT_SECRET` and deployment of the official theme catalog. These server steps have not been applied to production.

Long press is 500 ms. Its release never triggers a short press. In the player, hold Up to move forward 10 seconds or Down to move back 10 seconds, once per hold without changing volume. Three short presses of the same key, each within 800 ms of the previous press, switch to the previous (Up) or next (Down) song in the current full/chorus set. One or two short presses change volume by five per press after the 800 ms window. A different key or OK completes pending volume changes immediately. A three-press song switch starts at the beginning and preserves paused/playing state; it cancels a countdown. Seeking while paused stays paused; seeking cancels a countdown and clamps to the valid song range. Skipped time and decoder pre-roll earn no hearts. A new song optionally begins after five seconds. Pausing and returning from a countdown cancel it. Playback resumes from the paused position; changing songs or switching full/chorus mode starts that recording at the beginning. The full/chorus preference persists; changing it while paused stays paused. Sequential and random playback remain within the selected set of seven recordings; single-repeat loops the current song. With one song all modes continue safely.

- Every 120 seconds of PCM successfully written to the audio output earns one heart; countdowns and pauses earn nothing. This is software accounting, not acoustic measurement.
- Games last 45 active seconds. Every five catches earns one heart, capped at five per completed round. Paused time is excluded; leaving early earns nothing.
- Each five cumulative hearts unlocks a random missing card, without spending hearts. Two/four cards unlock outfits; six/eight unlock backgrounds. After all eight cards are owned, hearts continue accumulating.
- Three outfits: sky sailor, lemon cardigan, pink stage. Three scenes: sky room, lemon garden, heart stage. Card artwork combines the original sprite poses, clothing, colors and titles.
- Volume, full/chorus mode, repeat mode, countdown preference, listening remainder, hearts, cards, equipped outfit/background and badge are saved. Current song, playback position and unfinished games are not restored after restart.
- NVS uses a versioned, checksummed record. Invalid data uses defaults; NVS errors never trigger an automatic erase. Events save within three seconds; listening progress checkpoints every 30 seconds. Sudden power loss can lose progress since the last checkpoint.
- Backlight dims from 70% to 20% after 30 idle seconds, except during a game or badge display. This is not deep sleep.


The room toolbar uses four embedded pixel icons, from left to right: waving hand, heart, shirt and landscape. They mean greet, send a heart, change outfit and change background. Direction arrows and a filled circle represent selection and the OK key. The character area contains no decorative text; outfit/background names appear below the sprite only when selected.

Sprite export ignores near-transparent noise. All 15 frames have a 74-pixel character height and aligned feet; heart and idle poses use the same scale. Shared firmware/preview motion adds gentle sway, occasional waves, side steps, small hops and floating hearts.

## Desktop preview

From the repository root:

```sh
python3 tools/h2h/preview.py
```

Open <http://127.0.0.1:8877>. Use the displayed buttons or arrow keys/Enter; Escape returns. Click the audio button to allow browser playback. The preview executes the same C model as the firmware, renders the same image assets on a 240 × 320 canvas, and uses the browser's Opus decoder. It does not emulate LVGL, ESP32 scheduling or the speaker. A C compiler and Python 3.9+ are required. Preview progress is stored separately in `build/h2h-preview-save.bin`.

## Pack music

FFmpeg with libopus and ffprobe are required. Original inputs are read only.

```sh
python3 tools/h2h/pack_music.py --input /path/to/song.mp3 --title 'Lemon Tang' --artist Hearts2Hearts
```

For several songs, pass `--manifest /path/to/songs.json` containing:

```json
[
  {"file": "song.mp3", "title": "Lemon Tang", "artist": "Hearts2Hearts"}
]
```

Relative song paths resolve next to the manifest. For full/chorus pairs, list all complete songs first and matching chorus clips in the same order, then pass `--song-count 7`. Output is `assets/music/h2h/`: Ogg files for preview, fixed-size raw Opus frame files for firmware, a C catalog, a CMake embedding list, and a JSON capacity/hash report. Parameters are Opus CBR **16 kbps, mono, 48 kHz, 20 ms**. Missing files, malformed metadata, mismatched pairs, conversion/decoding failure, non-40-byte packets, or more than 5 MiB of encoded music fail. The packer accepts 1–99 recordings. Songs are embedded in the next firmware build; the device has no file-upload interface.

The 14 recordings were read from the user's private collection, under its full-song and chorus folders. The seven complete songs are Lemon Tang, FOCUS, ICONIC HEART, Pretty Please, RUDE!, STYLE and The Chase, each paired with its chorus clip. Source MP3 files remain untouched and are staged only under ignored `build/h2h-source-audio/`. The 14 Ogg files total 3,280,459 bytes; the raw frames embedded in firmware total 3,156,760 bytes. Audio resources are ignored by Git and are not included under the repository's MIT license.

After changing titles or UI text, rebuild the font before building firmware:

```sh
python3 -m venv build/asset-env
build/asset-env/bin/pip install Pillow==11.3.0 fonttools==4.60.0
npm install --prefix build/font-tools lv_font_conv@1.5.3
build/asset-env/bin/python tools/h2h/build_font.py
```

The source Noto Sans SC font, OFL license and generated subset are in `assets/fonts/h2h/`. Sprite exports can be rebuilt with `build/asset-env/bin/python tools/h2h/convert_sprites.py`. See [asset provenance](../../assets/README.md#h2h-pocket-assets).

## Build and delivery

Use ESP-IDF **5.5.3**, target ESP32-C3, 8 MiB Flash, with the unchanged default NVS/PHY/factory partition table. Activate your ESP-IDF environment, then run:

```sh
./tools/validate.sh
```

The gate runs repository checks, existing host tests, H2H model/worker/asset tests, an isolated firmware build, merged-image verification and archive creation. The capacity check requires at least 512 KiB free inside the application partition and a merged file no larger than 8 MiB. `esp_audio_codec` is pinned to 2.6.2, LVGL to 9.5.0. The audio worker reads consecutive 40-byte Opus packets directly from Flash without a seek index and outputs through bounded PCM buffers. Seeking decodes and discards 500 ms of history, exceeding the [RFC 7845](https://www.rfc-editor.org/rfc/rfc7845.html#section-4.6) minimum and matching verification across all 14 recordings, with correct pre-skip and end trimming. It survives page changes. Chinese labels use the generated font throughout; unsupported new titles cause the font generation check to fail.

Deliverable: `build/FoloToy-AI-Passport-full.bin`, to be flashed at **0x0** only after user approval. Its content-addressed archive in `build/firmware/<SHA-256>/` includes the matching ELF/MAP and a manifest. Verify with `python3 tools/archive_firmware.py verify <archive-directory>`. A merged flash can reset stored data; it does not authorize a full-chip erase. Consult the [firmware data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).

Implementation map: `main/h2h/h2h_model.c` owns behavior and save validation; `h2h_audio.c` owns decoding; `h2h_ui.c` owns LVGL; `main/main.c` connects BSP input, NVS, battery and worker status. Tools are under `tools/h2h/`. No microphone data is recorded.

## Acceptance status

Build and host results, artifact hashes and preview checks are recorded in the local generated `build/h2h-validation.json`. Host decoder tests use fakes to exercise the actual audio worker's PCM trimming, pause/resume, forward/backward seeking, paused seek, uncredited pre-roll, restart and failure paths; they do not prove ESP32 Opus decoder performance. FFmpeg separately verifies the actual complete Ogg stream.

Optional host seek verification: install a local libopus shared library and run `python3 tools/h2h/verify_seek.py`. This compares each full decode and five seek windows against FFmpeg using libopus, including pre-skip/end trim and decoder convergence; it does not measure ESP32 performance.

Device tests for **this revision have not run**. Prior firmware flash/startup logs do not validate this revision. After authorized flashing, check:

1. All labels and pages, three outfits × three rooms, greeting/heart/dance animations and badge hearts; ensure rounded physical display corners do not obscure content.
2. At least 30 minutes of playback while navigating, gaming, changing volume and pausing/resuming. Check sound, dropouts, responsiveness, resets, and reported free heap.
3. Countdown cancellation, 10-second seeking during playback and pause including both boundaries, triple-short-press song switching without volume changes, one/two-press volume changes, one-song random/repeat behavior, game pause and a single end-of-round payout.
4. Accrue a heart/card, equip an item, wait at least three seconds, power-cycle, and confirm restoration. Listening checkpoints need up to 30 seconds. Verify actual brightness and battery reading.

Built firmware and desktop previews are not evidence of device audio quality, battery life or thermal behavior.

## Hosted Python website

The production URL is `https://ai-passport.randomdance.cn`. The server checkout is
`/www/wwwroot/ai-passport.randomdance.cn`. The deployed website still serves its
previous 32 kbps, one-song release; this firmware music-library change has not
been published to the website. Nginx serves the allowlisted `public/` directory
and forwards API calls to Gunicorn on `127.0.0.1:8878`. The
`h2h-pocket-web.service` systemd unit starts on boot and restarts on failure.
Run exactly **one worker** with eight threads: the shared C bridge is protected
by a lock while each visitor has a separate model snapshot. The opaque visitor
cookie is HttpOnly, Secure and SameSite=Lax; saves live in SQLite outside the
public directory. Saves checkpoint after three seconds, so abrupt shutdown can
lose recent progress. A different browser or cleared cookie starts a new room.
Visitor cookies and inactive save retention are 30 days. This is separate from
the hardware save and the local preview save; there is no account sync.

Build a fresh release with `python3 tools/h2h/package_web.py build/web-release-N`.
Audio is excluded by default. Add `--include-audio` only for an explicitly
authorized public audio release. The user authorized the earlier website upload
of the complete Lemon Tang test song, not this 14-recording library. Rebuild the
bridge on Linux:

```sh
mkdir -p build
cc -O2 -std=c11 -Wall -Wextra -Werror -shared -fPIC -Imain/h2h main/h2h/h2h_model.c main/h2h/h2h_motion.c tools/h2h/web_bridge.c -o build/h2h-web.so
```

Configuration sources are in `tools/h2h/deploy/`. Each release goes under
`releases/`; `current` points to the active one. The Python environment and save
database remain under `shared/`. Publish by switching `current` and restarting
only `h2h-pocket-web.service`. Roll back by restoring the previous symlink and
restarting the same service; never delete `shared/data`. Do not upload the whole
repository or use the local development HTTP server as the public service.
Gunicorn's installed version is retained in the release `requirements.lock`.
Server-side validation covers request size, ranges, same-origin writes, visitor
isolation and corrupt-save handling. The frontend skips background polling while
a request is pending so slow connections do not accumulate a queue of polls.

Operational checks: `systemctl status h2h-pocket-web`, `journalctl -u h2h-pocket-web`,
and `curl https://ai-passport.randomdance.cn/healthz`. Deployment verification is
recorded locally in `build/h2h-web-deployment.json`. The website does not flash a
device. See [Gunicorn settings](https://gunicorn.org/reference/settings/) and
[Nginx static/proxy configuration](https://nginx.org/en/docs/beginners_guide.html).
