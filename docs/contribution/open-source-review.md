[简体中文](open-source-review.zh_CN.md) · **English**

# Pocket public-source readiness review

Review date: **2026-10-07**. Scope: the current `feature/stayc-paired-firmware` source snapshot for [hzpopjin/ai-passport](https://github.com/hzpopjin/ai-passport). This is a source sync, not a tag, release, firmware download, upstream merge or service deployment.

## Publication boundary

The owner requested excluding materials with unresolved redistribution permission. The public snapshot therefore omits all STAYC visual inputs and generated image data, the reference JS/WASM directory, recorded v15 trace fixtures, and five directly adapted source/header pairs (game/model, motion, life, decor and reaction). Music remains excluded. All are preserved locally and Git-ignored; no firmware/debug bundle is published. The [copyright notice](../assets/copyright-notice.md) states personal learning/research only and no commercial use for related copyright content, while retaining MIT for covered source and OFL for fonts.

The reviewed new source includes phone-assisted activation/full-save synchronization, bounded storage/profile/log/resource adapters, UI integration, BSP lifecycle/input improvements, host tests and tooling. Interfaces to excluded modules remain for authorized local development. This partial source snapshot is **not ready for a standalone public firmware release**.

## Checks and evidence

| Check | Result and meaning |
| --- | --- |
| Public repository conventions | PASS: 564 text files in the 593-file public tree passed paired-language, local-link, workflow-pin, secret-pattern and conflict-marker checks |
| Additional sensitive-content scan | No matches for token/private-key/JWT/credential/secret-QR patterns in the public worktree and the eight new historical blobs not already reachable from origin. Pattern scanning is not a guarantee that every possible secret was detected. |
| Internal handoff data | Detailed migration/deployment records retained in ignored local storage; public development notes contain portable requirements and commands |
| Excluded material boundary | PASS: staged and independently exported trees contain no recordings, supplied images/reference code, directly adapted modules, private traces or generated firmware |
| Local Build | PASS: `./tools/validate.sh --all --segmented` with retained local inputs, ESP-IDF 5.5.3 build and segmented archive verification |
| Local Host tests | PASS: the complete gate host tests and separate synthetic enrollment-helper tests |
| Device tests | NOT RUN during this review |
| Fresh public checkout | Repository/documentation checks PASS; `--static` explicitly fails on absent local inputs. Full STAYC build/host gate remains blocked by deliberately excluded materials |

The host helper's fixed temporary cJSON path was corrected to the same verified repository-local cache used by the main gate. OpenSSL headers/libraries are supplied per process on macOS. Validation reports missing private inputs explicitly; it never turns their tests into a silent pass.

## Local build identity

The reviewed local application is 5,949,024 bytes, with 2,374,048 bytes of default factory headroom. Application SHA-256: `c03c07147300d5cb3cbfc607e42f6c03738ae90bcdbacf78619a72be5a9474b5`; matching ELF SHA-256: `908cf8d2efdf1aaae2fd51f07d70f482d3d203b93c7a7a755b37e8a8d4a7f3b4`. The segmented archive stays in ignored local `build/firmware-segmented/<app-sha256>/`, without a download. Hashes identify corresponding artifacts; they do not prove licensing or device behavior.

The enrollment helper passed sanitizer checks, 16 server simulations, 11 host tests and an API-boundary audit using the installed IDF Python environment. It generated no real key, registered no account, connected no USB device and flashed nothing. Dependency checks cover pins, integrity and supplied licenses; an online vulnerability-database audit was not performed.

## Remaining release blockers

1. Obtain documented redistribution rights or replace every excluded asset, adapted module and fixture with material permitted for the intended release. Keep a source/license inventory.
2. Make a clean public clone pass the complete gate without undocumented private inputs. Current Actions firmware/host jobs still require those inputs and must not be represented as ready or green for this snapshot.
3. Physically verify startup, font coverage, timing, audio, BLE/PIN/reconnect, compatible phone/backend activation, full-save restore/conflicts and ownership changes, NVS/power-loss behavior, runtime heap/stack and any chosen candidate layout.
4. Review any future firmware or CI artifact for embedded media, secrets and data before distributing it. Do not publish a tag or invoke firmware-upload workflows for the current partial snapshot.

The default NVS/PHY/factory layout is unchanged. Candidate profile/log/resource layouts are not enabled or flashed by this review. Old account integration snapshots and the H2H browser preview are historical references, not proof of compatibility with the current phone JWT protocol. See the [user guide](../assets/pocket.md) and [development guide](../assets/stayc-development.md).
