/******************************************************************************
 * opt4001.h
 *
 * Portable driver for the Texas Instruments OPT4001 ambient light sensor.
 *
 * Register map, bit fields, and the CRC algorithm are taken from the OPT4001
 * datasheet (SBOS????) and cross-checked against the mainline Linux IIO driver
 * (drivers/iio/light/opt4001.c). Result is 4-bit EXPONENT + 20-bit MANTISSA
 * split across two 16-bit registers, converted to lux with a package-specific
 * coefficient.
 *
 * PORTING: you implement the two I2C functions declared at the bottom
 * (opt4001_i2c_read / opt4001_i2c_write). Everything else is bus-agnostic.
 * The sensor uses 16-bit big-endian registers addressed by an 8-bit pointer.
 ******************************************************************************/
#ifndef OPT4001_H
#define OPT4001_H

#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * I2C 7-bit addresses, selected by the ADDR pin strapping.
 * ---------------------------------------------------------------------- */
#define OPT4001_I2C_ADDR_GND   0x44   /* ADDR -> GND  (default)   */
#define OPT4001_I2C_ADDR_VDD   0x45   /* ADDR -> VDD              */
#define OPT4001_I2C_ADDR_SDA   0x46   /* ADDR -> SDA              */
#define OPT4001_I2C_ADDR_SCL   0x47   /* ADDR -> SCL              */

/* -------------------------------------------------------------------------
 * Register addresses (8-bit register pointer; each holds a 16-bit value).
 * ---------------------------------------------------------------------- */
#define OPT4001_REG_RESULT_MSB   0x00  /* EXPONENT + MANTISSA[19:8]        */
#define OPT4001_REG_RESULT_LSB   0x01  /* MANTISSA[7:0] + COUNTER + CRC    */
#define OPT4001_REG_THRESHOLD_L  0x08
#define OPT4001_REG_THRESHOLD_H  0x09
#define OPT4001_REG_CTRL         0x0A  /* configuration register          */
#define OPT4001_REG_FLAGS        0x0C
#define OPT4001_REG_DEVICE_ID    0x11

/* -------------------------------------------------------------------------
 * RESULT register field layout (per datasheet / Linux driver).
 *   Reg 0x00: [15:12]=EXPONENT, [11:0]=MANTISSA MSBs (upper 12 of 20)
 *   Reg 0x01: [15:8]=MANTISSA LSBs (lower 8 of 20), [7:4]=sample counter,
 *             [3:0]=CRC
 * ---------------------------------------------------------------------- */
#define OPT4001_EXPONENT_SHIFT   12
#define OPT4001_EXPONENT_MASK    0x000Fu   /* 4 bits after shifting        */
#define OPT4001_MSB_MANT_MASK    0x0FFFu   /* low 12 bits of reg 0x00      */
#define OPT4001_LSB_MANT_SHIFT   8
#define OPT4001_COUNTER_SHIFT    4
#define OPT4001_COUNTER_MASK     0x0Fu
#define OPT4001_CRC_MASK         0x0Fu

/* -------------------------------------------------------------------------
 * CTRL register (0x0A) field definitions.
 *   [15]    QWAKE      quick-wake from standby
 *   [13:10] RANGE      full-scale range (0xC = auto)
 *   [9:6]   CONV_TIME  conversion/integration time index
 *   [5:4]   OP_MODE    operating mode (0=powerdown,1=forced,3=continuous)
 *   [3]     LATCH      latched vs transparent interrupt
 *   [2]     INT_POL
 *   [1:0]   FAULT_CNT
 * ---------------------------------------------------------------------- */
#define OPT4001_CTRL_RANGE_SHIFT     10
#define OPT4001_CTRL_RANGE_AUTO      0x0Cu   /* automatic full-scale range   */
#define OPT4001_CTRL_CONVTIME_SHIFT  6
#define OPT4001_CTRL_OPMODE_SHIFT    4

/* Operating modes (OP_MODE field). */
#define OPT4001_OPMODE_POWERDOWN     0x0u
#define OPT4001_OPMODE_FORCED        0x1u   /* one-shot, then powerdown     */
#define OPT4001_OPMODE_CONTINUOUS    0x3u

/* Conversion-time indices (CONV_TIME field). Longer = lower noise, slower.
 * Index maps to: 0=600us,1=1ms,2=1.8ms,3=3.4ms,4=6.5ms,5=12.7ms,6=25ms,
 *                7=50ms,8=100ms,9=200ms,10=400ms,11=800ms. */
