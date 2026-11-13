/* History (doc/web-interface.md 4, 4.1): a task of its
 * own, once a second, feeds the day in RAM (sh_day: the chart, the summary of the day) from the
 * published state (sh_state) and the clock (sh_clock). At local midnight, with a set clock, the
 * summary of the day is written to the "history" partition (sh_hist: 32 B records in a ring) and
 * handed to the zone check (sh_clock_day). Without the partition (e.g. a device updated over the
 * air with the old partition table) the history is off; everything else works. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sh_day.h"
#include "sh_hist.h"

esp_err_t sh_history_init(void);    /* after sh_clock_init(): finds the partition and its newest record */
esp_err_t sh_history_start(void);   /* the task */

bool sh_history_on(void);           /* the partition is there */

/* The day in RAM, for reading (the chart, the run time today); keep it short. */
const sh_day_t *sh_history_day_lock(void);
void sh_history_day_unlock(void);

/* The valid daily summaries, newest or oldest first, at most `max` (0: all); `fn` as in
 * sh_hist_walk(). The writing of a new day waits meanwhile. Returns how many. */
unsigned sh_history_walk(bool newest_first, unsigned max, sh_hist_fn fn, void *ctx);

/* Erases the daily summaries (the day in RAM stays). */
esp_err_t sh_history_clear(void);
