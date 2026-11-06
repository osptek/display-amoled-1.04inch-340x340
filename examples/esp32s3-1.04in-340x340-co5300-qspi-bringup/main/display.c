#include "display.h"
#include "board_pins.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lv_demos.h"

static const char *TAG = "display";

/* QSPI 像素写入要求窗口起止为偶数，否则边缘会留下一条线。 */
static void lcd_area_rounder(lv_area_t *area)
{
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;

    if (area->x2 > LCD_H_RES - 1) {
        area->x2 = LCD_H_RES - 1;
    }
    if (area->y2 > LCD_V_RES - 1) {
        area->y2 = LCD_V_RES - 1;
    }
}

/* BOE 1.04" 340x340 CO5300 QSPI 简码，2024.10.21。窗口 0x0000..0x0153。 */
static const co5300_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xC4, (uint8_t []){0x80}, 1, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0x35, (uint8_t []){0x00}, 1, 0},
    {0x53, (uint8_t []){0x20}, 1, 0},
    {0x51, (uint8_t []){0xFF}, 1, 0},
    {0x63, (uint8_t []){0xFF}, 1, 0},
    {0x2A, (uint8_t []){0x00, 0x00, 0x01, 0x53}, 4, 0},
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0x53}, 4, 0},
    {0x11, NULL, 0, 60},
    {0x29, NULL, 0, 0},
};

static void panel_power_on(void)
{
    const gpio_config_t pwr = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_LCD_VCL_EN,
    };
    ESP_ERROR_CHECK(gpio_config(&pwr));
    gpio_set_level(PIN_LCD_VCL_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LCD_VCL_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    const gpio_config_t te = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << PIN_LCD_TE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&te));
}

static esp_err_t lcd_init(esp_lcd_panel_handle_t *panel, esp_lcd_panel_io_handle_t *io)
{
    ESP_LOGI(TAG, "init QSPI CO5300 %dx%d", LCD_H_RES, LCD_V_RES);

    const spi_bus_config_t buscfg = CO5300_PANEL_BUS_QSPI_CONFIG(PIN_LCD_CLK,
                                                                 PIN_LCD_D0,
                                                                 PIN_LCD_D1,
                                                                 PIN_LCD_D2,
                                                                 PIN_LCD_D3,
                                                                 LCD_H_RES * LCD_DRAW_BUFF_LINES * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    io_config.pclk_hz = LCD_PCLK_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, io),
                        TAG, "panel io");

    const co5300_vendor_config_t vendor_config = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(*io, &panel_config, panel), TAG, "new panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(*panel, LCD_X_GAP, LCD_Y_GAP), TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*panel, true), TAG, "disp on");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_co5300_set_brightness(*panel, 100), TAG, "brightness");
    return ESP_OK;
}

static esp_err_t touch_init(esp_lcd_touch_handle_t *tp)
{
    ESP_LOGI(TAG, "init CST820 on I2C (CST816S compatible, addr 0x15)");

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TP_I2C_PORT,
        .sda_io_num = PIN_TP_SDA,
        .scl_io_num = PIN_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "i2c bus");

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "touch io");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = PIN_TP_RST,
        .int_gpio_num = PIN_TP_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
    };
    return esp_lcd_touch_new_i2c_cst816s(io, &tp_cfg, tp);
}

esp_err_t display_lvgl_start(void)
{
    panel_power_on();

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(lcd_init(&panel, &io), TAG, "lcd");

    esp_lcd_touch_handle_t tp = NULL;
    esp_err_t tp_err = touch_init(&tp);
    if (tp_err != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed: %s", esp_err_to_name(tp_err));
        tp = NULL;
    }

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 8192;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = LCD_H_RES * LCD_DRAW_BUFF_LINES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565_SWAPPED,
        .rounder_cb = lcd_area_rounder,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "add display");

    if (tp) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = disp,
            .handle = tp,
        };
        lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);
        ESP_RETURN_ON_FALSE(indev, ESP_FAIL, TAG, "add touch");
    }

    if (lvgl_port_lock(0)) {
        lv_demo_widgets();
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "LVGL running");
    return ESP_OK;
}
