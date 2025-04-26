#include <stdio.h>
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define EPD_HOST    SPI2_HOST

#define PIN_NUM_MOSI 13
#define PIN_NUM_CLK  12
#define PIN_NUM_CS   10
#define PIN_NUM_DC   9
#define PIN_NUM_N_RST  46
#define PIN_NUM_BUSY 3
#define PIN_NUM_PWR  8

void epd_cmd(spi_device_handle_t* spi, const uint8_t cmd)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length = 8;                   //Command is 8 bits
    t.tx_buffer = &cmd;             //The data is the cmd itself
    t.user = (void*)0;              //D/C needs to be set to 0
    ret = spi_device_polling_transmit(*spi, &t); //Transmit!
    assert(ret == ESP_OK);          //Should have had no issues.
}

void epd_data(spi_device_handle_t* spi, const uint8_t *data, int len)
{
    esp_err_t ret;
    spi_transaction_t t;
    if (len == 0) {
        return;    //no need to send anything
    }
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length = len * 8;             //Len is in bytes, transaction length is in bits.
    t.tx_buffer = data;             //Data
    t.user = (void*)1;              //D/C needs to be set to 1
    ret = spi_device_polling_transmit(*spi, &t); //Transmit!
    assert(ret == ESP_OK);          //Should have had no issues.
}

void epd_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc = (int)t->user;
    gpio_set_level(PIN_NUM_DC, dc);
}

void spt_init(spi_device_handle_t* spi)
{
    esp_err_t ret;
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,     //Clock out at 10 MHz
        .mode = 0,                              //SPI mode 0
        .spics_io_num = PIN_NUM_CS,             //CS pin
        .queue_size = 7,                        //We want to be able to queue 7 transactions at a time
        .pre_cb = epd_spi_pre_transfer_callback, //Specify pre-transfer callback to handle D/C line
    };
    //Initialize the SPI bus
    ret = spi_bus_initialize(EPD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
    //Attach the LCD to the SPI bus
    ret = spi_bus_add_device(EPD_HOST, &devcfg, spi);
    ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
    //Initialize non-SPI GPIOs
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = ((1ULL << PIN_NUM_DC) | (1ULL << PIN_NUM_N_RST) | (1ULL << PIN_NUM_PWR));
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = true;
    gpio_config(&io_conf);
    gpio_set_level(PIN_NUM_DC, 0);
    gpio_set_level(PIN_NUM_PWR, 0);
    gpio_set_level(PIN_NUM_N_RST, 1);

    gpio_config_t io_conf_2 = {};
    io_conf_2.pin_bit_mask = (1ULL << PIN_NUM_BUSY);
    io_conf_2.mode = GPIO_MODE_INPUT;
    gpio_config(&io_conf_2);

    spi_device_handle_t* spi = malloc(sizeof(spi_device_handle_t));
    spt_init(spi);

    const uint8_t test_data[] = {0xAA, 0x55, 0x37, 0x62, 0x4C};

    while (1) {
        ESP_LOGI("ePaperDemo", "Hello, ESP32-S3 ePaper!");
        epd_cmd(spi, test_data[0]);
        epd_data(spi, &test_data[1], 4);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
