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

#define SH_HOSTNAME         CONFIG_SH_HOSTNAME

/* Control (Kconfig "Control"): temperatures in tenths of °C, times in seconds */
#define SH_MEASURE_PERIOD_S     CONFIG_SH_MEASURE_PERIOD_S
#define SH_DIFF_ON              (CONFIG_SH_DIFF_ON_K * 10)
#define SH_DIFF_OFF             (CONFIG_SH_DIFF_OFF_K * 10)
#define SH_MIN_RUN_S            (CONFIG_SH_MIN_RUN_MIN * 60)
#define SH_MIN_OFF_S            (CONFIG_SH_MIN_OFF_MIN * 60)
#define SH_TANK_MAX             (CONFIG_SH_TANK_MAX_C * 10)
#define SH_COL_MIN              (CONFIG_SH_COL_MIN_C * 10)
#define SH_COL_PROTECT          (CONFIG_SH_COL_PROTECT_C * 10)

_Static_assert(CONFIG_SH_DIFF_OFF_K < CONFIG_SH_DIFF_ON_K,
               "Control: the switch-off difference must be lower than the switch-on difference");
