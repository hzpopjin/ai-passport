[简体中文](README.zh_CN.md) · **English**

# Pocket

A pocket companion for the FoloToy AI Passport: choose your favorite group and member, decorate a pixel room, listen to music, play catch and memory games, collect cards, and display a fan badge.

**Choose your favorite group and members.** Group names identify optional content packs. Member artwork, songs, outfits and room themes belong to the selected content; the application name is always Pocket. Available choices depend on the installed firmware and authorized resource set.

This fork's current application is on **`feature/pocket-paired-firmware`**. `main` retains the upstream hardware-test baseline. This branch is a **source development snapshot**, not a ready-to-flash public release: recordings, supplied artwork, reference JS/WASM, recorded oracle traces, and five adapted gameplay modules are excluded while redistribution rights are unresolved. A clean clone cannot run the complete application build or host gate. Local validation with those inputs does not validate a standalone public distribution.

- **Using an existing device:** read the [Pocket user guide](docs/assets/pocket.md) for activation, controls, saving and troubleshooting.
- **Building or contributing:** read the [development guide](docs/assets/pocket-development.md), [release-readiness review](docs/contribution/open-source-review.md) and [AGENTS.md](AGENTS.md).
- **Copyright:** related copyright content is for personal learning and research only; commercial use is prohibited. Read the [copyright and material notice](docs/assets/copyright-notice.md). This does not grant permission to distribute third-party material. The source covered by [MIT](LICENSE) retains that license.

```sh
git clone --branch feature/pocket-paired-firmware https://github.com/hzpopjin/ai-passport.git
cd ai-passport
python3 tools/check_repo.py
```

The repository check validates public documentation and repository conventions. Firmware development requires **ESP-IDF 5.5.3**, **ESP32-C3**, **8 MiB Flash**, and **no PSRAM**, plus the separately authorized local inputs described in the development guide. No firmware image or private asset bundle is published here.

The default build uses phone-assisted activation and synchronization over Bluetooth. The phone holds the account login token; the card does not receive it or contact the backend directly. Compatible phone clients and backend services are separate prerequisites. External appearance packs, avatar storage and circular logs require reviewed candidate layouts; the default partition table has no such capacity.

The [earlier H2H/IAN guide](docs/assets/h2h-pocket.md) and [desktop preview](docs/assets/h2h-pocket.md#desktop-preview) describe the older application. The preview can inspect that interface without music, but does not emulate the current Pocket firmware or real account activation.

Based on [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport). Report reproducible problems through [GitHub Issues](https://github.com/hzpopjin/ai-passport/issues); follow the [security policy](.github/SECURITY.md) for sensitive reports. Never attach login tokens, full activation QR links, Wi-Fi passwords, private keys or raw device logs.
