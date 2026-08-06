#include "fatfs.h"
#include <string.h>

extern SPI_HandleTypeDef hspi1;

void sd_demo(void)
{
    FATFS   fs;
    FIL     fil;
    FRESULT fr;
    UINT    bw, br;
    char    wbuf[] = "hello sd from stm32u5\r\n";
    char    rbuf[64];

    // 1. Mount (SPIDISKPath is defined by CubeMX, usually "0:/")
    fr = f_mount(&fs, SPIDISKPath, 1);
    if (fr != FR_OK) { Error_Handler(); }

    // 2. Write a file
    fr = f_open(&fil, "test.txt", FA_CREATE_ALWAYS | FA_WRITE);
    if (fr == FR_OK) {
        f_write(&fil, wbuf, strlen(wbuf), &bw);
        f_close(&fil);
    }

    // 3. Read it back
    fr = f_open(&fil, "test.txt", FA_READ);
    if (fr == FR_OK) {
        f_read(&fil, rbuf, sizeof(rbuf) - 1, &br);
        rbuf[br] = '\0';
        f_close(&fil);
        // rbuf now holds the file contents
    }

    // 4. Append
    fr = f_open(&fil, "test.txt", FA_OPEN_APPEND | FA_WRITE);
    if (fr == FR_OK) {
        f_write(&fil, "line 2\r\n", 8, &bw);
        f_close(&fil);
    }

    f_mount(NULL, SPIDISKPath, 0);  // unmount
}

// In user_diskio_spi.c — set slow (<400kHz) for card init, fast after
static void SPI_SlowMode(void) {
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    HAL_SPI_Init(&hspi1);
}
static void SPI_FastMode(void) {
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8; // tune to <=25MHz
    HAL_SPI_Init(&hspi1);
}
