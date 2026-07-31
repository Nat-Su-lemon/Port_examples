/******************************************************************************
 * opt4001.c
 *
 * OPT4001 driver implementation. Bus-agnostic: all I2C traffic goes through
 * the two functions you implement (opt4001_i2c_read/write).
 *
 * Lux math
 * --------
 * The sensor reports EXPONENT (4b) and MANTISSA (20b). The linearized code is
 *   adc_codes = mantissa << exponent            (up to 28 bits)
 * and lux = adc_codes * package_coefficient, where the coefficient is a small
 * fraction (e.g. 437.5e-6 lux/code for SOT-5X3). To stay float-free we store
 * the coefficient as "nanolux per code" and divide:
 *   lux = (adc_codes * nano_per_code) / 1e9        [rearranged to avoid overflow]
 * We use 64-bit intermediates so a full-scale 28-bit code * ~437 can't wrap.
 ******************************************************************************/
#include "opt4001.h"

/* -------------------------------------------------------------------------
 * CRC check.
 * The OPT4001 embeds a 4-bit CRC over {mantissa(20), exponent(4), counter(4)}.
 * The bit rule (from the datasheet, matching the mainline Linux driver) is a
 * set of parity checks over selected bit groups. This is a data-integrity
 * guard; if it fails, treat the sample as corrupt and re-read.
 * ---------------------------------------------------------------------- */
static uint8_t popcount32(uint32_t x)
{
    /* portable parity/popcount without __builtin dependency */
    uint8_t c = 0;
    while (x) { c += (uint8_t)(x & 1u); x >>= 1; }
    return c;
}

static uint8_t opt4001_calc_crc(uint32_t mantissa, uint8_t exp, uint8_t count)
{
    /* Each CRC bit is even parity over a specific mask of the concatenated
     * fields. Masks below mirror the TI-documented algorithm. */
    uint8_t crc = 0;
    crc  =  (uint8_t)((popcount32(mantissa) + popcount32(exp) + popcount32(count)) & 1u);
    crc |= (uint8_t)(((popcount32(mantissa & 0xAAAAAu) + popcount32(exp & 0xAu) +
                       popcount32(count & 0xAu)) & 1u) << 1);
    crc |= (uint8_t)(((popcount32(mantissa & 0xCCCCCu) + popcount32(exp & 0xCu) +
                       popcount32(count & 0xCu)) & 1u) << 2);
    crc |= (uint8_t)(((popcount32(mantissa & 0xF0F0Fu) + popcount32(exp & 0x0u) +
                       popcount32(count & 0x0u)) & 1u) << 3);
    return crc & 0x0Fu;
}

/* Read one 16-bit big-endian register. */
static opt4001_status_t read_reg16(opt4001_t *dev, uint8_t reg, uint16_t *val)
{
    uint8_t buf[2];
    if (opt4001_i2c_read(dev->user, dev->i2c_addr, reg, buf, 2) != 0)
        return OPT4001_ERR_I2C;
    *val = (uint16_t)((buf[0] << 8) | buf[1]);   /* big-endian */
    return OPT4001_OK;
}

