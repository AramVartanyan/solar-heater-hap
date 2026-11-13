/* Created by Aram Vartanyan (c) 2026 */

/* Solar heater controller — pins and fixed parameters (Kconfig "Device Settings"). */
#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"

#define SH_PIN_T_COL        ((gpio_num_t)CONFIG_SH_PIN_T_COL)
#define SH_PIN_T_TANK       ((gpio_num_t)CONFIG_SH_PIN_T_TANK)
#define SH_PIN_RELAY        ((gpio_num_t)CONFIG_SH_PIN_RELAY)
#define SH_PIN_TM1637_CLK   ((gpio_num_t)CONFIG_SH_PIN_TM1637_CLK)
#define SH_PIN_TM1637_DIO   ((gpio_num_t)CONFIG_SH_PIN_TM1637_DIO)
#define SH_PIN_BTN_MENU     ((gpio_num_t)CONFIG_SH_PIN_BTN_MENU)
#define SH_PIN_BTN_MINUS    ((gpio_num_t)CONFIG_SH_PIN_BTN_MINUS)
#define SH_PIN_BTN_PLUS     ((gpio_num_t)CONFIG_SH_PIN_BTN_PLUS)
#define SH_PIN_LED          ((gpio_num_t)CONFIG_SH_PIN_LED)
