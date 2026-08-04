/******************************************************************************
 * dashboard_gui.c
 *
 * Single-file dashboard GUI with partial refresh + per-field update control.
 *
 * How partial refresh works here
 * ------------------------------
 * Each field occupies a fixed rectangle on screen (computed once in the layout
 * table). On a refresh we determine the DIRTY set (which fields changed), then:
 *   - FULL path  : redraw everything, push with the full-refresh call.
 *   - PARTIAL    : clear + redraw only the dirty fields' rectangles, push with
 *                  the partial-refresh call.
 * A counter forces a full refresh every DASH_FULL_REFRESH_EVERY partials so the
 * panel never ghosts permanently.
 *
 * EDIT FOR YOUR PANEL — search  >>> EDIT <<< :
 *   1. panel driver #include
 *   2a. full-refresh init + display calls
 *   2b. partial-refresh base-image + display-part calls
 *   2c. sleep call
 ******************************************************************************/
#include "dashboard_gui.h"

#include "GUI_Paint.h"
#include "fonts.h"

/* >>> EDIT 1 <<< : your panel driver header */
#include "EPD_2in13_V4.h"

#include <stdio.h>
#include <string.h>

#ifndef DASH_LOG
#define DASH_LOG(...)  printf("[GUI] " __VA_ARGS__)
#endif

/* ---- layout ---- */
#define MARGIN     4
#define ROW_H      16
#define VALUE_X    70
#define TITLE_H    20        /* height reserved for the clock row */
#define FONT_ROW   Font12
#define FONT_TITLE Font16
#define FG         BLACK
#define BG         WHITE
#define FMT_BUF    24
#define TEXT_BUF   (WIFI_PASS_MAXLEN + 1)  /* longest field: the password */

/* ---- owned framebuffer ---- */
#define FB_BYTES  (((DASH_PANEL_W + 7) / 8) * DASH_PANEL_H)
static uint8_t s_fb[FB_BYTES];
static bool    s_ready = false;

/* ---- refresh bookkeeping ---- */
static uint32_t s_dirty        = 0;   /* fields to redraw next Refresh   */
static uint32_t s_partial_count= 0;   /* partials since last full        */
static bool     s_force_full   = true;/* first draw is always full       */

/* -------------------------------------------------------------------------
 * Layout table: one entry per drawable field. `flag` ties the row to a
 * DASH_FLAG_* bit so dirty-tracking and drawing share the same identity.
 * y is computed from row order. Each field's rectangle is (0,y)-(W,y+ROW_H),
 * i.e. a full-width horizontal band, which is what the partial-refresh window
 * needs. Reorder rows by reordering this table.
 * ---------------------------------------------------------------------- */
typedef enum {
    F_TIME, F_TEMP, F_HUMID, F_LIGHT, F_BATT, F_SOC, F_SSID, F_PASS,
    F_COUNT
} field_idx_t;

typedef struct {
    uint32_t    flag;     /* which DASH_FLAG_* drives this row */
    const char *label;
    uint16_t    y;        /* filled in at init */
} field_row_t;

static field_row_t s_rows[F_COUNT] = {
    /* flag,               label,   y (set in init) */
    { DASH_FLAG_TIME,     "",       0 },   /* title/clock, special-cased */
    { DASH_FLAG_TEMP,     "Temp",   0 },
    { DASH_FLAG_HUMIDITY, "Humid",  0 },
    { DASH_FLAG_LIGHT,    "Light",  0 },
    { DASH_FLAG_BATTERY,  "Batt",   0 },
    { DASH_FLAG_BATTERY,  "SoC",    0 },   /* SoC shares the BATTERY flag */
    { DASH_FLAG_WIFI,     "SSID",   0 },
    { DASH_FLAG_WIFI,     "Pass",   0 },   /* Pass shares the WIFI flag  */
};

/* ---- snapshot of last-drawn values, for automatic change detection ---- */
static struct {
    int32_t  temp; uint16_t humid; uint32_t light;
    uint16_t batt_mv; uint16_t soc;
    uint8_t  hour, minute;
    char     ssid[WIFI_SSID_MAXLEN+1];
    char     pass[WIFI_PASS_MAXLEN+1];
    bool     show_pass;
} s_last;

/* ---- formatters ---- */
static void fmt_x100(char *o,int32_t v) /* o must be TEXT_BUF */{int32_t w=v/100,f=v%100;if(f<0)f=-f;snprintf(o,TEXT_BUF,"%ld.%02ld",(long)w,(long)f);}
static void fmt_mv(char *o,uint16_t mv) /* o must be TEXT_BUF */{snprintf(o,TEXT_BUF,"%u.%02u V",mv/1000,(mv%1000)/10);}

