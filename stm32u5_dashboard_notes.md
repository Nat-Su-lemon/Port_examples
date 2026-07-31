# STM32U585 E-Ink Dashboard — Design & Reference Notes

Consolidated notes covering: STM32U5 low-power modes and wake sources, the Stop 2
current-draw investigation, the portable e-ink GUI architecture, the sensor data
model, and the OPT4001 ambient-light driver.

Target: **STM32U585CIU6Q**. Panel: Waveshare e-ink (Paint/GUI_Paint library).

---

## 1. Low-Power Modes (Stop 2 / Standby / Shutdown)

| Mode | Regulator / Core | RAM retention | Wake behavior | Wake sources |
|------|------------------|---------------|---------------|--------------|
| **Stop 2** | LPR on, core clocks off | All SRAM + registers retained | Resumes **in place** after WFI/WFE | Any EXTI/GPIO edge, RTC, LPUART/LPTIM/I2C3, comparators, IWDG, tamper |
| **Standby** | Core powered off | Lost except backup domain; optional SRAM2 / BKPSRAM retention | **Reset-like restart** (SBF flag set) | WKUP pins only, RTC, IWDG, BOR, NRST, tamper |
| **Shutdown** | Core + most of backup off, BOR off | Lost except backup registers (if LSE/RTC alive) | **Power-on-reset** (no SBF flag) | WKUP pins only, RTC (if kept running), NRST |

Key distinctions:

- **Stop 2 resumes execution where it left off.** Standby and Shutdown reboot from
  the reset vector — detect Standby via `__HAL_PWR_GET_FLAG(PWR_FLAG_SBF)`; Shutdown
  looks like a fresh power-on and has **no** SBF flag.
- Before entering Standby/Shutdown, **clear pending wake flags** or the part wakes
  immediately (`HAL_PWR_ClearWakeUpFlag(PWR_WAKEUP_ALL_FLAG)` and
  `__HAL_RTC_WAKEUPTIMER_CLEAR_FLAG`).
- Wake-pin polarity and internal pull config must be set explicitly; floating WKUP
  inputs cause spurious immediate wakes (the classic Standby-loop bug).

### GPIO wake constraint (this board)

Standby and Shutdown wake **only on the dedicated WKUP pins**, not arbitrary EXTI
edges (the EXTI/GPIO logic in VCORE is powered down). On this design the available
pins **PA8, PA9, PA10 are not wired as WKUP sources**, so:

- **Need periodic wake only →** use RTC wakeup timer in Standby/Shutdown. Cleanest,
  lowest power, no pin needed.
- **Need an asynchronous "pin changed, wake now" trigger →** use **Stop 2** instead.
  In Stop 2 the EXTI is alive, so *any* GPIO can wake via a normal EXTI interrupt.
  Cost is higher current (single-digit µA vs. ~210 nA Standby), SRAM stays retained,
  and it resumes in place (no reset).

### RTC wake gotcha (STM32U5)

The RTC wakeup timer needs the **internal wakeup line enabled on the RTC side**, not
just the timer configured. A common symptom is "pin wake works but RTC never fires."
CubeMX's RTC Wake-Up activation handles it; if configuring by hand, that's the missing
piece. LSE (or LSI) must be running and selected as RTC source, and that init must run
on **every** boot (Standby/Shutdown reboot the core).

---

## 2. Stop 2 Current-Draw Investigation

**Symptom observed:** ~1 mA baseline with fast ~25 mA pulses on the current probe,
"when I go into Stop 2." Bursts *decrease* on entry but don't drop to µA.

Stop 2 on the U585 should be **single-digit µA** (datasheet ~4–9 µA depending on SRAM
retention). Reading mA means the part isn't truly in Stop 2 or something on the rail
is held active. Causes, in rough priority order:

1. **Debugger attached (most common).** SWD connected + debug-in-low-power bit set
   (CubeIDE often sets it) keeps clocks/debug domain alive → hundreds of µA to mA with
   a ragged, spiky trace. **Test: unplug the ST-Link, power the board standalone,
   re-measure.** "Bursts shrink but a floor remains" is textbook debug-attached
   behavior.
2. **GPIOs driving/floating.** In Stop 2 I/O states are retained; a pin driving a
   load, floating against a pull, or fighting an external pull keeps drawing (floating
   inputs oscillate → fast spikes). Set every unused pin to `GPIO_MODE_ANALOG` and put
   pins to external devices in a defined state before entry.
