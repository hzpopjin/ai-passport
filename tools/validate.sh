#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
firmware_format="${2:---merged}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware] [--merged|--segmented]" >&2
}

check_local_materials() {
    local required
    local missing=0
    for required in main/h2h/stayc_game.c main/h2h/stayc_game.h \
        main/h2h/stayc_game_motion.c main/h2h/stayc_game_motion.h \
        main/h2h/stayc_game_life.c main/h2h/stayc_game_life.h \
        main/h2h/stayc_game_decor.c main/h2h/stayc_game_decor.h \
        main/h2h/stayc_game_reaction.c main/h2h/stayc_game_reaction.h \
        assets/images/stayc/actions.c assets/images/stayc/sprites.c \
        assets/images/stayc/stayc_visuals.bin; do
        if [[ ! -f "${required}" ]]; then
            echo "Missing local-only input: ${required}" >&2
            missing=1
        fi
    done
    if [[ "${missing}" == 1 ]]; then
        echo "STAYC inputs are excluded pending redistribution permission. See docs/assets/stayc-development.md." >&2
        return 1
    fi
}

run_static_checks() {
    local actionlint_bin
    local test_dir
    local gc_flag="-Wl,--gc-sections"
    if [[ "$(uname -s)" == "Darwin" ]]; then gc_flag="-Wl,-dead_strip"; fi

    python3 tools/check_repo.py
    check_local_materials

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${gc_flag}" \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_segmented_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h \
        tests/test_h2h_model.c main/h2h/h2h_model.c main/h2h/h2h_motion.c -o "${test_dir}/test_h2h_model"
    "${test_dir}/test_h2h_model"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h \
        tests/test_h2h_theme.c main/h2h/h2h_theme.c main/h2h/h2h_model.c -o "${test_dir}/test_h2h_theme"
    "${test_dir}/test_h2h_theme"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/h2h_audio_stubs -Itests/audio_stubs -Icomponents/bsp/include -Imain/h2h \
        tests/test_h2h_audio.c -o "${test_dir}/test_h2h_audio"
    "${test_dir}/test_h2h_audio"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h \
        tests/test_h2h_setup.c main/h2h/h2h_setup.c -o "${test_dir}/test_h2h_setup"
    "${test_dir}/test_h2h_setup"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h \
        tests/test_h2h_ownership.c main/h2h/h2h_ownership.c -o "${test_dir}/test_h2h_ownership"
    "${test_dir}/test_h2h_ownership"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h \
        tests/test_h2h_pocket.c main/h2h/h2h_pocket.c main/h2h/h2h_ownership.c main/h2h/h2h_model.c -o "${test_dir}/test_h2h_pocket"
    "${test_dir}/test_h2h_pocket"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h \
        tests/test_h2h_activation.c main/h2h/h2h_model.c -o "${test_dir}/test_h2h_activation"
    "${test_dir}/test_h2h_activation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h \
        tests/test_stayc_scene.c main/h2h/stayc_scene.c main/h2h/h2h_model.c \
        assets/images/stayc/actions.c assets/images/stayc/sprites.c -o "${test_dir}/test_stayc_scene"
    "${test_dir}/test_stayc_scene"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_stayc_reference.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_h2h_resource_tools.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_h2h_oracle.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_stayc_game_reference.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_stayc_player_reference.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h \
        tests/test_stayc_store.c main/h2h/stayc_game_store.c main/h2h/stayc_game.c main/h2h/h2h_model.c \
        -o "${test_dir}/test_stayc_store"
    "${test_dir}/test_stayc_store"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h -include tests/fullsave_alloc_probe.h \
        -c main/h2h/h2h_fullsave.c -o "${test_dir}/fullsave_alloc_probe.o"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h \
        tests/test_h2h_fullsave.c "${test_dir}/fullsave_alloc_probe.o" main/h2h/h2h_pocket.c main/h2h/h2h_ownership.c main/h2h/h2h_model.c main/h2h/stayc_game_store.c main/h2h/stayc_game.c \
        -o "${test_dir}/test_h2h_fullsave"
    "${test_dir}/test_h2h_fullsave"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h \
        -Icomponents/bsp/include -Itests/audio_stubs tests/test_stayc_feedback.c main/h2h/stayc_game.c -o "${test_dir}/test_stayc_feedback"
    "${test_dir}/test_stayc_feedback"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h \
        tests/test_pocket_log_ring.c -o "${test_dir}/test_pocket_log_ring"
    "${test_dir}/test_pocket_log_ring"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h -Itests/log_stubs \
        tests/test_pocket_log_service.c main/h2h/pocket_log_ring.c -o "${test_dir}/test_pocket_log_service"
    "${test_dir}/test_pocket_log_service"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h tests/test_pocket_profile.c main/h2h/pocket_profile.c -lcrypto -o "${test_dir}/test_pocket_profile"
    "${test_dir}/test_pocket_profile"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h -Itests/log_stubs -Itests/profile_stubs tests/test_pocket_profile_esp.c -lcrypto -o "${test_dir}/test_pocket_profile_esp"
    "${test_dir}/test_pocket_profile_esp"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_pocket_log_decoder.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/build_h2h_cloud_host.py \
        --output "${test_dir}/test_h2h_cloud_host" \
        --dependencies "${H2H_CJSON_DIR:-${repo_root}/build/host-dependencies/cjson-v1.7.19}"
    for scenario in --relay-live-snapshot --wifi-lifecycle --activated-main --guarded-preferences --debug-cloud-barrier --gameplay-shop-fail --gameplay-postwrite-fail --gameplay-protected --gameplay-audio-boundaries --gameplay-debug-local --gameplay-debug-bound --gameplay-debug-cancel --gameplay-debug-stop-fail; do
        "${test_dir}/test_h2h_cloud_host" "${scenario}"
    done
    "${test_dir}/test_h2h_cloud_host"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h -Icomponents/pocket_crypto -I"${H2H_CJSON_DIR:-${repo_root}/build/host-dependencies/cjson-v1.7.19}" \
        tests/test_h2h_relay.c main/h2h/h2h_relay_transport.c main/h2h/h2h_pocket.c main/h2h/h2h_ownership.c main/h2h/h2h_model.c main/h2h/h2h_fullsave.c main/h2h/stayc_game_store.c main/h2h/stayc_game.c \
        main/h2h/h2h_resource.c main/h2h/h2h_resource_protocol.c components/pocket_crypto/monocypher.c components/pocket_crypto/monocypher-ed25519.c \
        "${H2H_CJSON_DIR:-${repo_root}/build/host-dependencies/cjson-v1.7.19}/cJSON.c" -lcrypto -lm -o "${test_dir}/test_h2h_relay"
    "${test_dir}/test_h2h_relay" tests/pocket_relay_conformance.json
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/relay_ble_stubs -Imain/h2h \
        tests/test_h2h_relay_ble.c main/h2h/h2h_relay_transport.c -o "${test_dir}/test_h2h_relay_ble"
    "${test_dir}/test_h2h_relay_ble"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_h2h_music_source.py
    PYTHONDONTWRITEBYTECODE=1 python3 tools/h2h/build_visual_resource.py "${test_dir}/visual"
    local crypto_flags=(-lcrypto)
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists openssl; then
        read -r -a crypto_flags <<< "$(pkg-config --cflags --libs openssl)"
    fi
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/h2h -Icomponents/pocket_crypto \
        tests/test_h2h_resource.c main/h2h/h2h_resource.c \
        components/pocket_crypto/monocypher.c components/pocket_crypto/monocypher-ed25519.c \
        "${crypto_flags[@]}" -o "${test_dir}/test_h2h_resource"
    local visual_file
    visual_file="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["manifest"]["sha256"]+".bin")' "${test_dir}/visual/unsigned-entry.json")"
    "${test_dir}/test_h2h_resource" "${test_dir}/visual/${visual_file}"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DCONFIG_ESP_MAIN_TASK_STACK_SIZE=12288 \
        -Itests/pocket_stubs -Imain/h2h -Icomponents/pocket_crypto -I"${H2H_CJSON_DIR:-${repo_root}/build/host-dependencies/cjson-v1.7.19}" \
        tests/test_h2h_relay_resources.c main/h2h/h2h_relay_transport.c main/h2h/h2h_pocket.c main/h2h/h2h_ownership.c main/h2h/h2h_model.c main/h2h/h2h_fullsave.c main/h2h/stayc_game_store.c main/h2h/stayc_game.c \
        main/h2h/h2h_resource.c main/h2h/h2h_resource_protocol.c main/h2h/h2h_resource_tls.c main/h2h/h2h_resource_runtime.c \
        components/pocket_crypto/monocypher.c components/pocket_crypto/monocypher-ed25519.c \
        "${H2H_CJSON_DIR:-${repo_root}/build/host-dependencies/cjson-v1.7.19}/cJSON.c" \
        "${crypto_flags[@]}" -lm -o "${test_dir}/test_h2h_relay_resources"
    "${test_dir}/test_h2h_relay_resources" "${test_dir}/visual/${visual_file}"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_h2h_assets.py
    PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_h2h_*web.py'
    PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_h2h_nfc_ndef.py'
    PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_h2h_theme_catalog.py'
    rm -rf "${test_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/build_phone_host.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_phone_wire.py
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    check_local_materials

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    if [[ "${firmware_format}" == "--segmented" ]]; then
        PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_segmented_firmware.py create \
            "${validation_build_dir}" --archive-root "${repo_root}/build/firmware-segmented"
        echo "Firmware build: PASS (segmented; existing minimal layout; no padded image)"
        return
    fi
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    python3 tools/h2h/check_capacity.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
if [[ "$#" -gt 2 || ( "${firmware_format}" != "--merged" && "${firmware_format}" != "--segmented" ) ]]; then
    usage
    exit 2
fi
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