/* ---- build the string a given field should show right now ---- */
static void field_text(dashboard_data_t *d, field_idx_t idx, char *out) /* out must be TEXT_BUF */
{
    switch (idx) {
    case F_TIME:
        if (Dashboard_IsReady(d,DASH_FLAG_TIME))
            snprintf(out,TEXT_BUF,"%02u:%02u",d->time.hour,d->time.minute);
        else snprintf(out,TEXT_BUF,"--:--");
        break;
    case F_TEMP:
        if (Dashboard_IsReady(d,DASH_FLAG_TEMP)){fmt_x100(out,d->temp_c_x100);strncat(out," C",TEXT_BUF-strlen(out)-1);}
        else snprintf(out,TEXT_BUF,"--");
        break;
    case F_HUMID:
        if (Dashboard_IsReady(d,DASH_FLAG_HUMIDITY)){fmt_x100(out,(int32_t)d->humidity_x100);strncat(out," %",TEXT_BUF-strlen(out)-1);}
        else snprintf(out,TEXT_BUF,"--");
        break;
    case F_LIGHT:
        if (Dashboard_IsReady(d,DASH_FLAG_LIGHT)) snprintf(out,TEXT_BUF,"%lu lux",(unsigned long)d->light_lux);
        else snprintf(out,TEXT_BUF,"--");
        break;
    case F_BATT:
        if (Dashboard_IsReady(d,DASH_FLAG_BATTERY)) fmt_mv(out,d->batt_mv);
        else snprintf(out,TEXT_BUF,"--");
        break;
    case F_SOC:
        if (Dashboard_IsReady(d,DASH_FLAG_BATTERY)) snprintf(out,TEXT_BUF,"%u%%",d->batt_soc_pct);
        else snprintf(out,TEXT_BUF,"--");
        break;
    case F_SSID:
        snprintf(out,TEXT_BUF,"%s",Dashboard_IsReady(d,DASH_FLAG_WIFI)?d->wifi_ssid:"--");
        break;
    case F_PASS:
        if (Dashboard_IsReady(d,DASH_FLAG_WIFI))
            snprintf(out,TEXT_BUF,"%s",d->wifi_show_pass?d->wifi_pass:"********");
        else snprintf(out,TEXT_BUF,"--");
        break;
    default: out[0]='\0';
    }
}

/* ---- draw one field's band into the framebuffer ---- */
static void draw_field(dashboard_data_t *d, field_idx_t idx)
{
    char buf[TEXT_BUF];
    field_text(d, idx, buf);
    uint16_t y = s_rows[idx].y;

    /* clear just this band so old pixels don't linger under new text */
    Paint_ClearWindows(0, y, DASH_PANEL_W, y + ROW_H, BG);

    if (idx == F_TIME) {
        Paint_DrawString_EN(MARGIN, y, buf, &FONT_TITLE, BG, FG);
    } else {
        Paint_DrawString_EN(MARGIN,  y, s_rows[idx].label, &FONT_ROW, BG, FG);
        Paint_DrawString_EN(VALUE_X, y, buf,               &FONT_ROW, BG, FG);
    }
}

/* ---- automatic change detection: compare live data to last snapshot,
 *      OR the changed fields into s_dirty, then update the snapshot. ---- */
static void diff_into_dirty(dashboard_data_t *d)
{
    if (d->temp_c_x100 != s_last.temp)        s_dirty |= DASH_FLAG_TEMP;
    if (d->humidity_x100 != s_last.humid)     s_dirty |= DASH_FLAG_HUMIDITY;
    if (d->light_lux != s_last.light)         s_dirty |= DASH_FLAG_LIGHT;
    if (d->batt_mv != s_last.batt_mv ||
        d->batt_soc_pct != s_last.soc)        s_dirty |= DASH_FLAG_BATTERY;
    if (d->time.hour != s_last.hour ||
        d->time.minute != s_last.minute)      s_dirty |= DASH_FLAG_TIME;
    if (strncmp(d->wifi_ssid,s_last.ssid,WIFI_SSID_MAXLEN)!=0 ||
        strncmp(d->wifi_pass,s_last.pass,WIFI_PASS_MAXLEN)!=0 ||
        d->wifi_show_pass != s_last.show_pass) s_dirty |= DASH_FLAG_WIFI;

    s_last.temp=d->temp_c_x100; s_last.humid=d->humidity_x100; s_last.light=d->light_lux;
    s_last.batt_mv=d->batt_mv; s_last.soc=d->batt_soc_pct;
    s_last.hour=d->time.hour; s_last.minute=d->time.minute;
    strncpy(s_last.ssid,d->wifi_ssid,WIFI_SSID_MAXLEN); s_last.ssid[WIFI_SSID_MAXLEN]=0;
    strncpy(s_last.pass,d->wifi_pass,WIFI_PASS_MAXLEN); s_last.pass[WIFI_PASS_MAXLEN]=0;
    s_last.show_pass=d->wifi_show_pass;
}

