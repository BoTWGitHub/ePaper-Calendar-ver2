#include <stdio.h>
#include "device.h"
#include "epd_7in5_v2.h"
#include "esp_log.h"

static const char *TAG = "main";
extern uint8_t photo[];
void app_main(void)
{
    ESP_LOGI(TAG, "Hello, please wait 3 seconds");
    DELAY_MS(3000);
    device_init();
    epd_7in5_v2_init();
    epd_7in5_v2_clear();
    DELAY_MS(500);
    ESP_LOGI(TAG, "Showing image...");
    epd_7in5_v2_display(photo);
    DELAY_MS(3000);
    epd_7in5_v2_init();
    epd_7in5_v2_clear();
    ESP_LOGI(TAG, "Display go to sleep");
    epd_7in5_v2_sleep();

    while (1) {
        ESP_LOGI(TAG, "Hello, ESP32-S3 ePaper! %d", GPIO_GET_LEVEL(PIN_NUM_BUSY));

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
