// main/wifi_manager.c
#include "wifi_manager.h"
#include "wifi_storage.h"
#include "softap_provisioning.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <string.h>

#define WIFI_MANAGER_MAX_RETRY 5 // Max connection retries before giving up/provisioning

static const char *TAG = "WIFI_MANAGER";

// --- Module State ---
static wifi_manager_state_t s_current_state = WIFI_STATE_UNINITIALIZED;
static EventGroupHandle_t s_wifi_event_group = NULL; // Event group for signaling main app
static int s_retry_num = 0;
static esp_netif_t *s_sta_netif = NULL;
// static esp_netif_t *s_ap_netif = NULL; // Keep AP netif handle if needed globally
static esp_event_handler_instance_t instance_any_id; // For wifi events
static esp_event_handler_instance_t instance_got_ip; // For IP events
static bool provisioning_triggered_manually = false; // Flag for manual trigger


// --- Forward Declarations ---
static void wifi_manager_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t wifi_manager_connect_sta(const char* ssid, const char* password);
static void wifi_manager_do_provisioning(void);


// --- Event Handler ---
static void wifi_manager_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
                // Connection attempt is usually called after this
                // esp_wifi_connect(); // connect_sta calls this
                s_current_state = WIFI_STATE_CONNECTING;
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
                s_retry_num = 0; // Reset retry counter on successful connection event
                 // State changes to CONNECTED only after getting IP
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED");
                s_current_state = WIFI_STATE_DISCONNECTED;
                if (s_wifi_event_group) {
                    xEventGroupClearBits(s_wifi_event_group, WIFI_MANAGER_CONNECTED_BIT); // Clear connected bit
                }

                // Check if disconnection was intentional (e.g., before provisioning)
                if (!provisioning_triggered_manually) {
                     if (s_retry_num < WIFI_MANAGER_MAX_RETRY) {
                         s_retry_num++;
                         ESP_LOGI(TAG, "Retry to connect to the AP (%d/%d)", s_retry_num, WIFI_MANAGER_MAX_RETRY);
                          vTaskDelay(pdMS_TO_TICKS(2000)); // Wait before retry
                         esp_wifi_connect();
                         s_current_state = WIFI_STATE_CONNECTING;
                    } else {
                        ESP_LOGE(TAG, "Connection failed after %d retries.", WIFI_MANAGER_MAX_RETRY);
                        if (s_wifi_event_group) {
                            xEventGroupSetBits(s_wifi_event_group, WIFI_MANAGER_DISCONNECTED_BIT); // Signal persistent failure
                        }
                        // Decide whether to auto-trigger provisioning here
                        // wifi_manager_do_provisioning();
                    }
                } else {
                    ESP_LOGI(TAG, "Disconnection seems intentional (manual provisioning trigger).");
                     // Reset flag after handling intentional disconnect
                     provisioning_triggered_manually = false;
                }

                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_current_state = WIFI_STATE_CONNECTED;
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_MANAGER_CONNECTED_BIT);
             xEventGroupClearBits(s_wifi_event_group, WIFI_MANAGER_DISCONNECTED_BIT);
        }
    }
}

// --- Internal STA Connection Function ---
static esp_err_t wifi_manager_connect_sta(const char* ssid, const char* password) {
    ESP_LOGI(TAG, "Attempting to connect to SSID: [%s]", ssid);

     if (s_current_state == WIFI_STATE_UNINITIALIZED || s_sta_netif == NULL) {
         ESP_LOGE(TAG, "Wi-Fi Manager not initialized properly.");
         return ESP_FAIL;
     }

    // Ensure Wi-Fi is initialized
    esp_err_t init_err = esp_wifi_init(& (wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT());
     if (init_err != ESP_OK && init_err != ESP_ERR_WIFI_INIT_STATE) {
         ESP_LOGE(TAG, "esp_wifi_init failed (%s)", esp_err_to_name(init_err));
         return init_err;
     }


    // Set mode and config
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t wifi_config = { 0 };
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = strlen(password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Start and Connect
    esp_err_t start_err = esp_wifi_start();
     if (start_err != ESP_OK) {
         ESP_LOGE(TAG, "esp_wifi_start failed (%s)", esp_err_to_name(start_err));
         return start_err;
     }

    esp_err_t connect_err = esp_wifi_connect();
    if (connect_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed (%s)", esp_err_to_name(connect_err));
        // Don't return error immediately, let event handler manage retries
    } else {
        s_current_state = WIFI_STATE_CONNECTING;
         ESP_LOGI(TAG, "esp_wifi_connect() called.");
    }
     // Reset retry counter when initiating a new connection attempt
     s_retry_num = 0;
     // Clear event bits related to connection state
     if (s_wifi_event_group) {
         xEventGroupClearBits(s_wifi_event_group, WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_DISCONNECTED_BIT);
     }

    return ESP_OK; // Indicate connection attempt was initiated
}


// --- Internal Provisioning Trigger ---
static void wifi_manager_do_provisioning(void) {

     if (s_current_state == WIFI_STATE_PROVISIONING) {
          ESP_LOGW(TAG, "Provisioning already in progress.");
          return;
     }

     // Stop current connection attempts or disconnect if connected
     if (s_current_state == WIFI_STATE_CONNECTING || s_current_state == WIFI_STATE_CONNECTED) {
          ESP_LOGI(TAG, "Stopping current Wi-Fi connection for provisioning...");
          provisioning_triggered_manually = true; // Prevent auto-retry on disconnect
          ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
          // Wait a moment for disconnect event? Or proceed assuming cleanup happens.
          vTaskDelay(pdMS_TO_TICKS(500)); // Give some time for disconnect
          // Do not stop wifi here, start_softap_provisioning manages its wifi lifecycle
     }

     s_current_state = WIFI_STATE_PROVISIONING;
     ESP_LOGI(TAG, "Starting SoftAP provisioning...");

     // Call the provisioning module function
     // Use a reasonable timeout, e.g., 5 minutes (or make it configurable)
     TickType_t prov_timeout = pdMS_TO_TICKS(5 * 60 * 1000);
     esp_err_t prov_status = start_softap_provisioning(prov_timeout);

      if (prov_status == ESP_OK) {
            ESP_LOGI(TAG, "Provisioning successful. Attempting connection with new credentials...");
             // Read new credentials and attempt connection again
            char ssid[33] = {0};
            char password[65] = {0};
            if (read_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password)) == ESP_OK) {
                 wifi_manager_connect_sta(ssid, password);
            } else {
                 ESP_LOGE(TAG, "Failed to read credentials after successful provisioning!");
                  s_current_state = WIFI_STATE_ERROR; // Or DISCONNECTED?
                 // Signal failure?
            }

      } else if (prov_status == ESP_ERR_TIMEOUT) {
          ESP_LOGW(TAG, "Provisioning timed out.");
           s_current_state = WIFI_STATE_DISCONNECTED; // Revert state
      } else {
           ESP_LOGE(TAG, "Provisioning failed.");
            s_current_state = WIFI_STATE_DISCONNECTED; // Revert state
      }
       // Signal that provisioning attempt finished
       if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_MANAGER_PROV_DONE_BIT);
       }
      provisioning_triggered_manually = false; // Reset flag
}

