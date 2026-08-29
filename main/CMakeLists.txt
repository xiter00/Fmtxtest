idf_component_register(SRCS "GhostC.c"
                            "managers/boot_system.c"
                            "managers/display_system.c"
                            "managers/input_system.c"
                            "managers/system.c"
                            "oled/fonts.c"
                            "oled/ssd1306_i2c.c"
                            "oled/i2c.c"
                            "oled/font_glcd_5x7.c"
                            "oled/font_tahoma_8pt.c"
                       INCLUDE_DIRS "include" "."
                       REQUIRES esp_wifi nvs_flash esp_netif esp_event esp_timer driver esp_driver_gpio vfs esp_http_server esp_driver_rmt esp_adc esp_http_client app_update mbedtls esp_driver_ledc esp_driver_i2s
                       PRIV_REQUIRES esp-tls )


target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-zmuldefs")
