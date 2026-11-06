#pragma once

#include "driver/gpio.h"
#include "hal/spi_types.h"
#include "driver/i2c_types.h"

#define LCD_H_RES           340
#define LCD_V_RES           340

/* 画面相对 GRAM 的偏移。若显示内容整体平移，改这两个值。 */
#define LCD_X_GAP           0
#define LCD_Y_GAP           0

/* 一次绘制的行数，必须是偶数。双缓冲约 109 KB，放在内部 RAM。 */
#define LCD_DRAW_BUFF_LINES 80

#define PIN_TP_SCL          GPIO_NUM_42
#define PIN_TP_SDA          GPIO_NUM_41
#define PIN_TP_RST          GPIO_NUM_40
#define PIN_TP_INT          GPIO_NUM_39
#define PIN_LCD_VCL_EN      GPIO_NUM_38

#define PIN_LCD_CLK         GPIO_NUM_9
#define PIN_LCD_D0          GPIO_NUM_10
#define PIN_LCD_D1          GPIO_NUM_11
#define PIN_LCD_D2          GPIO_NUM_12
#define PIN_LCD_D3          GPIO_NUM_13
#define PIN_LCD_CS          GPIO_NUM_14
#define PIN_LCD_RST         GPIO_NUM_15
#define PIN_LCD_TE          GPIO_NUM_16

#define LCD_SPI_HOST        SPI2_HOST
#define TP_I2C_PORT         I2C_NUM_0
#define LCD_PCLK_HZ         (80 * 1000 * 1000)
