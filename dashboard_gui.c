/******************************************************************************
 * dashboard_gui.c
 *
 * Implementation of the portable e-ink dashboard GUI.
 *
 * How the drawing works
 * ---------------------
 * Everything is drawn into an in-RAM framebuffer via Waveshare's Paint_*
 * primitives (Paint_Clear, Paint_DrawString_EN, Paint_DrawRectangle, ...).
 * Nothing reaches the physical panel until we call panel->flush() at the end.
 * That means the screen updates in one clean shot, not field-by-field.
 *
 * Layout model
 * ------------
 * The screen is divided into simple stacked "rows", each ROW_H pixels tall.
 * A tiny cursor (g_y) walks down the screen; each draw_* helper renders one
 * row and advances the cursor. This makes the layout easy to reorder: move a
 * draw_* call up or down and everything reflows. No pixel math to maintain.
 *
 * Formatting
 * ----------
 * Values are fixed-point ints in the data model. We convert them to short
 * strings with a couple of tiny helpers (no printf/float in the hot path;
 * snprintf is used only for convenience and can be swapped for hand-rolled
 * integer formatting if you want to drop the libc dependency).
 ******************************************************************************/
#include "dashboard_gui.h"

/* Waveshare GUI library. Adjust include paths to your project layout. */
#include "GUI_Paint.h"
#include "fonts.h"

#include <stdio.h>    /* snprintf — see note above */
#include <string.h>

/* -------------------------------------------------------------------------
 * Layout constants. Tune these to your panel size. The defaults below suit a
 * ~250x122 (2.13") panel in landscape. All positions derive from these, so
 * changing ROW_H or MARGIN reflows the whole screen.
 * ---------------------------------------------------------------------- */
#define MARGIN        4          /* left/right padding, px                    */
#define ROW_H         16         /* height of one data row, px                */
#define LABEL_X       MARGIN     /* x where a row's label starts              */
#define VALUE_X       70         /* x where a row's value starts (label col.) */

/* Fonts come from Waveshare's fonts.h (Font8/12/16/20/24). Pick per role. */
#define FONT_TITLE    Font16
#define FONT_ROW      Font12
#define FONT_SMALL    Font8

/* Monochrome e-ink: two colors. Waveshare uses BLACK/WHITE macros. */
#define FG            BLACK       /* ink       */
#define BG            WHITE       /* paper     */

/* Shared scratch-buffer size for value formatting. Big enough for any
 * "12345.67 hPa"-style string plus unit and NUL, with margin. */
#define FMT_BUF       24

/* Module-local drawing cursor: the y of the next row to draw. Reset at the
 * top of each render. Kept file-static because Paint itself is a singleton. */
static uint16_t g_y;

/* =========================================================================
 * Tiny formatting helpers.
 * Each turns a fixed-point field into a display string in `out`.
 * ====================================================================== */

/* 2345 (0.01 C)  ->  "23.45"
 * Callers pass a buffer of >= FMT_BUF bytes (see FMT_BUF below). */
static void fmt_x100(char *out, int32_t v_x100)
{
    int32_t whole = v_x100 / 100;
    int32_t frac  = v_x100 % 100;
    if (frac < 0) frac = -frac;              /* keep fraction positive */
    snprintf(out, FMT_BUF, "%ld.%02ld", (long)whole, (long)frac);
}

/* 101325 Pa  ->  "1013.2 hPa"  (Pa/100 = hPa) */
static void fmt_pressure(char *out, uint32_t pa)
{
    uint32_t hpa_x10 = pa / 10;              /* Pa -> hPa*10 */
    snprintf(out, FMT_BUF, "%lu.%01lu hPa",
             (unsigned long)(hpa_x10 / 10), (unsigned long)(hpa_x10 % 10));
}

/* 3700 mV  ->  "3.70 V"  */
static void fmt_mv(char *out, uint16_t mv)
{
    snprintf(out, FMT_BUF, "%u.%02u V", mv / 1000, (mv % 1000) / 10);
}

/* =========================================================================
 * Row primitives.
 * draw_row prints "Label: value" at the current cursor and advances g_y.
 * When a field isn't ready we substitute a placeholder so the layout is
 * stable and you can see at a glance what's still pending.
 * ====================================================================== */
static void draw_label(const char *label)
{
    Paint_DrawString_EN(LABEL_X, g_y, label, &FONT_ROW, BG, FG);
}

/* Draw one "Label:  value" row. `ready` chooses value vs placeholder. */
static void draw_row(const char *label, const char *value, bool ready)
{
    draw_label(label);
    Paint_DrawString_EN(VALUE_X, g_y, ready ? value : "--", &FONT_ROW, BG, FG);
    g_y += ROW_H;
}

/* =========================================================================
 * Section renderers. Each reads its field(s) from `d`, formats, and draws.
 * They check the ready bit so a not-yet-populated sensor shows "--".
 * ====================================================================== */

