idf_component_register(
    SRCS "main.c" "h2h/h2h_model.c" "h2h/h2h_motion.c" "h2h/h2h_audio.c" "h2h/h2h_ui.c" "h2h/h2h_theme.c" "h2h/h2h_ble.c" "h2h/h2h_cloud.c"
         "../assets/music/h2h/tracks.c" "../assets/images/h2h/sprites.c" "../assets/images/h2h/icons.c"
         "../assets/fonts/h2h/h2h_font_16.c"
    INCLUDE_DIRS "." "h2h"
    REQUIRES bsp esp_timer nvs_flash esp_audio_codec esp_system bt esp_wifi esp_netif esp_event esp_http_client esp-tls mbedtls json
)
include("${CMAKE_CURRENT_LIST_DIR}/../assets/music/h2h/music.cmake")
