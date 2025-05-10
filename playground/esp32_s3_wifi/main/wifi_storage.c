
#include "wifi_storage.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "WIFI_STORAGE";

esp_err_t save_wifi_credentials(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_WIFI_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) writing SSID to NVS!", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    err = nvs_set_str(nvs_handle, NVS_KEY_WIFI_PASS, password);

     if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Wi-Fi credentials saved to NVS.");
        } else {
            ESP_LOGE(TAG, "Error (%s) committing NVS!", esp_err_to_name(err));
        }
    } else {
         ESP_LOGE(TAG, "Error (%s) writing Password to NVS!", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t read_wifi_credentials(char* ssid, size_t ssid_len, char* password, size_t pass_len) {
    nvs_handle_t nvs_handle;
    memset(ssid, 0, ssid_len);
    memset(password, 0, pass_len);

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_NVS_NOT_INITIALIZED) {
            ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        }
        return err;
    }

    size_t required_ssid_len = ssid_len;
    err = nvs_get_str(nvs_handle, NVS_KEY_WIFI_SSID, ssid, &required_ssid_len);
    if (err == ESP_OK) {
        size_t required_pass_len = pass_len;
        esp_err_t pass_err = nvs_get_str(nvs_handle, NVS_KEY_WIFI_PASS, password, &required_pass_len);
        if (pass_err != ESP_OK && pass_err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Error (%s) reading password from NVS!", esp_err_to_name(pass_err));
            err = pass_err;
        } else if (pass_err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "Password not found in NVS (might be an open network).");
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Error (%s) reading SSID from NVS!", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return (err == ESP_OK && strlen(ssid) > 0) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
