idf_component_register(
    SRCS
        "app_main.c"
        "sh_state.c"
        "sh_led.c"
        "sh_relay.c"
        "sh_sensors.c"
        "sh_display.c"
        "sh_buttons.c"
        "sh_pump_logic.c"
        "sh_sensor_logic.c"
        "sh_settings_logic.c"
        "sh_settings.c"
        "sh_menu.c"
        "sh_control.c"
    INCLUDE_DIRS "."
    REQUIRES
        # ESP-IDF
        driver esp_timer nvs_flash esp_wifi esp_event esp_netif esp_app_format
        # project/common (git submodules)
        button outputwrite captive-wifi container_nvs general-ota esp_tm1637 ds18b20
        # managed components (idf_component.yml)
        led_strip)
