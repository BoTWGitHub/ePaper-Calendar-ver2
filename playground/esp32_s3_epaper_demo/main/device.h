#ifndef _DEVICE_H_
#define _DEVICE_H_

#include "driver/spi_master.h"
#include "driver/gpio.h"

#define EPD_HOST    SPI2_HOST

#define PIN_NUM_MOSI 11
#define PIN_NUM_CLK  12
#define PIN_NUM_CS   10
#define PIN_NUM_DC   9
#define PIN_NUM_N_RST 8
#define PIN_NUM_BUSY 7
#define PIN_NUM_PWR  3

#define GPIO_SET_LEVEL(_pin, _value) gpio_set_level(_pin, _value)
#define GPIO_GET_LEVEL(_pin) gpio_get_level(_pin)
#define DELAY_MS(__xms) vTaskDelay(__xms / portTICK_PERIOD_MS)

void epd_cmd(const uint8_t cmd);
void epd_data(const uint8_t data);
void epd_data2(const uint8_t *data, int len);
void device_init(void);

#endif