3. **Analog/autonomous peripherals left enabled.** Stop 2 keeps ADC4, comparators,
   OPAMPs, VREFBUF, LPUART/LPTIM/I2C3 (autonomous) alive if not disabled. VREFBUF and
   OPAMPs draw meaningfully. Disable before entry.
4. **External device on the rail** (e.g. a WiFi/BLE radio) not put to sleep — its own
   regulator/oscillator keeps running; the 25 mA bursts especially smell like radio
   beacons/keepalive. Sleep it **explicitly**; the MCU sleeping doesn't sleep it.

**Bisection procedure:**

1. Pull the debugger, measure standalone. (Resolves most cases.)
2. Still mA → hold external peripherals in reset / depopulate; if spikes vanish they
   were never the MCU.
3. Still high on a bare MCU → audit init: disable VREFBUF/ADC/OPAMPs/comparators, set
   all GPIOs analog or defined-pull before `HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI)`.

**Diagnostic tip:** toggle a GPIO high right before the Stop-2 entry call and low at
the top of `main()`/on wake; scope it against the current trace. If it never stays
high → not entering the mode. If it pulses rapidly → wake loop.

---

## 3. Panel Sleep + Entry Ordering

Correct sequence each wake:

1. Gather sensor data into the struct.
2. Render the dashboard (draws into RAM framebuffer, flushes to panel).
3. Wait for the refresh to finish (`Dashboard_TakeRenderDone`).
4. Put the **panel** into deep sleep (`EPD_..._Sleep()` via `DashboardGUI_Sleep`) so
   it draws ~0 current.
5. Enter Stop 2 (or Standby) — with the panel asleep and sensors idle, nothing holds
   current high.

---

## 4. Architecture Overview

```
[ sensors / wifi / rtc ]  --write field + set ready bit-->  dashboard_data_t
                                                                   |
                                                        (read by, never mutated)
                                                                   v
                                                          dashboard_gui  (renderer)
                                                                   |
                                                        draws into Paint framebuffer
                                                                   |
                                                          panel callbacks  (flush)
                                                                   v
                                                          Waveshare e-ink panel
```

Files:

| File | Role |
|------|------|
| `dashboard_data.h` | The data contract — one struct + ready-flag bitmask shared by producers and the GUI. Inline helpers only. |
| `dashboard_gui.h/.c` | Portable renderer built on Waveshare Paint. Panel bound via 3 callbacks. |
| `sensor_hub.c` | Acquisition layer: reads each sensor, converts units, writes struct fields, sets/clears ready bits. |
| `opt4001.h/.c` | OPT4001 ambient-light driver with user-supplied I2C functions. |
| `example_usage.c` | End-to-end wiring reference. |

The **only** thing the GUI and the sensors share is `dashboard_data_t`. Swap a sensor
driver or the WiFi stack without touching the GUI; swap panels by rewriting 3 callbacks.

---

## 5. The Data Model (`dashboard_data.h`)

- **Fixed-point integers**, not floats: `temp_c_x100` (0.01 °C), `pressure_pa` (Pa),
  `humidity_x100` (0.01 %RH), `light_lux` (lux), `batt_mv` (mV), `batt_soc_pct` (%).
  Cheap to copy, printf-free on the hot path, safe to stash in backup registers.
- **`ready_flags`**: one 32-bit bitmask (`DASH_FLAG_TEMP`, `..._WIFI`, etc.). A set bit
  means "this field holds valid data."
- **`render_done`**: set by the GUI after a full refresh; the main loop polls it before
  powering down.

Producer side (inline helpers):

```c
data->temp_c_x100 = 2345;                      // write field
Dashboard_MarkReady(&data, DASH_FLAG_TEMP);    // latch it valid
Dashboard_MarkStale(&data, DASH_FLAG_TEMP);    // mark invalid (shows "--")
```

### Sticky-vs-expiring semantics (important)

`ready_flags` and field values are **latched**. `Dashboard_MarkReady` only ORs bits in;
the render path **never** clears a flag or mutates a value. So:

- **"ready" = "has valid data" (latched), not "fresh since last render."** The struct is
  a persistent snapshot; each refresh re-paints whatever it currently holds.
- **Event-driven fields (e.g. WiFi password) persist across every refresh** until your
  code overwrites them. The credential stays on screen indefinitely, surviving periodic
  RTC-driven refreshes, and only changes when you rotate it:

  ```c
  Dashboard_MarkStale(&data, DASH_FLAG_WIFI);   // optional: blank first
  strcpy(data.wifi_pass, new_pass);
  Dashboard_MarkReady(&data, DASH_FLAG_WIFI);   // new value now sticky
  ```

