[简体中文](README.zh_CN.md) · **English**

# RandomDance MiniApp Passport snapshot

This directory carries only the AI Passport integration from the separate RandomDance MiniApp checkout. The inspected base commit was `e0d27b5ca81b4f927ac546b8b11464bcb326eaad`. Unrelated client changes are excluded.

`files/` contains the four `pages/ai-passport/ai-passport.*` page files and two `utils/ai-passport*.js` modules. Copy its contents into the MiniApp root. `wiring.patch` adds the page route, settings action and settings entry against the inspected base:

```sh
git apply --check /path/to/ai-passport/integrations/randomdance-miniapp/wiring.patch
git apply /path/to/ai-passport/integrations/randomdance-miniapp/wiring.patch
```

Inspect the target client's own AGENTS instructions and current changes before copying or applying. If the page is already registered, reconcile the existing implementation instead of duplicating it. The current API utility must provide `isLoggedIn`, `ensureBusinessLogin`, `unwrapBusiness`, and `request` with `auth: "business"` and `omitData` support; these are supplied by the inspected base and no shared API replacement is included.

The page supports formal-account QR claim, device listing, official theme selection, BLE Wi-Fi provisioning and refresh, public/private NFC card controls, URL copying and unbinding. Credentials are sent to the card over its authenticated BLE protocol and are not stored in MiniApp profile data.

JavaScript syntax and JSON checks can run locally. WeChat Developer Tools compilation, formal-account flows, system Bluetooth pairing, physical Wi-Fi provisioning, theme download and NFC behavior remain unverified. No client upload or publication is part of this snapshot. Deploy the [backend integration](../randomdance-backend/README.md) before expecting `/passport` calls to succeed.
