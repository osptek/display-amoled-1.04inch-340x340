#pragma once

#include "esp_err.h"

/* 打开 VCL、初始化 CO5300 / CST820，并启动 LVGL。 */
esp_err_t display_lvgl_start(void);
