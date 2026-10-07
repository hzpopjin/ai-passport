[简体中文](pocket.zh_CN.md) · **English**

# Pocket user guide

Pocket is a companion application for AI Passport, with groups and members as optional content. Editions can use different group artwork, music and room configurations; this guide is the common entry point for everyday controls, music, games and saves. The public branch is a development snapshot; it does not include a ready-to-flash image or all required private materials. See the [development guide](stayc-development.md) if you are starting from source, and the [copyright notice](copyright-notice.md) before using media.

## Choose your edition

Check the installed Pocket edition, included group/members and authorized media first. **STAYC is an optional edition, not the only content direction for Pocket**; the earlier H2H/IAN edition has its [own guide](h2h-pocket.md). The current development branch compiles the STAYC selection. It does not establish arbitrary runtime group switching or arbitrary downloadable group packs. Changing editions requires matching firmware, assets and a save-compatibility plan from the maintainer. Member, song and outfit counts depend on the installed edition.

## Start here

1. Turn on the device. An unactivated card shows a setup QR and guide address; an activated card opens the main interface.
2. For first activation, use a phone client compatible with `pocket-phone-jwt-v1` and its matching backend. This repository's older web/MiniApp integration snapshots do not establish compatibility with that protocol.
3. Sign in on the phone, allow Bluetooth and camera access as requested, and follow the setup QR or client connection flow. Select the intended `RDP-…` card and compare the displayed Bluetooth PIN before approving pairing.
4. Complete activation in the phone client. Keep the phone near the card until it reports a durable successful card acknowledgement and the card opens the main interface. A scanned QR or connected Bluetooth icon alone does not prove activation.
5. After durable activation, local play can continue without continuous phone connectivity. Reconnect the same account to synchronize progress. Initial activation and online synchronization require the compatible client/service.

The phone retains your login token and accesses the backend. The card receives bounded Bluetooth messages, stores ownership and saves locally, and does not receive the JWT or use Wi-Fi for backend access in the default build. A stored Bluetooth bond can be reused after reconnecting.

## Learn the three buttons

**Up / Down** choose an item. **Short OK** opens or confirms it. **Long OK** usually goes back. Long press starts at 500 ms; releasing a long press does not also trigger a short press. Some player actions wait for the multi-press window, so a short delay is expected.

| Screen | Up / Down | Short OK | Long OK |
| --- | --- | --- | --- |
| Home | Choose room, music, arcade, collection, badge, settings or user information | Open the selected screen | Follow the on-screen hint |
| Room | Choose greeting, heart, wardrobe, background, life, decor, member or interaction | Open/perform selection | Home |
| Member/interaction picker | Choose a member or interaction | Apply member or start interaction | Room |
| Interaction/life scene | Follow the selected scene's hint | Pause/resume; replay a completed scene | Cancel/back |
| Wardrobe/background/decor | Preview an item | Apply owned item; confirm a purchase when shown | Cancel/back |
| Music menu/song list | Choose song, full/chorus, playback mode or countdown | Apply/open selection | Previous screen |
| Player | One/two short presses: volume ±5 per press; three: previous/next song; hold: seek +10/−10 seconds | Play/pause or cancel countdown | Music menu |
| Catch game | Move the basket left/right | Pause/resume | Leave without completion reward |
| Memory game | Enter the shown directions after the demonstration | Follow the phase hint | Leave/back |
| Collection/card | Select a card | Open card or use it as a badge | Previous screen |
| Badge | Follow the screen hint | Follow the screen hint | Home |
| Settings/user information | Select available entry | Apply/open selected entry | Return to the entry's origin |

In the player, make three presses of the **same** direction within 800 ms between presses to change song. One or two presses become volume changes after that window. Up selects previous; Down selects next. Seeking and song changes preserve pause state and cancel a countdown. Long OK leaves the player while playback continues. Sequential, random and single-repeat modes stay within the selected full-song or chorus set. Session history retains the most recent 256 entries and is not saved across restarts.

## Explore and collect

- Members, outfits, rooms and interactions depend on the installed edition. For example, the current optional STAYC local set has six members, two outfits per member, three rooms and eight rich interactions. Its supplied media is not distributed in the public repository.
- Music played through the PCM output earns one heart per 120 seconds of successfully written audio. Pauses, countdowns and skipped time earn none; this accounting does not measure whether you heard sound.
- Complete games, life activities and journey goals to earn the rewards shown in the interface. Leaving a game or scene early does not earn its completion reward. Some accomplishments pay only once.
- Every five cumulative hearts unlock a missing card until all eight are owned. Hearts and shop stars are separate: purchases spend stars. Account website points are a third, independent balance.
- Previewing or cancelling a shop choice does not spend stars. A purchase finishes only after the selected change is durably saved; a storage failure keeps the prior balance and equipment.
- The fan badge displays a chosen owned card. A legacy third outfit record may remain from an older save; the current STAYC example offers its two actual outfits; other editions follow their own real materials.

## Saves, account information and privacy

Progress, earned items and settings are saved locally. The complete phone save protocol carries a 164-byte record containing gameplay and ownership-scoped metadata; older 48-byte integration examples describe a different protocol. Restarting does not resume an unfinished game, animation or playback cursor. Abrupt power loss can lose the most recent uncommitted progress.

A changed owner starts a separate ownership epoch. Do not clear NVS, rebind or erase the device to repair a synchronization error. When a local and remote save conflict, play is locked and both copies are retained. Restore explicitly adopts the remote copy and preserves the former local record for recovery; it does not merge balances. On the setup QR screen, diagnostics can be opened by releasing all buttons, tapping Up–Down–Up–Down, holding OK for at least three seconds, releasing it, then clicking OK when confirmation appears. Select Restore and click OK twice within five seconds without moving selection. Reconnect the phone to finish the restore acknowledgement. Long OK leaves diagnostics.

User information shows the last synchronized profile. A Bluetooth icon means connected, not authenticated or synchronized. Arbitrary account-name characters can exceed the embedded font coverage. Avatar/profile persistence requires the reviewed profile candidate partition; the default factory-only layout does not provide it. External resource installation and circular diagnostics likewise need separately reviewed layouts. Do not expect them from a default build.

## If something goes wrong

| Symptom | What to try |
| --- | --- |
| Setup QR remains | Confirm the client supports the current phone protocol, sign into the intended account, enable Bluetooth and reconnect. A legacy client may be incompatible. |
| Bluetooth code fails or connection drops | Compare the card and phone PIN, move them closer and retry from the setup/account page. Check the phone's Bluetooth permission. |
| Setup link is unavailable | Use the compatible client's device connection flow. Published setup routes and account service availability are separate deployment requirements. |
| No sound | Raise volume and resume playback; ask the firmware maintainer to check the local audio inputs and speaker. A build does not prove physical audio output. |
| Save conflict or storage error | Stop repeated purchases/restores. Keep local data, record the displayed error and obtain maintainer help. Restore only when you intend to adopt the remote save. |
| Empty avatar or unsupported resource | Check the installed partition variant. The default layout has no external profile/resource bank. |
| Square boxes in an account name | Use a name covered by the embedded font or request a font update. Correct UTF-8 alone does not ensure glyph coverage. |

For support, include firmware version, device/phone model, client version, exact steps and sanitized error codes in [an issue](https://github.com/hzpopjin/ai-passport/issues). Omit full QR URLs, PINs, tokens, passwords, personal account details and raw logs. Never install an unverified firmware or erase the whole chip as a routine troubleshooting step. Updates require a verified image, compatible layout and explicit approval; see the [data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).
