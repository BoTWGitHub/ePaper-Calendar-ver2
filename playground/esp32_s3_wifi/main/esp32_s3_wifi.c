// main/main.c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "wifi_manager.h" // Include the Wi-Fi manager header

static const char *TAG = "APP_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Application");

    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize Wi-Fi Manager
    ESP_ERROR_CHECK(wifi_manager_init());

    // 3. Start Wi-Fi Connection Process
    ESP_ERROR_CHECK(wifi_manager_start());

    ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");

    // 4. Wait for connection (using event group or blocking call)
    // Using Event Group (allows checking other things)
    EventGroupHandle_t wifi_event_group = wifi_manager_get_event_group();
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_DISCONNECTED_BIT,
                                           pdFALSE, // Don't clear
                                           pdFALSE, // Wait for either
                                           portMAX_DELAY); // Wait forever

    esp_err_t wifi_status;
    if (bits & WIFI_MANAGER_CONNECTED_BIT) {
         ESP_LOGI(TAG, "Wi-Fi Connected!");
         wifi_status = ESP_OK;
    } else if (bits & WIFI_MANAGER_DISCONNECTED_BIT) {
         ESP_LOGE(TAG, "Wi-Fi connection failed permanently.");
          wifi_status = ESP_FAIL;
    } else {
          ESP_LOGE(TAG, "Unexpected state waiting for Wi-Fi connection.");
          wifi_status = ESP_FAIL; // Or some other error
    }

    // 5. Proceed based on connection status
    if (wifi_status == ESP_OK) {
        ESP_LOGI(TAG, "Connected successfully. Starting main application tasks.");

        // --- Your Main Application Loop ---
        while (1) {
            // Check Wi-Fi state periodically if needed
            if (wifi_manager_get_state() == WIFI_STATE_CONNECTED) {
                ESP_LOGI(TAG, "Main loop: Fetching ICS data...");
                // Add your ICS fetching, e-paper update, and sleep logic here
            } else {
                 ESP_LOGW(TAG, "Main loop: Wi-Fi disconnected. Waiting for reconnect...");
                 // Maybe wait for connection bit again or handle error
                 // For now, just delay
            }
            vTaskDelay(pdMS_TO_TICKS(60 * 1000)); // Example: Run every minute
        }
        // --- End Main Loop ---

    } else {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi after all attempts.");
        // Handle persistent failure: Deep sleep? Display error? Restart?
        ESP_LOGE(TAG, "Halting application.");
         vTaskDelay(portMAX_DELAY); // Halt
    }
}