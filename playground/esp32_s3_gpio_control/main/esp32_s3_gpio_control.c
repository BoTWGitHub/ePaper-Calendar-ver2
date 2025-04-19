/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define GPIO_PIN_NUM 4
#define CONFIG_BLINK_PERIOD 2000

static uint8_t s_led_state = 0;

void app_main(void)
{
    gpio_reset_pin(GPIO_PIN_NUM);
    gpio_set_direction(GPIO_PIN_NUM, GPIO_MODE_OUTPUT);

    while (1) {
        gpio_set_level(GPIO_PIN_NUM, s_led_state);
        s_led_state = !s_led_state;
        vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
    }
}
