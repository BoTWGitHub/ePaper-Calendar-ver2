// main/wifi_manager.h
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

typedef enum {
    WIFI_STATE_UNINITIALIZED,
    WIFI_STATE_INITIALIZED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_PROVISIONING,
    WIFI_STATE_ERROR
} wifi_manager_state_t;

// Event bits for signaling main application
#define WIFI_MANAGER_CONNECTED_BIT BIT0
#define WIFI_MANAGER_DISCONNECTED_BIT BIT1 // Persistent disconnection after retries
#define WIFI_MANAGER_PROV_DONE_BIT BIT2    // Provisioning process attempted (success or fail)


/**
 * @brief Initializes the Wi-Fi Manager
 * Should be called once at startup after NVS initialization.
 * Initializes Netif, Event Loop, Wi-Fi stack, creates event group.
 *
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Starts the Wi-Fi connection process.
 * Reads credentials from NVS. If found, attempts to connect as STA.
 * If not found, or if connection fails permanently, triggers provisioning.
 * This is non-blocking; use wifi_manager_wait_for_ip or event group to check status.
 *
 * @return esp_err_t ESP_OK if the process started, error code otherwise.
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Gets the current state of the Wi-Fi Manager.
 *
 * @return wifi_manager_state_t The current state.
 */
wifi_manager_state_t wifi_manager_get_state(void);

/**
 * @brief Waits for the Wi-Fi manager to signal a successful IP connection.
 *
 * @param timeout_ticks Timeout duration in FreeRTOS ticks.
 * @return esp_err_t ESP_OK if connected, ESP_ERR_TIMEOUT if timed out.
 */
esp_err_t wifi_manager_wait_for_ip(TickType_t timeout_ticks);

/**
 * @brief Manually triggers the provisioning process.
 * Disconnects if currently connected.
 *
 * @return esp_err_t ESP_OK if provisioning was triggered, error code otherwise.
 */
esp_err_t wifi_manager_trigger_provisioning(void);

/**
 * @brief Get the event group handle used by the Wi-Fi manager.
 * Allows external tasks to wait on specific events.
 *
 * @return EventGroupHandle_t Handle to the event group, or NULL if not initialized.
 */
EventGroupHandle_t wifi_manager_get_event_group(void);


#endif // WIFI_MANAGER_H