#define OPT4001_CONVTIME_100MS       8u
#define OPT4001_CONVTIME_800MS       11u

/* Expected value in the low 12 bits of the DEVICE_ID register. */
#define OPT4001_DEVICE_ID_MASK       0x0FFFu

/* -------------------------------------------------------------------------
 * Package-specific lux coefficient.
 * lux = ADC_CODES * coeff, where ADC_CODES = MANTISSA << EXPONENT.
 * The coefficient differs by package:
 *   SOT-5X3 (DTS): 437.5e-6   -> we store nanolux-per-code = 437500
 *   PicoStar     : 312.5e-6   -> 312500
 * Pick the one matching YOUR part. We keep it as an integer (nanolux per
 * code) to stay float-free; the read function returns whole lux.
 * ---------------------------------------------------------------------- */
#define OPT4001_LUX_NANO_PER_CODE_SOT5X3   437u   /* 437.5e-6 lux/code, see note */
#define OPT4001_LUX_NANO_PER_CODE_PICOSTAR 312u

/* -------------------------------------------------------------------------
 * Device handle. Holds the bus address and package coefficient. Create one
 * per physical sensor. `user` is an opaque pointer passed straight through to
 * your I2C functions (use it for an I2C_HandleTypeDef*, a bus index, etc.).
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  i2c_addr;          /* 7-bit address (OPT4001_I2C_ADDR_*)     */
    uint32_t lux_milli_per_code;/* lux-per-code * 1e6, see note in .c     */
    void    *user;              /* passed to opt4001_i2c_read/write       */
} opt4001_t;

/* Return codes. */
typedef enum {
    OPT4001_OK = 0,
    OPT4001_ERR_I2C,        /* your I2C function reported failure          */
    OPT4001_ERR_ID,         /* device ID mismatch (wrong/absent chip)      */
    OPT4001_ERR_CRC,        /* result CRC check failed (corrupt read)      */
    OPT4001_ERR_PARAM
} opt4001_status_t;

/* One decoded measurement. */
typedef struct {
    uint32_t lux;           /* whole lux                                   */
    uint32_t adc_codes;     /* linearized 28-bit code (mantissa<<exponent) */
    uint8_t  exponent;      /* 0..8                                        */
    uint32_t mantissa;      /* 20-bit                                      */
    uint8_t  counter;       /* rolling sample counter 0..15                */
    bool     crc_ok;        /* CRC validated                               */
} opt4001_result_t;

/* -------------------------------------------------------------------------
 * Driver API
 * ---------------------------------------------------------------------- */

/* Fill a handle. Call once. `lux_coeff_nano` is one of the
 * OPT4001_LUX_NANO_PER_CODE_* constants for your package. */
void opt4001_init_handle(opt4001_t *dev, uint8_t i2c_addr,
                         uint32_t lux_coeff_nano, void *user);

/* Verify the device ID over I2C. Returns OPT4001_OK if the chip answers. */
opt4001_status_t opt4001_probe(opt4001_t *dev);

/* Configure continuous mode, auto-range, given conversion-time index. */
opt4001_status_t opt4001_configure(opt4001_t *dev, uint8_t convtime_idx);

/* Read + decode one measurement (checks CRC, computes lux). */
opt4001_status_t opt4001_read(opt4001_t *dev, opt4001_result_t *out);

/* =========================================================================
 * I2C HAL — YOU IMPLEMENT THESE TWO.
 *
 * The OPT4001 uses an 8-bit register pointer then 16-bit big-endian data.
 * Both functions return 0 on success, non-zero on failure.
 *
 * read : write `reg` (1 byte), then read `len` bytes into `data`
 *        (repeated-start, no stop between). Typical for register reads.
 * write: write `reg` (1 byte) followed by `len` data bytes.
 *
 * `addr7` is the 7-bit I2C address; shift/format as your HAL expects.
 * `user` is dev->user, passed through untouched (e.g. your I2C handle).
 *
 * Example STM32 HAL implementation is provided (commented) in opt4001.c.
 * ====================================================================== */
int opt4001_i2c_read(void *user, uint8_t addr7, uint8_t reg,
                     uint8_t *data, uint16_t len);
int opt4001_i2c_write(void *user, uint8_t addr7, uint8_t reg,
                      const uint8_t *data, uint16_t len);

#endif /* OPT4001_H */
