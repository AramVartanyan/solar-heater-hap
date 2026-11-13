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
        "sh_pump_ctl.c"
        "sh_meas.c"
        "sh_alarm.c"
        "sh_ind.c"
        "sh_settings.c"
        "sh_settings_store.c"
        "sh_installer.c"
        "sh_installer_store.c"
        "sh_reset.c"
        "sh_web_api.c"
        "sh_web.c"
        "sh_menu.c"
        "sh_control.c"
        "sh_clock_rules.c"
        "sh_tz.c"
        "sh_clock.c"
        "sh_day.c"
        "sh_hist.c"
        "sh_history.c"
        "sh_sun.c"
        "sh_season.c"
        "sh_cool.c"
        "sh_learn_store.c"
        "sh_heater.c"
        "sh_e8.c"
        "sh_pulse.c"
        "sh_net.c"
        "sh_fw.c"
        "sh_link.c"
    INCLUDE_DIRS "."
    # the page of the web interface (sh_web), served as it is
    EMBED_FILES "web/index.html" "web/favicon.svg"
    REQUIRES
        # ESP-IDF
        driver esp_timer nvs_flash esp_wifi esp_event esp_netif esp_app_format esp_http_server esp_partition
        # project/common (git submodules)
        button outputwrite captive-wifi container_nvs general-ota esp_tm1637 ds18b20
        # managed components (idf_component.yml)
        led_strip mdns)