/* ---- draw all fields (used by the full path) ---- */
static void draw_all(dashboard_data_t *d)
{
    Paint_SelectImage(s_fb);
    Paint_Clear(BG);
    for (int i=0;i<F_COUNT;i++) draw_field(d,(field_idx_t)i);
}

/* ---- draw only dirty fields (used by the partial path) ---- */
static void draw_dirty(dashboard_data_t *d)
{
    Paint_SelectImage(s_fb);
    for (int i=0;i<F_COUNT;i++)
        if (s_dirty & s_rows[i].flag) draw_field(d,(field_idx_t)i);
}

/* ========================================================================= */
bool Dashboard_GuiInit(void)
{
    DASH_LOG("init: fb=%p size=%u\n",(void*)s_fb,(unsigned)sizeof(s_fb));

    /* compute each row's y. Title first, then data rows below it. */
    s_rows[F_TIME].y = 0;
    uint16_t y = TITLE_H + 2;
    for (int i=1;i<F_COUNT;i++){ s_rows[i].y = y; y += ROW_H; }

    /* >>> EDIT 2a <<< : panel bring-up (full-refresh init) BEFORE Paint. */
    DEV_Module_Init();
    EPD_2in13_V4_Init();

    Paint_NewImage(s_fb, DASH_PANEL_W, DASH_PANEL_H, ROTATE_90, BG);
    Paint_SetScale(2);
    Paint_SelectImage(s_fb);
    Paint_Clear(BG);

    /* reset bookkeeping; first refresh will be a full one */
    s_dirty = 0; s_partial_count = 0; s_force_full = true;
    memset(&s_last,0,sizeof(s_last));

    s_ready = true;
    DASH_LOG("init: OK\n");
    return true;
}

void Dashboard_GuiMarkDirty(uint32_t flags){ s_dirty |= flags; }
void Dashboard_GuiForceFull(void){ s_force_full = true; }

void Dashboard_GuiRefresh(dashboard_data_t *data)
{
    if (!s_ready){ DASH_LOG("refresh SKIPPED: not init\n"); return; }
    if (!data)   { DASH_LOG("refresh SKIPPED: null data\n"); return; }

    /* Merge automatic change-detection into any manual dirty bits. */
    diff_into_dirty(data);

    /* Decide full vs partial. Full if: forced, first draw, partial disabled,
     * or we've hit the periodic de-ghost limit. */
    bool do_full = s_force_full ||
                   (DASH_FULL_REFRESH_EVERY == 0) ||
                   (s_partial_count >= DASH_FULL_REFRESH_EVERY);

    if (do_full) {
        draw_all(data);
        /* >>> EDIT 2a (cont) <<< : full-refresh init + display.
         * If your panel needs re-init to leave partial mode, do it here. */
        EPD_2in13_V4_Init();                 /* full-refresh mode */
        EPD_2in13_V4_Display(s_fb);          /* full display      */
        s_partial_count = 0;
        s_force_full = false;
        DASH_LOG("refresh: FULL (flags=0x%08lX)\n",(unsigned long)data->ready_flags);
    } else if (s_dirty) {
        draw_dirty(data);
        /* >>> EDIT 2b <<< : partial-refresh sequence.
         * Waveshare V2/V3/V4: set the base image once, then DisplayPart.
         * Exact names vary; common forms:
         *   EPD_2in13_V4_Init_Fast();                 // enter partial/fast mode
         *   EPD_2in13_V4_Display_Base(s_fb);          // or DisplayPartBaseImage
         *   EPD_2in13_V4_Display_Partial(s_fb);       // or DisplayPart
         * Use whichever your driver provides. */
        EPD_2in13_V4_Display_Partial(s_fb);  /* partial display */
        s_partial_count++;
        DASH_LOG("refresh: PARTIAL dirty=0x%08lX (#%lu)\n",
                 (unsigned long)s_dirty,(unsigned long)s_partial_count);
    } else {
        DASH_LOG("refresh: nothing dirty, skipped draw\n");
    }

    s_dirty = 0;
    Dashboard_MarkRenderDone(data);
}

void Dashboard_GuiSleep(void)
{
    if (!s_ready) return;
    /* >>> EDIT 2c <<< : panel deep sleep */
    EPD_2in13_V4_Sleep();
    DASH_LOG("panel sleep\n");
}

bool Dashboard_GuiIsReady(void){ return s_ready; }
