<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## H2H Pocket assets

- `images/h2h/ian-atlas-source.png`: original transparent 5-pose × 3-outfit artwork generated with the built-in image tool on 2026-09-29. Art direction: IAN-inspired auburn hair, navy/white sailor dress, lemon cardigan and pink stage outfit; idle, wave, heart, dance and celebration poses; bright cream/sky/lemon/pink palette. Public style reference: [official Hearts2Hearts IAN post](https://x.com/Hearts2Hearts/status/2064905464455053601). This is original fan art, not an official portrait or endorsement; the reference photograph is not bundled.
- `images/h2h/ian_<outfit>_<pose>.png`: 15 transparent 64 × 80 sprite frames. `sprites.c` stores RGB565A8 planes, 15,360 bytes per frame (230,400 total). The conversion tool ignores alpha below 128 when measuring bounds, preserving the original alpha inside the crop. Nearest-neighbor export aligns feet and gives all 15 characters a 74-pixel height. Firmware uses integer 1×/2× scales. Room backgrounds, card layouts and moving hearts are drawn by the application; no third-party background artwork is embedded.
- `fonts/h2h/NotoSansSC.ttf`: [Google Fonts Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc), distributed under the included `fonts/h2h/OFL.txt`. `h2h_font_16.c` is a 16 px, 4 bpp subset for all UI strings and packed song metadata. `glyphs.txt` is its inventory. The matching preview font is `preview/h2h/font.ttf`. Regenerate with `tools/h2h/build_font.py`; glyph coverage is checked before export.
- `music/h2h/track_000`–`track_013`: seven Hearts2Hearts complete songs and matching chorus clips read from the user's private library, under its full-song and chorus folders. The source MP3 files remain unchanged in ignored `build/h2h-source-audio/`. Conversion uses 16 kbps CBR Opus, mono, 48 kHz and 20 ms frames; decoded PCM output is 16-bit. `.ogg` files serve the local preview; `.bin` files contain fixed 40-byte Opus frames embedded in firmware. The 14 Ogg files total 3,280,459 bytes and firmware frames total 3,156,760 bytes. Music files are Git-ignored. Rights remain with their holders; no redistribution license was supplied for these recordings.

Usage and regeneration commands: [H2H Pocket guide](../docs/assets/h2h-pocket.md).

UI toolbar icons use four original 16 × 16 pixel masks in `images/h2h/icons.json`; `tools/h2h/build_icons.py` exports the matching `icons.c` RGB565A8 assets (3,072 bytes). The desktop preview reads the same masks. They require no emoji or symbol font.
