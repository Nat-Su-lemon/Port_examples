/******************************************************************************
 * example_dashboard.c
 *
 * Simplest possible use of the single-file dashboard GUI.
 * Three calls: Init once, Refresh to draw, Sleep before low-power.
 ******************************************************************************/
#include "dashboard_gui.h"     /* the GUI pair */
#include "dashboard_data.h"    /* the data model */

/* One data model instance for the whole app. */
static dashboard_data_t g_data;

int main(void)
{
    /* --- your normal MCU bring-up happens first ---
     * HAL_Init();
     * SystemClock_Config();
     * MX_GPIO_Init();
     * MX_SPI1_Init();          // panel SPI must be up before GuiInit
     */

    /* 1. Zero the data model, then bring up the panel + GUI. */
    Dashboard_Init(&g_data);
    if (!Dashboard_GuiInit()) {
        /* init failed — check the [GUI] debug log. Don't draw. */
        for (;;) { }
    }

    for (;;) {
        /* 2. Fill in whatever data you have this cycle.
         *    Write the field, then mark it ready. Unset fields show "--". */

        g_data.temp_c_x100 = 2345;                        /* 23.45 C */
        Dashboard_MarkReady(&g_data, DASH_FLAG_TEMP);

        g_data.humidity_x100 = 4820;                      /* 48.20 % */
        Dashboard_MarkReady(&g_data, DASH_FLAG_HUMIDITY);

        g_data.light_lux = 620;                           /* 620 lux */
        Dashboard_MarkReady(&g_data, DASH_FLAG_LIGHT);

        g_data.batt_mv = 3910;                            /* 3.91 V  */
        g_data.batt_soc_pct = 84;                         /* 84 %    */
        Dashboard_MarkReady(&g_data, DASH_FLAG_BATTERY);

        g_data.time.hour = 14; g_data.time.minute = 30;   /* 14:30   */
        Dashboard_MarkReady(&g_data, DASH_FLAG_TIME);

        /* WiFi: set once and it stays on screen across every refresh
         * until you change it. */
        strcpy(g_data.wifi_ssid, "GuestNet");
        strcpy(g_data.wifi_pass, "welcome123");
        g_data.wifi_show_pass = true;                     /* show in clear */
        Dashboard_MarkReady(&g_data, DASH_FLAG_WIFI);

        /* 3. Draw it. */
        Dashboard_GuiRefresh(&g_data);

        /* 4. Panel to sleep, then enter low power (or just delay for a test). */
        Dashboard_GuiSleep();

        /* enter_low_power(LP_STOP2, 60);   // wake on RTC in 60 s */
        /* --- or, for a bench test without low-power: --- */
        /* HAL_Delay(60000); */
    }
}
