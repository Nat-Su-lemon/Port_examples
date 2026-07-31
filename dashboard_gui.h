/******************************************************************************
 * dashboard_gui.h
 *
 * PORTABLE e-ink GUI layer, built on top of Waveshare's GUI_Paint.
 *
 * What this module is
 * -------------------
 * It knows how to turn a dashboard_data_t (see dashboard_data.h) into pixels
 * in the Paint framebuffer. It does NOT know how to talk to the panel, and it
 * does NOT know where the data came from. That separation is what makes it
 * portable:
 *
 *      [ your sensors ] --writes--> dashboard_data_t --read by--> [ this GUI ]
 *                                                                      |
 *                                                          draws into Paint buffer
 *                                                                      |
 *                                            [ your panel driver ] <---flush
 *
 * Panel binding (the ONE thing you wire up per board)
 * ---------------------------------------------------
 * Waveshare panel drivers vary (EPD_2in13_V4_Init, EPD_2in9_Init, ...). Rather
 * than hard-code one, you hand the GUI three function pointers describing YOUR
 * panel through a dashboard_panel_t. Swap panels by swapping that struct.
 *
 * Typical usage
 * -------------
 *      static uint8_t framebuf[FRAMEBUF_BYTES];
 *      dashboard_gui_t gui;
 *      DashboardGUI_Init(&gui, &my_panel, framebuf, W, H, ROTATE_90);
 *      ...
 *      DashboardGUI_Render(&gui, &data);   // draws + flushes to panel
 ******************************************************************************/
#ifndef DASHBOARD_GUI_H
#define DASHBOARD_GUI_H

#include <stdint.h>
#include <stdbool.h>
#include "dashboard_data.h"

/* -------------------------------------------------------------------------
 * Panel binding.
 *
 * You provide these three callbacks for whatever Waveshare panel you use.
 * They wrap the panel-specific functions so the GUI can stay generic.
 *
 *   init(full)  : bring up the panel. `full` selects a full refresh (clean,
 *                 slow, no ghosting) vs a partial/fast refresh when supported.
 *   flush(buf)  : push the completed framebuffer to the panel. Maps to e.g.
 *                 EPD_2in13_V4_Display(buf).
 *   sleep()     : put the panel into deep sleep so it draws ~no current while
 *                 the MCU is in Stop 2. Maps to e.g. EPD_2in13_V4_Sleep().
 * ---------------------------------------------------------------------- */
typedef struct {
    void (*init)(bool full_refresh);
    void (*flush)(uint8_t *framebuffer);
    void (*sleep)(void);
} dashboard_panel_t;

/* -------------------------------------------------------------------------
 * GUI instance. Holds everything the renderer needs. You own the storage
 * (usually one static instance). Nothing here is global, so it's testable.
 * ---------------------------------------------------------------------- */
typedef struct {
    const dashboard_panel_t *panel;   /* your panel callbacks              */
    uint8_t *framebuffer;             /* Paint's pixel buffer (you supply) */
    uint16_t width;                   /* logical width  (post-rotation)    */
    uint16_t height;                  /* logical height (post-rotation)    */
    uint16_t rotate;                  /* ROTATE_0/90/180/270 from GUI_Paint */
    uint32_t refresh_count;           /* # of renders; used to decide when
                                         to force a full (de-ghost) refresh */
    uint8_t  full_refresh_every;      /* force full refresh every N renders
                                         (0 = always full). E-ink partial
                                         refreshes accumulate ghosting.     */
} dashboard_gui_t;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/*
 * Initialize the GUI instance.
 *   gui         : instance to init
 *   panel       : your panel callbacks (must outlive the GUI)
 *   framebuffer : buffer big enough for the panel (width_bytes * height)
 *   width,height: LOGICAL dimensions after rotation
 *   rotate      : ROTATE_0 / ROTATE_90 / ROTATE_180 / ROTATE_270
 * Also calls Paint_NewImage() for you.
 */
void DashboardGUI_Init(dashboard_gui_t *gui,
                       const dashboard_panel_t *panel,
                       uint8_t *framebuffer,
                       uint16_t width, uint16_t height,
                       uint16_t rotate);

/*
 * Render the whole dashboard from `data` and flush to the panel.
 * Reads data->ready_flags to decide what's shown vs placeholdered.
 * Sets data->render_done when the flush completes.
 *
 * This is the one call your main loop makes after sensors have updated.
 */
void DashboardGUI_Render(dashboard_gui_t *gui, dashboard_data_t *data);

/*
 * Put the panel to sleep (call before entering Stop 2 / Standby).
 * Thin wrapper over panel->sleep(); here so your low-power code doesn't reach
 * into the panel callbacks directly.
 */
void DashboardGUI_Sleep(dashboard_gui_t *gui);

#endif /* DASHBOARD_GUI_H */