/* Write one 16-bit big-endian register. */
static opt4001_status_t write_reg16(opt4001_t *dev, uint8_t reg, uint16_t val)
{
    uint8_t buf[2] = { (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    if (opt4001_i2c_write(dev->user, dev->i2c_addr, reg, buf, 2) != 0)
        return OPT4001_ERR_I2C;
    return OPT4001_OK;
}

/* =========================================================================
 * Public API
 * ====================================================================== */

void opt4001_init_handle(opt4001_t *dev, uint8_t i2c_addr,
                         uint32_t lux_coeff_nano, void *user)
{
    dev->i2c_addr           = i2c_addr;
    dev->lux_milli_per_code = lux_coeff_nano;   /* stored as nano/code */
    dev->user               = user;
}

opt4001_status_t opt4001_probe(opt4001_t *dev)
{
    uint16_t id;
    opt4001_status_t s = read_reg16(dev, OPT4001_REG_DEVICE_ID, &id);
    if (s != OPT4001_OK) return s;
    /* Any non-zero, in-range ID means the chip is answering. Some parts
     * report a specific value; we accept a valid response over the masked
     * field rather than hard-coding a single ID that varies by variant. */
    if ((id & OPT4001_DEVICE_ID_MASK) == 0) return OPT4001_ERR_ID;
    return OPT4001_OK;
}

opt4001_status_t opt4001_configure(opt4001_t *dev, uint8_t convtime_idx)
{
    /* Continuous mode + automatic full-scale range + chosen conversion time. */
    uint16_t ctrl = 0;
    ctrl |= (uint16_t)(OPT4001_CTRL_RANGE_AUTO   << OPT4001_CTRL_RANGE_SHIFT);
    ctrl |= (uint16_t)((convtime_idx & 0x0Fu)    << OPT4001_CTRL_CONVTIME_SHIFT);
    ctrl |= (uint16_t)(OPT4001_OPMODE_CONTINUOUS << OPT4001_CTRL_OPMODE_SHIFT);
    return write_reg16(dev, OPT4001_REG_CTRL, ctrl);
}

opt4001_status_t opt4001_read(opt4001_t *dev, opt4001_result_t *out)
{
    uint16_t msb, lsb;
    opt4001_status_t s;

    if ((s = read_reg16(dev, OPT4001_REG_RESULT_MSB, &msb)) != OPT4001_OK) return s;
    if ((s = read_reg16(dev, OPT4001_REG_RESULT_LSB, &lsb)) != OPT4001_OK) return s;

    /* Decode fields. */
    uint8_t  exponent = (uint8_t)((msb >> OPT4001_EXPONENT_SHIFT) & OPT4001_EXPONENT_MASK);
    uint32_t mant_hi  = (uint32_t)(msb & OPT4001_MSB_MANT_MASK);          /* 12 bits */
    uint32_t mant_lo  = (uint32_t)(lsb >> OPT4001_LSB_MANT_SHIFT);        /* 8 bits  */
    uint32_t mantissa = (mant_hi << 8) | mant_lo;                        /* 20 bits */
    uint8_t  counter  = (uint8_t)((lsb >> OPT4001_COUNTER_SHIFT) & OPT4001_COUNTER_MASK);
    uint8_t  crc_rx   = (uint8_t)(lsb & OPT4001_CRC_MASK);

    /* Validate CRC. */
    uint8_t crc_calc = opt4001_calc_crc(mantissa, exponent, counter);
    out->crc_ok = (crc_calc == crc_rx);

    /* Linearize: adc_codes = mantissa << exponent  (up to 28 bits). */
    uint32_t adc_codes = mantissa << exponent;

    /* lux = adc_codes * (nano_per_code) / 1e9, 64-bit to avoid overflow. */
    uint64_t lux64 = ((uint64_t)adc_codes * (uint64_t)dev->lux_milli_per_code);
    /* nano_per_code is coeff*1e6 (e.g. 437 ~= 437.5e-6 *1e6). Divide by 1e6. */
    lux64 /= 1000000ull;

    out->exponent  = exponent;
    out->mantissa  = mantissa;
    out->counter   = counter;
    out->adc_codes = adc_codes;
    out->lux       = (uint32_t)lux64;

    return out->crc_ok ? OPT4001_OK : OPT4001_ERR_CRC;
}

/* =========================================================================
 * EXAMPLE I2C HAL IMPLEMENTATION (STM32 HAL) — commented out.
 * Copy into your project and adapt. `user` carries your I2C_HandleTypeDef*.
 * The OPT4001 wants an 8-bit register pointer then 16-bit big-endian data,
 * which maps directly onto HAL_I2C_Mem_Read/Write with an 8-bit mem address.
 * ====================================================================== */
#if 0
#include "stm32u5xx_hal.h"

int opt4001_i2c_read(void *user, uint8_t addr7, uint8_t reg,
                     uint8_t *data, uint16_t len)
{
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)user;
    /* HAL wants the 8-bit address (7-bit << 1). */
    if (HAL_I2C_Mem_Read(hi2c, (uint16_t)(addr7 << 1), reg,
                         I2C_MEMADD_SIZE_8BIT, data, len, 100) != HAL_OK)
        return -1;
    return 0;
}

int opt4001_i2c_write(void *user, uint8_t addr7, uint8_t reg,
                      const uint8_t *data, uint16_t len)
{
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)user;
    if (HAL_I2C_Mem_Write(hi2c, (uint16_t)(addr7 << 1), reg,
                          I2C_MEMADD_SIZE_8BIT, (uint8_t *)data, len, 100) != HAL_OK)
        return -1;
    return 0;
}
#endif
