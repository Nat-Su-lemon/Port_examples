/******************************************************************************
 * dashboard_data.h
 *
 * THE DATA CONTRACT between your sensor/system code and the e-ink GUI.
 *
 * Design philosophy
 * -----------------
 * This struct is the *only* thing the GUI and your sensor drivers share.
 *   - PRODUCERS (sensor tasks, WiFi stack, fuel gauge, RTC) WRITE fields
 *     and set the matching "ready" bit.
 *   - The CONSUMER (the GUI renderer) READS fields whose ready bit is set
 *     and draws them.
 * Neither side needs to know how the other works, so you can swap out a
 * BME280 for an SHT4x, or the WiFi driver, without touching the GUI at all.
 *
 * Porting in your own sensors
 * ---------------------------
 * 1. Your driver computes a value (e.g. temperature in 0.01 C).
 * 2. It writes the field:            data->temp_c_x100 = 2345;   // 23.45 C
 * 3. It marks that field ready:      Dashboard_MarkReady(data, DASH_FLAG_TEMP);
 * That's the whole integration surface. The GUI picks it up on next render.
 *
 * Fixed-point note
 * ----------------
 * We store scaled integers (x100, x1000, mV, etc.) instead of floats so the
 * struct is cheap to copy, safe to stash in backup registers, and printf-free
 * on the hot path. The GUI formats them into strings at draw time.
 ******************************************************************************/
#ifndef DASHBOARD_DATA_H
#define DASHBOARD_DATA_H

#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * "Ready" flag bits.
 * Each sensor field has one bit here. A producer sets its bit when the
 * corresponding field holds fresh, valid data. The GUI uses these bits to
 * decide what to draw (and can show a placeholder for fields not yet ready).
 * Using a bitmask (not one bool per field) keeps it to a single 32-bit word
 * that is trivial to snapshot atomically.
 * ---------------------------------------------------------------------- */
typedef enum {
    DASH_FLAG_TEMP      = (1u << 0),   /* temperature valid              */
    DASH_FLAG_PRESSURE  = (1u << 1),   /* pressure valid                 */
    DASH_FLAG_HUMIDITY  = (1u << 2),   /* humidity valid                 */
    DASH_FLAG_LIGHT     = (1u << 3),   /* ambient light valid            */
    DASH_FLAG_WIFI      = (1u << 4),   /* wifi credential/status valid   */
    DASH_FLAG_BATTERY   = (1u << 5),   /* battery SoC + voltage valid    */
    DASH_FLAG_TIME      = (1u << 6),   /* time-of-day valid              */

    /* Convenience mask: "every sensor field is populated". */
    DASH_FLAG_ALL       = DASH_FLAG_TEMP | DASH_FLAG_PRESSURE |
                          DASH_FLAG_HUMIDITY | DASH_FLAG_LIGHT |
                          DASH_FLAG_WIFI | DASH_FLAG_BATTERY |
                          DASH_FLAG_TIME
} dashboard_flag_t;

/* WiFi association state, shown as an icon/label on screen. */
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

/* Simple wall-clock holder. Fill this from your RTC (HAL_RTC_GetTime/Date).
 * Kept separate from HAL types so the GUI stays portable / testable on PC. */
typedef struct {
    uint8_t hour;      /* 0..23 */
    uint8_t minute;    /* 0..59 */
    uint8_t second;    /* 0..59 */
    uint8_t day;       /* 1..31 */
    uint8_t month;     /* 1..12 */
    uint16_t year;     /* e.g. 2026 */
} dash_time_t;

#define WIFI_SSID_MAXLEN  32
#define WIFI_PASS_MAXLEN  64   /* 63 chars + NUL, matches WPA2 max */

/* -------------------------------------------------------------------------
 * The dashboard data model.
 *
 * All values are fixed-point integers (see header note). The comment on each
 * field gives the unit and scale so your producer knows exactly what to write.
 * ---------------------------------------------------------------------- */
typedef struct {

    /* ---- Environmental sensors ---- */
    int32_t  temp_c_x100;      /* temperature, hundredths of C   (2345 = 23.45 C) */
    uint32_t pressure_pa;      /* pressure, whole Pascals        (101325 = 1013.25 hPa) */
    uint16_t humidity_x100;    /* relative humidity, hundredths of % (4550 = 45.50 %) */
    uint32_t light_lux;        /* ambient light, whole lux        */

    /* ---- WiFi ---- */
    char         wifi_ssid[WIFI_SSID_MAXLEN + 1];  /* network name, NUL-terminated */
    char         wifi_pass[WIFI_PASS_MAXLEN + 1];  /* guest password, NUL-terminated */
    wifi_state_t wifi_state;                        /* association state */
    bool         wifi_show_pass;                    /* true = print password in clear
                                                       (e.g. a guest kiosk); false = mask it */

    /* ---- Battery ---- */
    uint16_t batt_soc_pct;     /* state of charge, whole percent 0..100 */
    uint16_t batt_mv;          /* battery voltage in millivolts  (3700 = 3.700 V) */

    /* ---- Time ---- */
    dash_time_t time;          /* current wall-clock, from RTC */

    /* ---- Synchronization / lifecycle ---- */

    /* Bitmask of DASH_FLAG_* bits. A bit set == that field is valid.
     * Producers set bits via Dashboard_MarkReady(); GUI reads them. */
    volatile uint32_t ready_flags;

    /* Set by the GUI (via Dashboard_MarkRenderDone) after a full screen
     * refresh completes. Your main loop can poll this to know it's safe to
     * power down the panel and enter Stop 2 / Standby. */
    volatile bool render_done;

} dashboard_data_t;


/* -------------------------------------------------------------------------
 * Small inline helpers for the producer side.
 * Inline so there's zero call overhead and no extra .c to link.
 * ---------------------------------------------------------------------- */

/* Zero the model and clear all flags. Call once at startup. */
static inline void Dashboard_Init(dashboard_data_t *d)
{
    /* memset-free clear so we don't drag in string.h here; the struct is
     * small and this is called once. */
    uint8_t *p = (uint8_t *)d;
    for (uint32_t i = 0; i < sizeof(*d); i++) p[i] = 0;
    d->wifi_state = WIFI_STATE_DISCONNECTED;
}

/* Producer: mark one or more fields valid (OR the bits in). */
static inline void Dashboard_MarkReady(dashboard_data_t *d, uint32_t flags)
{
    d->ready_flags |= flags;
}

/* Producer: mark one or more fields stale/invalid (clear the bits). */
static inline void Dashboard_MarkStale(dashboard_data_t *d, uint32_t flags)
{
    d->ready_flags &= ~flags;
}

/* Consumer or main loop: is a given field ready? */
static inline bool Dashboard_IsReady(const dashboard_data_t *d, uint32_t flags)
{
    return (d->ready_flags & flags) == flags;
}

/* GUI: called internally once a full refresh finishes. */
static inline void Dashboard_MarkRenderDone(dashboard_data_t *d)
{
    d->render_done = true;
}

/* Main loop: has the GUI finished the last refresh? (Clears the flag.) */
static inline bool Dashboard_TakeRenderDone(dashboard_data_t *d)
{
    if (d->render_done) { d->render_done = false; return true; }
    return false;
}

#endif /* DASHBOARD_DATA_H */