- **Poll-driven fields self-expire on failure** because `sensor_hub.c` calls
  `Dashboard_MarkStale` when a read fails — a dead sensor reverts to `"--"` instead of
  freezing an old reading.

Both behaviors coexist: live sensors expire on failure; latched fields like the password
persist until explicitly changed. (If you want the password to auto-blank at the 1-week
TTL even before new credentials arrive, add a `Dashboard_MarkStale(&data, DASH_FLAG_WIFI)`
in the expiry check.)

---

## 6. The GUI Renderer (`dashboard_gui.c`)

- **Composites in RAM, flushes once.** All `Paint_*` calls draw into the framebuffer;
  nothing hits the panel until `panel->flush()`. The screen updates in one clean shot.
- **Walking-cursor layout.** A `g_y` cursor walks down the screen; each `draw_*` helper
  renders one row and advances it. Reorder the screen by moving `draw_*` calls — no pixel
  math. Layout constants (`ROW_H`, `VALUE_X`, fonts) are at the top of the file.
- **Auto de-ghosting.** Forces a full refresh every N partial refreshes
  (`full_refresh_every`), since e-ink partial refreshes accumulate ghosting.

### Not-ready fields: **skip vs. wait**

The renderer **never waits and never blocks.** A clear ready bit only changes *what
string is drawn*, not *whether to proceed*:

```c
Paint_DrawString_EN(VALUE_X, g_y, ready ? value : "--", &FONT_ROW, BG, FG);
g_y += ROW_H;   // advances regardless — layout stays fixed, no reflow
```

`DashboardGUI_Render` runs straight through (title → environment → battery → wifi) in a
single non-blocking pass. Any not-ready field renders as `"--"`.

The "wait" decision lives **one level up in your main loop**, via the gate before render:

```c
if (Dashboard_IsReady(&g_data, DASH_FLAG_TIME | DASH_FLAG_BATTERY)) {
    DashboardGUI_Render(&g_gui, &g_data);
    while (!Dashboard_TakeRenderDone(&g_data)) { /* spin or yield */ }
    DashboardGUI_Sleep(&g_gui);
}
```

