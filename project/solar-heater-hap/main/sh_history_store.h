/* History kept by the firmware (doc/web-interface.md 4, 4.1; doc/tasks/step9-clock-history.md 3.2,
 * 3.3): the day chart and the day in RAM, stepped by the control task every second; the daily
 * summaries in the "history" partition (data, 64 KB, a ring of 32-byte records), one written at
 * local midnight while the clock is set. Without the partition (a device updated by OTA keeps its
 * old partition table) the summaries are off and everything else works. Also keeps the time zone
 * check by the collector peak. The logic: sh_history, sh_clock. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sh_history.h"

void sh_history_init(void);
bool sh_history_available(void);            /* the partition is there and readable */

/* One step (control task). Returns whether the clock is reliable. */
bool sh_history_step(const sh_hist_in_t *in);

/* New time zone, or the learned values are reset. */
void sh_history_peak_reset(bool zone_confirmed);

/* For the web interface: the pump today and the time zone check ... */
void sh_history_today(uint32_t run_s[SH_RUN_KINDS], sh_peak_t *peak);
/* ... the day chart (`pts`: SH_HIST_SLOTS points) and the last summaries (`days`:
 * SH_HIST_WEB_DAYS), the newest first ... */
void sh_history_chart(sh_hist_point_t *pts, int *n, bool *day, sh_hist_day_t *days, int *n_days);
/* ... and the lines of the CSV in parts (after sh_hist_csv_header()): whole lines from summary
 * `*pos` on, the oldest first; `*pos` moves on. Returns the length; 0 at the end.
 * `size` >= SH_HIST_CSV_LINE_MAX. */
size_t sh_history_csv(unsigned *pos, char *buf, size_t size);

esp_err_t sh_history_erase(void);           /* all summaries */
