/******************************************************************************
 * dashboard_gui.h
 *
 * Single-file e-ink dashboard GUI with PARTIAL REFRESH and per-field update
 * control, driven by the flag bits already in dashboard_data.h.
 *
 * Update model
 * ------------
 * You already mark fields ready with DASH_FLAG_*. This adds a "dirty" concept:
 * a field is redrawn only when its bit is in the dirty mask. Two ways to use it:
 *
 *   1. AUTOMATIC: call Dashboard_GuiRefresh(&data) and it repaints whatever
 *      changed since last draw (it snapshots values and diffs them).
 *
 *   2. MANUAL: call Dashboard_GuiMarkDirty(flags) to force specific fields to
 *      redraw, then Dashboard_GuiRefresh(&data). Useful when you know exactly
 *      what changed and don't want the diff.
 *
 * Full vs partial
 * ---------------
 * E-ink MUST do a full refresh periodically or it ghosts permanently. This
 * module forces a full refresh every FULL_REFRESH_EVERY partial updates (and
 * on the first draw). You can also force one with Dashboard_GuiForceFull().
 *
 * Panel-specific edits are all in dashboard_gui.c, marked  >>> EDIT <<< .
 ******************************************************************************/
#ifndef DASHBOARD_GUI_H
#define DASHBOARD_GUI_H

#include <stdint.h>
#include <stdbool.h>
#include "dashboard_data.h"

/* Panel logical (post-rotation) size. Set to YOUR panel. */
#define DASH_PANEL_W   250
#define DASH_PANEL_H   122

/* Force a full (de-ghosting) refresh every N partial refreshes. E-ink needs
 * this — never run partial forever. 0 = always full (disables partial). */
#define DASH_FULL_REFRESH_EVERY   8

/* Bring up panel + GUI. Call ONCE after SPI/HAL init. false on failure. */
bool Dashboard_GuiInit(void);

/* Draw the dashboard from `data`, using partial refresh for changed fields.
 * No-op (logs) if not initialized or data is NULL. Sets data->render_done. */
void Dashboard_GuiRefresh(dashboard_data_t *data);

/* MANUAL control: mark specific fields to be redrawn on the next Refresh.
 * `flags` is any OR of DASH_FLAG_*. Adds to the current dirty set. */
void Dashboard_GuiMarkDirty(uint32_t flags);

/* Force the NEXT Refresh to be a full (clean) refresh of the whole screen. */
void Dashboard_GuiForceFull(void);

/* Panel deep sleep, call before Stop 2 / Standby. */
void Dashboard_GuiSleep(void);

/* True once Init has succeeded. */
bool Dashboard_GuiIsReady(void);

#endif /* DASHBOARD_GUI_H */
