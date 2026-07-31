/******************************************************************************
 * sensor_hub.c
 *
 * The acquisition layer that sits between raw sensor drivers and the GUI's
 * dashboard_data_t. Your main loop calls SensorHub_Poll() once per wake; it
 * reads each sensor, converts to the dashboard's fixed-point units, writes the
 * struct field, and sets the ready flag. If a sensor read fails, that field's
 * ready bit is left clear so the GUI shows "--" rather than stale/garbage data.
 *
 * This is where "organize and put data into the structs" happens. Each sensor
 * gets a tiny read_* function so you can enable/disable or swap them one at a
 * time. Add a new sensor by writing one read_* and calling it from Poll().
 ******************************************************************************/
#include "dashboard_data.h"
#include "opt4001.h"

/* Your other sensor drivers would be included here, e.g.: */
/* #include "bme280.h"     temp/pressure/humidity            */
/* #include "max17048.h"   battery fuel gauge                 */

/* -------------------------------------------------------------------------
 * Hub context: handles for every sensor plus whatever bus handles they need.
 * One instance, initialized once at boot.
 * ---------------------------------------------------------------------- */
typedef struct {
    opt4001_t light;        /* OPT4001 handle                              */
    /* bme280_t   env;      other driver handles ...                       */
    /* max17048_t gauge;                                                   */
    void      *rtc;         /* opaque RTC handle (RTC_HandleTypeDef*)      */
} sensor_hub_t;

/* -------------------------------------------------------------------------
 * Init: set up every sensor handle. Call once after your I2C/RTC bring-up.
 * `i2c` is your I2C handle, passed through to each driver's `user` pointer.
 * ---------------------------------------------------------------------- */
void SensorHub_Init(sensor_hub_t *hub, void *i2c, void *rtc)
{
    hub->rtc = rtc;

    /* OPT4001 on the default (ADDR->GND) address, SOT-5X3 package coefficient. */
    opt4001_init_handle(&hub->light,
                        OPT4001_I2C_ADDR_GND,
                        OPT4001_LUX_NANO_PER_CODE_SOT5X3,
                        i2c);
    (void)opt4001_probe(&hub->light);
    (void)opt4001_configure(&hub->light, OPT4001_CONVTIME_100MS);

    /* bme280_init(&hub->env, ...);  max17048_init(&hub->gauge, ...); */
}

/* =========================================================================
 * Per-sensor read functions.
 * Each returns true on success (field written + ready), false on failure
 * (field left untouched, caller leaves the ready bit clear).
 * ====================================================================== */

/* --- Ambient light (OPT4001) --- */
static bool read_light(sensor_hub_t *hub, dashboard_data_t *d)
{
    opt4001_result_t r;
    if (opt4001_read(&hub->light, &r) != OPT4001_OK)
        return false;           /* I2C error or CRC fail: don't publish */

    d->light_lux = r.lux;       /* dashboard wants whole lux */
    return true;
}

/* --- Environmental (BME280 or similar) ---
 * Stub showing the unit conversions the dashboard expects. Replace the
 * fake reads with your driver calls. */
static bool read_environment(sensor_hub_t *hub, dashboard_data_t *d)
{
    (void)hub;
    /* Example: bme280_read(&hub->env, &t, &p, &h);  where the driver gives:
     *   t  in 0.01 C   -> temp_c_x100   (direct)
     *   p  in Pa        -> pressure_pa   (direct)
     *   h  in 0.01 %RH  -> humidity_x100 (direct)
     * If your driver returns different units, scale here. */

    int32_t  t_x100 = 2345;      /* 23.45 C  (replace) */
    uint32_t p_pa   = 101325;    /* 1013.25 hPa (replace) */
    uint16_t h_x100 = 4550;      /* 45.50 %RH (replace) */

    d->temp_c_x100   = t_x100;
    d->pressure_pa   = p_pa;
    d->humidity_x100 = h_x100;
    return true;
}