This split is deliberate: the renderer stays dumb and non-blocking (a full e-ink refresh
already takes hundreds of ms; you don't want it stalling on a slow sensor), while the
*policy* of "ready enough to bother refreshing" stays where you can tune it.

> To **omit** a not-ready row entirely (row disappears, rows below shift up) instead of
> showing `"--"`, guard the whole `draw_row` call on the ready bit so it neither draws
> nor advances the cursor.

### WiFi password display policy

The password renders in clear text **only** when `wifi_show_pass` is set (guest-kiosk
case); otherwise it masks to `********`. Deliberate switch so a credential isn't painted
to screen by accident.

### Panel binding

Bind your panel with three callbacks — the only board-specific glue:

```c
typedef struct {
    void (*init)(bool full_refresh);   // EPD_..._Init() / _Init_Fast()
    void (*flush)(uint8_t *framebuffer);// EPD_..._Display(buf)
    void (*sleep)(void);                // EPD_..._Sleep()
} dashboard_panel_t;
```

Framebuffer size for 1bpp = `ceil(width/8) * height`. Pass native (pre-rotation) W/H to
`Paint_NewImage` with the appropriate `ROTATE_*`.

---

## 7. Sensor Hub (`sensor_hub.c`)

`SensorHub_Poll()` is the single call the main loop makes each wake. One `read_*` function
per source (light, environment, battery, time, wifi), each doing unit conversion into the
struct and returning success/failure. Fields publish **independently**: a failing sensor
leaves its bit clear (GUI shows `"--"`) without blocking the others. Add a sensor = one
`read_*` + one line in `Poll()`.

```c
if (read_light(hub, d)) Dashboard_MarkReady(d, DASH_FLAG_LIGHT);
else                    Dashboard_MarkStale(d, DASH_FLAG_LIGHT);
```

The OPT4001 path is fully wired; BME280 / MAX17048 / RTC reads are stubbed with the exact
unit conversions marked for you to drop in.

---

## 8. OPT4001 Ambient-Light Driver (`opt4001.h/.c`)

TI OPT4001. Register map and bit fields taken from the datasheet, cross-checked against
the mainline Linux IIO driver (`drivers/iio/light/opt4001.c`).

### Register map

| Reg | Addr | Contents |
|-----|------|----------|
| RESULT_MSB | `0x00` | `[15:12]` EXPONENT, `[11:0]` MANTISSA[19:8] |
| RESULT_LSB | `0x01` | `[15:8]` MANTISSA[7:0], `[7:4]` sample counter, `[3:0]` CRC |
| CTRL | `0x0A` | `[13:10]` range (0xC=auto), `[9:6]` conv-time, `[5:4]` op-mode |
| DEVICE_ID | `0x11` | device ID (low 12 bits) |

I2C address by ADDR strap: `0x44` (GND, default) / `0x45` (VDD) / `0x46` (SDA) / `0x47` (SCL).
16-bit registers, **big-endian**, addressed by an 8-bit register pointer.

### Lux conversion (float-free)

```
adc_codes = mantissa << exponent            // up to 28 bits
lux       = adc_codes * coefficient         // coefficient is package-specific
```

Coefficient stored as an integer ("nano per code"):
- **SOT-5X3 (DTS): 437.5e-6 → use 437**
- **PicoStar: 312.5e-6 → use 312**

**Pick the constant matching your package** at `opt4001_init_handle`. 64-bit intermediates
prevent overflow on full-scale codes.

**Verified:** TI's worked example (mantissa `0x8CB00`, exponent 2) decodes to **1007 lux**
against their expected ~1000, and the CRC roundtrip validates.

### API

```c
void opt4001_init_handle(opt4001_t *dev, uint8_t i2c_addr,
                         uint32_t lux_coeff_nano, void *user);
opt4001_status_t opt4001_probe(opt4001_t *dev);                 // verify ID
opt4001_status_t opt4001_configure(opt4001_t *dev, uint8_t convtime_idx);
opt4001_status_t opt4001_read(opt4001_t *dev, opt4001_result_t *out);
```

### I2C functions — YOU implement these two

```c
int opt4001_i2c_read (void *user, uint8_t addr7, uint8_t reg,
                      uint8_t *data, uint16_t len);   // write reg, repeated-start, read
int opt4001_i2c_write(void *user, uint8_t addr7, uint8_t reg,
                      const uint8_t *data, uint16_t len);
```

Return 0 on success. `user` is `dev->user` passed through untouched (use for your
`I2C_HandleTypeDef*`). STM32 HAL implementation (in an `#if 0` block in `opt4001.c`):

```c
int opt4001_i2c_read(void *user, uint8_t addr7, uint8_t reg,
                     uint8_t *data, uint16_t len) {
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)user;
    if (HAL_I2C_Mem_Read(hi2c, (uint16_t)(addr7 << 1), reg,
                         I2C_MEMADD_SIZE_8BIT, data, len, 100) != HAL_OK)
        return -1;
    return 0;
}
```

### CRC caveat

The 4-bit CRC is a parity check over `{mantissa, exponent, counter}`. The algorithm runs
correctly and validates roundtrip, but the exact per-bit mask table (esp. the upper CRC
bits) was reconstructed from the datasheet's documented algorithm rather than copied
verbatim. If real hardware reports CRC failures on known-good reads, verify that mask
table against your datasheet's CRC section — or treat `OPT4001_ERR_CRC` as non-fatal (a
warning) rather than dropping the sample.

---

## 9. Quick Checklists

**Bring-up order (every boot — Standby/Shutdown reboot the core):**
1. `HAL_Init`, clocks.
2. RTC init with LSE selected, internal wakeup line enabled.
3. I2C init.
4. `SensorHub_Init` (probes + configures OPT4001, etc.).
5. `DashboardGUI_Init` (binds panel + framebuffer, calls `Paint_NewImage`).
6. Check `PWR_FLAG_SBF` / backup-register magic to detect wake-from-deep-sleep.

**Each wake loop:**
1. `SensorHub_Poll` → fills struct, sets/clears ready bits.
2. Gate on required flags → `DashboardGUI_Render`.
3. Wait `Dashboard_TakeRenderDone`.
4. `DashboardGUI_Sleep` (panel deep sleep).
5. Enter Stop 2 / Standby; wake on RTC.

**Low-power sanity:**
- Debugger unplugged for real current measurement.
- All unused GPIOs → analog; external-device pins in defined states.
- VREFBUF / ADC / OPAMPs / comparators disabled before entry.
- Wake flags cleared before entering Standby/Shutdown.
- Panel asleep, external radio/sensors explicitly slept.
