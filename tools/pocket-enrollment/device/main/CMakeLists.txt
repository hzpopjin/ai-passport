idf_component_register(
    SRCS "enrollment_core.c" "enrollment_main.c" "${POCKET_BOARD_REPO}/components/bsp/src/bsp_button.c"
    INCLUDE_DIRS "." "${POCKET_BOARD_REPO}/components/bsp/include"
    REQUIRES nvs_flash esp_driver_usb_serial_jtag esp_driver_spi esp_driver_i2c
             esp_adc esp_timer esp_hw_support esp_app_format json mbedtls espressif__button)
