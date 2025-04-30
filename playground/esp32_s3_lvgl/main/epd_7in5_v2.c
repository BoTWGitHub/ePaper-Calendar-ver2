#include "epd_7in5_v2.h"
#include "device.h"
#include "esp_log.h"

static const char *TAG = "epd_7in5_v2";

static void epd_power_on(void)
{
    ESP_LOGI(TAG, "e-Paper power on");
    GPIO_SET_LEVEL(PIN_NUM_PWR, 1);
    DELAY_MS(100);
}

static void epd_reset(void)
{
    ESP_LOGI(TAG, "e-Paper reset");
    GPIO_SET_LEVEL(PIN_NUM_N_RST, 1);
    DELAY_MS(200);
    GPIO_SET_LEVEL(PIN_NUM_N_RST, 0);
    DELAY_MS(200);
    GPIO_SET_LEVEL(PIN_NUM_N_RST, 1);
    DELAY_MS(200);
}

static void epd_wait_until_idle(void)
{
    ESP_LOGI(TAG, "e-Paper busy");
    do{
        epd_cmd(0x71);
        DELAY_MS(5);
    } while (GPIO_GET_LEVEL(PIN_NUM_BUSY) == 0);
    DELAY_MS(20);
    ESP_LOGI(TAG, "e-Paper busy release");
}

static void epd_7in5_v2_trun_on_display(void)
{
    ESP_LOGI(TAG, "e-Paper turn on display");
    epd_cmd(0x12);
    DELAY_MS(100);
    epd_wait_until_idle();
}

void epd_7in5_v2_init(void)
{
    epd_power_on();
    epd_reset();

    epd_cmd(0x06);
    epd_data(0x17);
    epd_data(0x17);
    epd_data(0x28);
    epd_data(0x17);

    epd_cmd(0x01);
    epd_data(0x07);
    epd_data(0x07);
    epd_data(0x3F);
    epd_data(0x3F);

    epd_cmd(0x04);
    DELAY_MS(100);
    epd_wait_until_idle();

    epd_cmd(0x00);
    epd_data(0x1F);

    epd_cmd(0x61);
    epd_data(0x03);
    epd_data(0x20);
    epd_data(0x01);
    epd_data(0xE0);

    epd_cmd(0x15);
    epd_data(0x00);

    epd_cmd(0x50);
    epd_data(0x10);
    epd_data(0x07);

    epd_cmd(0x60);
    epd_data(0x22);
}

void epd_7in5_v2_clear(void)
{
    int width = EPD_7IN5_V2_WIDTH / 8;
    uint8_t image[EPD_7IN5_V2_WIDTH / 8] = {0x00};
    epd_cmd(0x10);
    for(int i = 0; i < width; i++) {
        image[i] = 0xFF;
    }
    for(int i = 0; i < EPD_7IN5_V2_HEIGHT; i++) {
        epd_data2(image, width);
    }

    epd_cmd(0x13);
    for(int i = 0; i < width; i++) {
        image[i] = 0x00;
    }
    for(int i = 0; i < EPD_7IN5_V2_HEIGHT; i++) {
        epd_data2(image, width);
    }

    epd_7in5_v2_trun_on_display();
}

void epd_7in5_v2_display(uint8_t *image)
{
    int width = EPD_7IN5_V2_WIDTH / 8;
    epd_cmd(0x10);
    for (int j = 0; j < EPD_7IN5_V2_HEIGHT; j++) {
        epd_data2((uint8_t *)(image + j * width), width);
    }

    epd_cmd(0x13);
    for (int j = 0; j < EPD_7IN5_V2_HEIGHT; j++) {
        for (int i = 0; i < width; i++) {
            image[i + j * width] = ~image[i + j * width];
        }
    }
    for (int j = 0; j < EPD_7IN5_V2_HEIGHT; j++) {
        epd_data2((uint8_t *)(image + j * width), width);
    }
    epd_7in5_v2_trun_on_display();
}

void epd_7in5_v2_sleep(void)
{
    epd_cmd(0x50);
    epd_data(0xF7);
    epd_cmd(0x02);
    epd_wait_until_idle();
    epd_cmd(0x07);
    epd_data(0xA5);
}