static void draw_title_bar(const dashboard_data_t *d, uint16_t width)
{
    char buf[FMT_BUF];

    /* Title/clock across the top, with a divider line under it. */
    if (Dashboard_IsReady(d, DASH_FLAG_TIME)) {
        snprintf(buf, sizeof(buf), "%02u:%02u",
                 d->time.hour, d->time.minute);
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }
    /* Clock top-left, big. */
    Paint_DrawString_EN(MARGIN, 0, buf, &FONT_TITLE, BG, FG);

    /* Battery % top-right as a compact readout, if we have it. */
    if (Dashboard_IsReady(d, DASH_FLAG_BATTERY)) {
        char b[12];
        snprintf(b, sizeof(b), "%u%%", d->batt_soc_pct);
        /* right-align-ish: back off from the right edge by ~4 chars */
        uint16_t x = (width > 40) ? (width - 40) : 0;
        Paint_DrawString_EN(x, 0, b, &FONT_TITLE, BG, FG);
    }

    /* Divider line just below the title band. */
    g_y = FONT_TITLE.Height + 2;
    Paint_DrawLine(MARGIN, g_y, width - MARGIN, g_y,
                   FG, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    g_y += 4;   /* gap before the first data row */
}

static void draw_environment(const dashboard_data_t *d)
{
    char buf[FMT_BUF];

    fmt_x100(buf, d->temp_c_x100);
    /* append unit; buf has room */
    strncat(buf, " C", sizeof(buf) - strlen(buf) - 1);
    draw_row("Temp",  buf, Dashboard_IsReady(d, DASH_FLAG_TEMP));

    fmt_x100(buf, (int32_t)d->humidity_x100);
    strncat(buf, " %", sizeof(buf) - strlen(buf) - 1);
    draw_row("Humid", buf, Dashboard_IsReady(d, DASH_FLAG_HUMIDITY));

    fmt_pressure(buf, d->pressure_pa);
    draw_row("Press", buf, Dashboard_IsReady(d, DASH_FLAG_PRESSURE));

    snprintf(buf, sizeof(buf), "%lu lux", (unsigned long)d->light_lux);
    draw_row("Light", buf, Dashboard_IsReady(d, DASH_FLAG_LIGHT));
}

static void draw_battery(const dashboard_data_t *d)
{
    char buf[FMT_BUF];
    fmt_mv(buf, d->batt_mv);
    draw_row("Batt",  buf, Dashboard_IsReady(d, DASH_FLAG_BATTERY));
}

static void draw_wifi(const dashboard_data_t *d)
{
    bool ready = Dashboard_IsReady(d, DASH_FLAG_WIFI);

    /* SSID row. */
    draw_row("SSID", ready ? d->wifi_ssid : "--", ready);

    /* Password row: shown in clear only if wifi_show_pass is set (guest
     * kiosk use), otherwise masked. This is a deliberate policy switch so
     * you don't leak a credential onto a screen by accident. */
    if (ready) {
        if (d->wifi_show_pass) {
            draw_row("Pass", d->wifi_pass, true);
        } else {
            draw_row("Pass", "********", true);
        }
    } else {
        draw_row("Pass", "--", false);
    }

    /* Connection state as a short word. */
    const char *st = "?";
    switch (d->wifi_state) {
        case WIFI_STATE_DISCONNECTED: st = "offline";  break;
        case WIFI_STATE_CONNECTING:   st = "connecting";break;
        case WIFI_STATE_CONNECTED:    st = "online";    break;
        case WIFI_STATE_ERROR:        st = "error";     break;
    }
    draw_row("Link", st, ready);
}

/* =========================================================================
 * Public API
 * ====================================================================== */

void DashboardGUI_Init(dashboard_gui_t *gui,
                       const dashboard_panel_t *panel,
                       uint8_t *framebuffer,
                       uint16_t width, uint16_t height,
                       uint16_t rotate)
{
    gui->panel              = panel;
    gui->framebuffer        = framebuffer;
    gui->width              = width;
    gui->height             = height;
    gui->rotate             = rotate;
    gui->refresh_count      = 0;
    gui->full_refresh_every = 10;   /* de-ghost with a full refresh every 10 */

    /* Bind the framebuffer to Waveshare's Paint engine. WidthMemory/HeightMemory
     * are the PRE-rotation panel dims; Paint handles the rotation mapping. For a
     * landscape 250x122 panel you'd pass the panel's native W/H and ROTATE_90. */
    Paint_NewImage(framebuffer, width, height, rotate, BG);
    Paint_SetScale(2);              /* 2 = 1bpp monochrome (black/white)     */
    Paint_Clear(BG);
}

void DashboardGUI_Render(dashboard_gui_t *gui, dashboard_data_t *data)
{
    /* Decide full vs partial refresh. E-ink partial refreshes are fast but
     * leave ghosting; periodically we force a clean full refresh. */
    bool full = (gui->full_refresh_every == 0) ||
                (gui->refresh_count % gui->full_refresh_every == 0);

    /* 1. Bring the panel up in the chosen mode. */
    gui->panel->init(full);

    /* 2. Compose the frame in RAM. */
    Paint_SelectImage(gui->framebuffer);
    Paint_Clear(BG);
    g_y = 0;

    draw_title_bar(data, gui->width);   /* clock + batt% + divider  */
    draw_environment(data);             /* temp/humid/press/light   */
    draw_battery(data);                 /* battery voltage          */
    draw_wifi(data);                    /* ssid/pass/link           */

    /* 3. Push the finished frame to the physical panel. */
    gui->panel->flush(gui->framebuffer);

    /* 4. Bookkeeping + signal completion to the main loop. */
    gui->refresh_count++;
    Dashboard_MarkRenderDone(data);
}

void DashboardGUI_Sleep(dashboard_gui_t *gui)
{
    if (gui->panel && gui->panel->sleep) {
        gui->panel->sleep();
    }
}
