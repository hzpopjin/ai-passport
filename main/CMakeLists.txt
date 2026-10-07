idf_component_register(
    SRCS "main.c" "h2h/h2h_model.c" "h2h/h2h_motion.c" "h2h/h2h_audio.c" "h2h/h2h_setup.c" "h2h/h2h_ui.c" "h2h/h2h_theme.c" "h2h/h2h_ble.c" "h2h/h2h_fullsave.c" "h2h/h2h_phone.c" "h2h/h2h_relay_transport.c" "h2h/h2h_cloud.c" "h2h/h2h_ownership.c" "h2h/h2h_pocket.c" "h2h/h2h_resource.c" "h2h/h2h_resource_protocol.c" "h2h/h2h_resource_runtime.c"
         "h2h/stayc_scene.c" "h2h/stayc_visual.c" "h2h/h2h_header.c" "h2h/pocket_log.c" "h2h/pocket_log_ring.c" "h2h/pocket_profile.c" "h2h/pocket_profile_esp.c"
         "h2h/stayc_game.c" "h2h/stayc_game_store.c" "h2h/stayc_game_player.c" "h2h/stayc_game_motion.c"
         "h2h/stayc_game_life.c" "h2h/stayc_game_reaction.c" "h2h/stayc_game_decor.c"
         "h2h/stayc_game_ui.c" "h2h/stayc_game_visual.c"
         "../assets/music/stayc/tracks.c" "../assets/images/stayc/sprites.c" "../assets/images/stayc/actions.c"
         "../assets/fonts/h2h/h2h_font_16.c"
    INCLUDE_DIRS "." "h2h"
    REQUIRES bsp esp_timer nvs_flash esp_audio_codec esp_system bt esp_wifi esp_netif esp_event mbedtls json pocket_crypto esp_partition
    PRIV_REQUIRES esp_app_format
)
include("${CMAKE_CURRENT_LIST_DIR}/../assets/music/stayc/music.cmake")
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../assets/images/stayc/stayc_visuals.bin" BINARY)
