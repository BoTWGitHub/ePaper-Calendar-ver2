#ifndef WIFI_STORAGE_H
#define WIFI_STORAGE_H

#include "esp_err.h"
#include <stddef.h>

#define NVS_NAMESPACE "storage"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"

esp_err_t save_wifi_credentials(const char* ssid, const char* password);
esp_err_t read_wifi_credentials(char* ssid, size_t ssid_len, char* password, size_t pass_len);

#endif // WIFI_STORAGE_H