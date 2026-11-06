idf_component_register(SRCS "main.c" "display.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_spi
                             esp_lcd_co5300 esp_lcd_touch_cst816s esp_lvgl_port lvgl)
