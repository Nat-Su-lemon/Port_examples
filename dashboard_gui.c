/******************************************************************************
 * dashboard_gui.c
 *
 * Everything in one file: owned framebuffer, init guard, layout, and direct
 * Waveshare EPD and Paint calls.
 *
 * EDIT FOR YOUR PANEL (only these):
 *   >>> EDIT 1 <<<  the panel driver #include
 *   >>> EDIT 2 <<<  the 3 EPD_* calls (init / display / sleep)
 *   plus DASH_PANEL_W/H and ROTATE in the header / init.
 ******************************************************************************/
#include "dashboard_gui.h"

#include "GUI_Paint.h"
#include "fonts.h"

/* >>> EDIT 1 <<< : your panel driver header */
#include "EPD_2in13_V4.h"

#include <stdio.h>
#include <string.h>

/* ---- debug print: redirect to UART/SWO, or silence by defining empty ---- */
#ifndef DASH_LOG
#define DASH_LOG(...)  printf("[GUI] " __VA_ARGS__)
#endif

/* ---- layout (tune to your panel) ---- */
#define MARGIN     4
#define ROW_H      16
#define VALUE_X    70
#define FONT_ROW   Font12
#define FONT_TITLE Font16
#define FG         BLACK
#define BG         WHITE
#define FMT_BUF    24

/* ---- owned framebuffer: 1bpp => ceil(W/8)*H bytes ---- */
#define FB_BYTES  (((DASH_PANEL_W + 7) / 8) * DASH_PANEL_H)
static uint8_t  s_fb[FB_BYTES];
static bool     s_ready = false;
static uint16_t s_y;    /* draw cursor */

/* ---- tiny fixed-point formatters ---- */
static void fmt_x100(char *o, int32_t v){
    int32_t w=v/100, f=v%100; if(f<0)f=-f;
    snprintf(o,FMT_BUF,"%ld.%02ld",(long)w,(long)f);
}
static void fmt_mv(char *o, uint16_t mv){
    snprintf(o,FMT_BUF,"%u.%02u V",mv/1000,(mv%1000)/10);
}

/* ---- one row: "Label value" or "--" if not ready ---- */
static void row(const char *label, const char *value, bool ready){
    Paint_DrawString_EN(MARGIN,  s_y, label,                 &FONT_ROW, BG, FG);
    Paint_DrawString_EN(VALUE_X, s_y, ready ? value : "--",  &FONT_ROW, BG, FG);
    s_y += ROW_H;
}

/* ========================================================================= */
bool Dashboard_GuiInit(void)
{
    DASH_LOG("init: fb=%p size=%u\n", (void*)s_fb, (unsigned)sizeof(s_fb));

    /* >>> EDIT 2a <<< : panel hardware bring-up BEFORE any Paint call.
     * This ordering is what prevents the init-time hardfault. */
    DEV_Module_Init();
    EPD_2in13_V4_Init();

    /* Bind our owned buffer to Paint. Pass logical size + matching rotate. */
    Paint_NewImage(s_fb, DASH_PANEL_W, DASH_PANEL_H, ROTATE_90, BG);
    Paint_SetScale(2);
    Paint_SelectImage(s_fb);
    Paint_Clear(BG);

    s_ready = true;
    DASH_LOG("init: OK\n");
    return true;
}

void Dashboard_GuiRefresh(dashboard_data_t *data)
{
    if (!s_ready) { DASH_LOG("refresh SKIPPED: not init\n"); return; }
    if (!data)    { DASH_LOG("refresh SKIPPED: null data\n"); return; }

    char buf[FMT_BUF];

    Paint_SelectImage(s_fb);
    Paint_Clear(BG);
    s_y = 0;

    /* clock */
    if (Dashboard_IsReady(data, DASH_FLAG_TIME))
        snprintf(buf,sizeof(buf),"%02u:%02u",data->time.hour,data->time.minute);
    else
        snprintf(buf,sizeof(buf),"--:--");
    Paint_DrawString_EN(MARGIN, 0, buf, &FONT_TITLE, BG, FG);
    s_y = FONT_TITLE.Height + 4;

    /* rows */
    fmt_x100(buf, data->temp_c_x100);
    strncat(buf," C",sizeof(buf)-strlen(buf)-1);
    row("Temp", buf, Dashboard_IsReady(data, DASH_FLAG_TEMP));

    fmt_x100(buf, (int32_t)data->humidity_x100);
    strncat(buf," %",sizeof(buf)-strlen(buf)-1);
    row("Humid", buf, Dashboard_IsReady(data, DASH_FLAG_HUMIDITY));

    snprintf(buf,sizeof(buf),"%lu lux",(unsigned long)data->light_lux);
    row("Light", buf, Dashboard_IsReady(data, DASH_FLAG_LIGHT));

    fmt_mv(buf, data->batt_mv);
    row("Batt", buf, Dashboard_IsReady(data, DASH_FLAG_BATTERY));

    snprintf(buf,sizeof(buf),"%u%%",data->batt_soc_pct);
    row("SoC", buf, Dashboard_IsReady(data, DASH_FLAG_BATTERY));

    bool w = Dashboard_IsReady(data, DASH_FLAG_WIFI);
    row("SSID", w ? data->wifi_ssid : "--", w);
    if (w && data->wifi_show_pass) row("Pass", data->wifi_pass, true);
    else                           row("Pass", w ? "********" : "--", w);

    /* >>> EDIT 2b <<< : push to panel */
    EPD_2in13_V4_Display(s_fb);

    Dashboard_MarkRenderDone(data);
    DASH_LOG("refresh: done (flags=0x%08lX)\n",(unsigned long)data->ready_flags);
}

void Dashboard_GuiSleep(void)
{
    if (!s_ready) return;
    /* >>> EDIT 2c <<< : panel deep sleep */
    EPD_2in13_V4_Sleep();
    DASH_LOG("panel sleep\n");
}

bool Dashboard_GuiIsReady(void) { return s_ready; }
