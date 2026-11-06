#include "esp_log.h"
#include "display.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "start CO5300 + CST820 + LVGL");
    ESP_ERROR_CHECK(display_lvgl_start());
}
