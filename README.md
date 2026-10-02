[简体中文](README.zh_CN.md) · **English**

# H2H Pocket — IAN edition

An ESP32-C3 AI Passport application with a pixel room, music playback, a lemon-catching game, collectible cards and a fan badge. Optional RandomDance account binding adds BLE Wi-Fi provisioning, QR approval, official appearance packs and an NFC profile webpage.

This repository is a fork of [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport). Application development lives on `feature/h2h-pocket`; `main` retains the upstream baseline. The source handoff is a development snapshot, with physical device and account integration acceptance still pending.

Start with [AGENTS.md](AGENTS.md), the [application manual](docs/assets/h2h-pocket.md), and the [agent handoff](docs/assets/h2h-agent-handoff.md). The handoff identifies the implementation, external client integration, validation requirements and remaining work.

```sh
git clone --branch feature/h2h-pocket https://github.com/hzpopjin/ai-passport.git
cd ai-passport
python3 tools/h2h/preview.py
```

Open <http://127.0.0.1:8877> and use the buttons or arrow keys/Enter; Escape returns. The local preview supports navigation and layout inspection. Its account screen does not emulate real SSO, BLE or NFC.

Personal music recordings are excluded from Git and the source archive. The visual preview runs without them, but playback, asset integrity tests and firmware builds require the matching local audio resources. Restore or regenerate them as explained in the [handoff](docs/assets/h2h-agent-handoff.md#restore-local-music-before-the-complete-gate). Build with ESP-IDF **5.5.3**, targeting ESP32-C3 with **8 MB Flash and no PSRAM**. A successful build does not prove device behavior.

Account/backend changes are staged for review; this snapshot does not deploy them or publish the MiniApp. The three official packs select embedded outfits/backgrounds. Firmware rewriting and arbitrary theme uploads remain future development.