// --- Public Functions ---

esp_err_t wifi_manager_init(void) {
    if (s_current_state != WIFI_STATE_UNINITIALIZED) {
        ESP_LOGW(TAG, "Wi-Fi Manager already initialized.");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Initializing Wi-Fi Manager...");

    // 1. Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // 2. Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. Create default Wi-Fi network interfaces
    s_sta_netif = esp_netif_create_default_wifi_sta();
    // s_ap_netif = esp_netif_create_default_wifi_ap(); // Create AP only if needed later
    if (s_sta_netif == NULL /*|| s_ap_netif == NULL*/) {
        ESP_LOGE(TAG, "Failed to create default Wi-Fi netifs");
        return ESP_FAIL;
    }

    // 4. Initialize Wi-Fi stack
     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
     ESP_ERROR_CHECK(esp_wifi_init(&cfg));


    // 5. Register event handlers
     ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_manager_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_manager_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // 6. Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi event group");
        // Cleanup previous steps?
        return ESP_FAIL;
    }

    s_current_state = WIFI_STATE_INITIALIZED;
    ESP_LOGI(TAG, "Wi-Fi Manager Initialized.");
    return ESP_OK;
}

esp_err_t wifi_manager_start(void) {
    if (s_current_state == WIFI_STATE_UNINITIALIZED) {
        ESP_LOGE(TAG, "Wi-Fi Manager not initialized. Call wifi_manager_init() first.");
        return ESP_FAIL;
    }
     if (s_current_state == WIFI_STATE_CONNECTING || s_current_state == WIFI_STATE_CONNECTED || s_current_state == WIFI_STATE_PROVISIONING) {
          ESP_LOGW(TAG,"Wi-Fi Manager already started or in progress.");
          return ESP_OK;
     }

    ESP_LOGI(TAG, "Starting Wi-Fi connection process...");
    char ssid[33] = {0};
    char password[65] = {0};

    esp_err_t read_status = read_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password));

    if (read_status == ESP_OK) {
        ESP_LOGI(TAG, "Stored credentials found. Attempting connection...");
        return wifi_manager_connect_sta(ssid, password); // Initiate connection
    } else if (read_status == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No credentials found in NVS. Starting provisioning...");
        wifi_manager_do_provisioning(); // Start provisioning
        return ESP_OK; // Indicate process started
    } else {
        ESP_LOGE(TAG, "Error reading NVS (%s). Cannot start Wi-Fi.", esp_err_to_name(read_status));
        s_current_state = WIFI_STATE_ERROR;
        return ESP_FAIL;
    }
}

wifi_manager_state_t wifi_manager_get_state(void) {
    return s_current_state;
}

esp_err_t wifi_manager_wait_for_ip(TickType_t timeout_ticks) {
    if (s_wifi_event_group == NULL) return ESP_FAIL;

    ESP_LOGD(TAG, "Waiting for IP connection (timeout: %lu ticks)...", (unsigned long)timeout_ticks);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_DISCONNECTED_BIT,
                                           pdFALSE, // Don't clear bits
                                           pdFALSE, // Wait for either bit
                                           timeout_ticks);

    if (bits & WIFI_MANAGER_CONNECTED_BIT) {
        ESP_LOGD(TAG, "Connected bit set.");
        return ESP_OK;
    } else if (bits & WIFI_MANAGER_DISCONNECTED_BIT) {
         ESP_LOGW(TAG, "Disconnected bit set while waiting for IP.");
         return ESP_FAIL; // Indicate persistent failure
    } else {
        ESP_LOGW(TAG, "Timeout waiting for IP connection.");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t wifi_manager_trigger_provisioning(void) {
     if (s_current_state == WIFI_STATE_UNINITIALIZED) {
         ESP_LOGE(TAG, "Wi-Fi Manager not initialized.");
         return ESP_FAIL;
     }
      if (s_current_state == WIFI_STATE_PROVISIONING) {
          ESP_LOGW(TAG, "Provisioning already in progress.");
          return ESP_FAIL; // Or ESP_OK if idempotent?
     }

     ESP_LOGI(TAG, "Manual provisioning triggered.");
     wifi_manager_do_provisioning();
     return ESP_OK;
}

EventGroupHandle_t wifi_manager_get_event_group(void) {
     return s_wifi_event_group;
}