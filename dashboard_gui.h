/******************************************************************************
 * dashboard_gui.h
 *
 * Single-file e-ink dashboard GUI. Owns its framebuffer, guards against
 * pre-init draws, calls Waveshare EPD and Paint directly. Your dashboard
 * manager calls just three functions: Init, Refresh, Sleep.
 *
 * Data model stays in dashboard_data.h (included below).
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

/* Optional: route status prints to your UART/SWO. If you already retarget
 * printf, leave as-is. To silence, #define DASH_LOG(...) before including,
 * or edit the fallback in the .c. */

/* Bring up panel + GUI and bind the internally-owned framebuffer.
 * Call ONCE after SPI/HAL init. Returns false on failure (see log). */
bool Dashboard_GuiInit(void);

/* Draw the dashboard from `data` and push to panel. No-op (logs a warning)
 * if not initialized or data is NULL. Sets data->render_done when done. */
void Dashboard_GuiRefresh(dashboard_data_t *data);

/* Panel deep sleep, call before Stop 2 / Standby. */
void Dashboard_GuiSleep(void);

/* True once Init has succeeded and it's safe to draw. */
bool Dashboard_GuiIsReady(void);

#endif /* DASHBOARD_GUI_H */