/* --- Battery (MAX17048 fuel gauge) --- */
static bool read_battery(sensor_hub_t *hub, dashboard_data_t *d)
{
    (void)hub;
    /* max17048_read(&hub->gauge, &soc, &mv); */
    uint16_t soc = 87;           /* % (replace) */
    uint16_t mv  = 3912;         /* mV (replace) */

    d->batt_soc_pct = soc;
    d->batt_mv      = mv;
    return true;
}

/* --- Time (RTC) --- */
static bool read_time(sensor_hub_t *hub, dashboard_data_t *d)
{
    (void)hub;
    /* Fill from HAL_RTC_GetTime/GetDate. Read TIME then DATE (HAL requirement
     * to unlock the shadow registers), then copy into dash_time_t. Example:
     *
     *   RTC_TimeTypeDef t; RTC_DateTypeDef dt;
     *   HAL_RTC_GetTime(hub->rtc, &t, RTC_FORMAT_BIN);
     *   HAL_RTC_GetDate(hub->rtc, &dt, RTC_FORMAT_BIN);
     *   d->time.hour = t.Hours; d->time.minute = t.Minutes; ...
     */
    d->time.hour   = 14;         /* (replace with RTC read) */
    d->time.minute = 30;
    d->time.second = 0;
    d->time.day    = 31;
    d->time.month  = 7;
    d->time.year   = 2026;
    return true;
}

/* --- WiFi credential ---
 * Not a "sensor" but fits the same pattern. Pull from wherever your WiFi
 * provisioning stores the guest password (RAM, backup registers, flash). */
static bool read_wifi(sensor_hub_t *hub, dashboard_data_t *d)
{
    (void)hub;
    /* Copy SSID/pass from your store; set state from the WiFi stack. */
    const char *ssid = "GuestNet";     /* (replace) */
    const char *pass = "welcome123";   /* (replace) */
    int i;
    for (i = 0; i < WIFI_SSID_MAXLEN && ssid[i]; i++) d->wifi_ssid[i] = ssid[i];
    d->wifi_ssid[i] = '\0';
    for (i = 0; i < WIFI_PASS_MAXLEN && pass[i]; i++) d->wifi_pass[i] = pass[i];
    d->wifi_pass[i] = '\0';
    d->wifi_state     = WIFI_STATE_CONNECTED;
    d->wifi_show_pass = true;          /* guest kiosk: display in clear */
    return true;
}

/* =========================================================================
 * SensorHub_Poll
 *
 * The one call your main loop makes each wake. Reads everything, publishing
 * each field independently: a failing sensor doesn't block the others, and
 * only successfully-read fields get their ready bit set. The GUI then renders
 * whatever is ready.
 * ====================================================================== */
void SensorHub_Poll(sensor_hub_t *hub, dashboard_data_t *d)
{
    if (read_light(hub, d))       Dashboard_MarkReady(d, DASH_FLAG_LIGHT);
    else                          Dashboard_MarkStale(d, DASH_FLAG_LIGHT);

    if (read_environment(hub, d)) Dashboard_MarkReady(d,
                                     DASH_FLAG_TEMP | DASH_FLAG_PRESSURE |
                                     DASH_FLAG_HUMIDITY);
    else                          Dashboard_MarkStale(d,
                                     DASH_FLAG_TEMP | DASH_FLAG_PRESSURE |
                                     DASH_FLAG_HUMIDITY);

    if (read_battery(hub, d))     Dashboard_MarkReady(d, DASH_FLAG_BATTERY);
    else                          Dashboard_MarkStale(d, DASH_FLAG_BATTERY);

    if (read_time(hub, d))        Dashboard_MarkReady(d, DASH_FLAG_TIME);
    else                          Dashboard_MarkStale(d, DASH_FLAG_TIME);

    if (read_wifi(hub, d))        Dashboard_MarkReady(d, DASH_FLAG_WIFI);
    else                          Dashboard_MarkStale(d, DASH_FLAG_WIFI);
}
