/******************************************************************************
 * example_usage.c
 *
 * Shows the full wiring:  sensors -> dashboard_data_t -> GUI -> panel,
 * plus where the Stop 2 entry goes. This is illustrative; drop the pieces
 * into your real main.c.
 ******************************************************************************/
#include "dashboard_gui.h"
#include "dashboard_data.h"
#include "GUI_Paint.h"   /* for ROTATE_90 and the BLACK/WHITE macros */

/* Your Waveshare panel driver headers, e.g.: */
/* #include "EPD_2in13_V4.h" */

/* -------------------------------------------------------------------------
 * 1. PANEL BINDING
 * Wrap YOUR panel's functions in the three callbacks. This is the only
 * board-specific glue. To use a different Waveshare panel, rewrite these
 * three wrappers and nothing else changes.
 * ---------------------------------------------------------------------- */
static void panel_init(bool full_refresh)
{
    if (full_refresh) {
        /* EPD_2in13_V4_Init();      full, clean, no ghost */
    } else {
        /* EPD_2in13_V4_Init_Fast(); partial/fast if supported */
    }
}
static void panel_flush(uint8_t *fb)
{
    /* EPD_2in13_V4_Display(fb); */
    (void)fb;
}
static void panel_sleep(void)
{
    /* EPD_2in13_V4_Sleep();  deep sleep so panel draws ~0 in Stop 2 */
}

static const dashboard_panel_t my_panel = {
    .init  = panel_init,
    .flush = panel_flush,
    .sleep = panel_sleep,
};

/* -------------------------------------------------------------------------
 * 2. STORAGE
 * The framebuffer size = ceil(width/8) * height for 1bpp. For a 250x122
 * panel driven at native 122x250 that's ceil(122/8)*250 = 16*250 = 4000 B.
 * Size this to YOUR panel.
 * ---------------------------------------------------------------------- */
#define PANEL_W        122          /* native (pre-rotation) width  */
#define PANEL_H        250          /* native (pre-rotation) height */
#define FRAMEBUF_BYTES (((PANEL_W + 7) / 8) * PANEL_H)

static uint8_t          g_framebuf[FRAMEBUF_BYTES];
static dashboard_gui_t  g_gui;
static dashboard_data_t g_data;

/* -------------------------------------------------------------------------
 * 3. SENSOR PRODUCERS (examples)
 * Each sensor task writes its field(s) and marks them ready. In real code
 * these run from your BME280/light-sensor/fuel-gauge drivers, ISRs, or DMA
 * completion callbacks. Note they touch ONLY the data struct.
 * ---------------------------------------------------------------------- */
static void on_bme280_sample(int32_t t_x100, uint32_t p_pa, uint16_t rh_x100)
{
    g_data.temp_c_x100  = t_x100;
    g_data.pressure_pa  = p_pa;
    g_data.humidity_x100 = rh_x100;
    Dashboard_MarkReady(&g_data,
        DASH_FLAG_TEMP | DASH_FLAG_PRESSURE | DASH_FLAG_HUMIDITY);
}

static void on_light_sample(uint32_t lux)
{
    g_data.light_lux = lux;
    Dashboard_MarkReady(&g_data, DASH_FLAG_LIGHT);
}

static void on_fuel_gauge(uint16_t soc_pct, uint16_t mv)   /* MAX17048 */
{
    g_data.batt_soc_pct = soc_pct;
    g_data.batt_mv      = mv;
    Dashboard_MarkReady(&g_data, DASH_FLAG_BATTERY);
}

static void on_wifi_provisioned(const char *ssid, const char *pass)
{
    /* copy with bounds; fields are fixed arrays */
    for (int i = 0; i < WIFI_SSID_MAXLEN && ssid[i]; i++) g_data.wifi_ssid[i] = ssid[i];
    for (int i = 0; i < WIFI_PASS_MAXLEN && pass[i]; i++) g_data.wifi_pass[i] = pass[i];
    g_data.wifi_state     = WIFI_STATE_CONNECTED;
    g_data.wifi_show_pass = true;   /* guest kiosk: show it in clear */
    Dashboard_MarkReady(&g_data, DASH_FLAG_WIFI);
}

static void on_rtc_read(dash_time_t t)
{
    g_data.time = t;
    Dashboard_MarkReady(&g_data, DASH_FLAG_TIME);
}

/* -------------------------------------------------------------------------
 * 4. MAIN LOOP
 * ---------------------------------------------------------------------- */
int example_main(void)
{
    /* HAL_Init(); SystemClock_Config(); ... your normal bring-up ... */

    Dashboard_Init(&g_data);
    DashboardGUI_Init(&g_gui, &my_panel, g_framebuf,
                      /* logical, post-rotation */ PANEL_H, PANEL_W,
                      ROTATE_90);

    for (;;) {
        /* --- gather sensor data (however your app schedules it) --- */
        /* on_rtc_read(read_rtc());                                   */
        /* on_bme280_sample(...); on_light_sample(...);               */
        /* on_fuel_gauge(...);                                        */

        /* Only redraw once the fields we care about are fresh. You can
         * gate on DASH_FLAG_ALL, or a subset, or just render every wake. */
        if (Dashboard_IsReady(&g_data, DASH_FLAG_TIME | DASH_FLAG_BATTERY)) {

            DashboardGUI_Render(&g_gui, &g_data);

            /* Wait for the refresh to finish before powering down. */
            while (!Dashboard_TakeRenderDone(&g_data)) { /* spin or yield */ }

            /* Panel to deep sleep so it doesn't dominate Stop 2 current. */
            DashboardGUI_Sleep(&g_gui);
        }

        /* --- enter low power ---
         * With the panel asleep and sensors idle, drop the MCU into Stop 2
         * (or Standby). Wake on RTC to refresh again. See earlier discussion
         * for the entry function; the point is the GUI has already flushed
         * and the panel is asleep, so nothing here holds current high. */
        /* enter_low_power(LP_STOP2, refresh_interval_seconds); */
    }
}
