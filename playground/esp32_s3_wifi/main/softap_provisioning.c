#include "softap_provisioning.h"
#include "wifi_storage.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <esp_http_server.h>
#include <ctype.h>
#include "esp_mac.h"

static const char *TAG = "SOFTAP_PROV";

// --- SoftAP 設定 (內部使用) ---
#define PROV_ESP_WIFI_AP_SSID      "ESP-Setup"
#define PROV_ESP_WIFI_AP_PASSWORD  "12345678"
#define PROV_ESP_WIFI_AP_CHANNEL   1
#define PROV_ESP_MAX_STA_CONN      1

// --- Provisioning Event Group (內部同步用) ---
static EventGroupHandle_t prov_event_group = NULL;
const int PROV_DONE_BIT = BIT0; // Creds received and saved OK
const int PROV_FAIL_BIT = BIT1; // Internal error during provisioning

// --- HTTP Server Handle (內部使用) ---
static httpd_handle_t server = NULL;
static esp_netif_t *prov_ap_netif = NULL; // Hold the netif pointer for cleanup

// --- Forward declarations for static functions ---
static void url_decode_helper(const char* src, char* dst, size_t dst_size);
static esp_err_t root_get_handler(httpd_req_t *req);
static esp_err_t connect_post_handler(httpd_req_t *req);
static esp_err_t start_webserver(void);
static void stop_webserver(void);
static void softap_prov_ap_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t softap_prov_init_wifi(void);
static void softap_prov_deinit_wifi(void);

// --- HTML Form ---
static const char* html_form =
    "<!DOCTYPE html><html><head><title>ESP32 Wi-Fi Setup</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<style> body { font-family: Arial, sans-serif; } "
    "label, input { display: block; margin-bottom: 10px; width: 90%%; max-width: 300px; } "
    "input[type='text'], input[type='password'] { padding: 8px; } "
    "button { padding: 10px 15px; background-color: #007bff; color: white; border: none; cursor: pointer; } "
    "</style></head><body>"
    "<h1>Wi-Fi Setup</h1>"
    "<form method='post' action='/connect'>"
    "<label for='ssid'>Wi-Fi Network (SSID):</label>"
    "<input type='text' id='ssid' name='ssid' required><br>"
    "<label for='password'>Password:</label>"
    "<input type='password' id='password' name='password'><br>"
    "<button type='submit'>Connect</button>"
    "</form></body></html>";

// --- Static URL Decode Helper ---
static void url_decode_helper(const char* src, char* dst, size_t dst_size) {
    char a, b;
    size_t len = 0;
    while (*src && len < dst_size - 1) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit((unsigned char)a) && isxdigit((unsigned char)b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10); else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10); else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
            len++;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
            len++;
        } else {
            *dst++ = *src++;
            len++;
        }
    }
    *dst = '\0';
}

// --- HTTP Handlers (Static) ---
static esp_err_t root_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Serving provisioning page");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_form, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t connect_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret, remaining = req->content_len;
    memset(buf, 0, sizeof(buf));

    if (remaining >= sizeof(buf)) {
        ESP_LOGE(TAG, "POST Content too long (%d bytes)", remaining);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        if(prov_event_group) xEventGroupSetBits(prov_event_group, PROV_FAIL_BIT); // Signal internal failure
        return ESP_FAIL;
    }
    ret = httpd_req_recv(req, buf, remaining);
     if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) { httpd_resp_send_408(req); }
        ESP_LOGE(TAG, "Error receiving POST data (ret: %d)", ret);
        if(prov_event_group) xEventGroupSetBits(prov_event_group, PROV_FAIL_BIT);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    ESP_LOGD(TAG, "Received raw POST data (%d bytes): [%s]", ret, buf); // Debug log

    char ssid[33] = {0};
    char password[65] = {0};
    char encoded_ssid[129] = {0};
    char encoded_password[193] = {0};

    if (httpd_query_key_value(buf, "ssid", encoded_ssid, sizeof(encoded_ssid)) != ESP_OK || strlen(encoded_ssid) == 0) {
         ESP_LOGE(TAG, "SSID parameter is missing or empty in POST data.");
         httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or empty SSID parameter");
         if(prov_event_group) xEventGroupSetBits(prov_event_group, PROV_FAIL_BIT);
         return ESP_FAIL;
    }
    // Password can be empty, don't check error strictly for it
    httpd_query_key_value(buf, "password", encoded_password, sizeof(encoded_password));

    ESP_LOGD(TAG, "Value from httpd_query_key_value for SSID: [%s]", encoded_ssid);
    ESP_LOGD(TAG, "Value from httpd_query_key_value for Password: [%s]", encoded_password);

    url_decode_helper(encoded_ssid, ssid, sizeof(ssid));
    url_decode_helper(encoded_password, password, sizeof(password));

    ESP_LOGI(TAG, "Decoded SSID: [%s]", ssid);
    // ESP_LOGI(TAG, "Decoded Password: [%s]", password); // Avoid logging password

     if (strlen(ssid) == 0) { // Should not happen if parsing was OK, but double check
         ESP_LOGE(TAG, "Decoded SSID is empty.");
         httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to decode SSID");
         if(prov_event_group) xEventGroupSetBits(prov_event_group, PROV_FAIL_BIT);
         return ESP_FAIL;
     }

    // --- Save Credentials using wifi_storage ---
    esp_err_t save_err = save_wifi_credentials(ssid, password);
    if (save_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Wi-Fi credentials (%s)", esp_err_to_name(save_err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save credentials");
        if(prov_event_group) xEventGroupSetBits(prov_event_group, PROV_FAIL_BIT);
        return ESP_FAIL;
    }

    // --- Respond and Signal Success ---
    const char* resp_str = "Credentials received. Setup will continue.";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(100)); // Short delay for response
    if(prov_event_group) xEventGroupSetBits(prov_event_group, PROV_DONE_BIT); // Signal success

    return ESP_OK;
}

static const httpd_uri_t uri_get = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = root_get_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_post = {
    .uri      = "/connect",
    .method   = HTTP_POST,
    .handler  = connect_post_handler,
    .user_ctx = NULL
};

// --- Web Server Start/Stop (Static) ---
static esp_err_t start_webserver(void) {
    if (server == NULL) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true;
        ESP_LOGI(TAG, "Starting HTTP server...");
        if (httpd_start(&server, &config) == ESP_OK) {
            httpd_register_uri_handler(server, &uri_get);
            httpd_register_uri_handler(server, &uri_post);
            ESP_LOGI(TAG, "HTTP server started.");
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Error starting HTTP server!");
            return ESP_FAIL;
        }
    }
    ESP_LOGW(TAG, "Webserver already started.");
    
    return ESP_OK;
}

static void stop_webserver(void) {
    if (server) {
        httpd_stop(server);
        ESP_LOGI(TAG, "HTTP server stopped.");
        server = NULL;
    }
}

// --- SoftAP Event Handler (Static) ---
static void softap_prov_ap_event_handler(void* arg, esp_event_base_t event_base,
                                         int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined SoftAP", MAC2STR(event->mac));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left SoftAP", MAC2STR(event->mac));
    }
}

// --- SoftAP Wi-Fi Init/Deinit (Static) ---
static esp_err_t softap_prov_init_wifi(void) {
    ESP_LOGI(TAG, "Initializing Wi-Fi for SoftAP provisioning...");

    // Create AP netif instance specifically for provisioning
    prov_ap_netif = esp_netif_create_default_wifi_ap();
    if (prov_ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create SoftAP netif");
        return ESP_FAIL;
    }

    // Use default Wi-Fi init config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_INIT_STATE) { // Allow re-initialization
        ESP_LOGE(TAG, "esp_wifi_init failed (%s)", esp_err_to_name(ret));
        esp_netif_destroy(prov_ap_netif); // Cleanup netif
        prov_ap_netif = NULL;
        return ret;
    }
    // Must unregister handlers before re-registering or check instance
    // Simpler: Assume handlers are managed correctly by calling context (wifi_manager)
    // or ensure wifi is stopped before calling this init.
    // We will register handlers specific to this provisioning session.
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &softap_prov_ap_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &softap_prov_ap_event_handler, NULL));


    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = PROV_ESP_WIFI_AP_SSID,
            .ssid_len = strlen(PROV_ESP_WIFI_AP_SSID),
            .channel = PROV_ESP_WIFI_AP_CHANNEL,
            .password = PROV_ESP_WIFI_AP_PASSWORD,
            .max_connection = PROV_ESP_MAX_STA_CONN,
            .authmode = (strlen(PROV_ESP_WIFI_AP_PASSWORD) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK,
            .pmf_cfg = { .required = false },
        },
    };

    if (strlen(PROV_ESP_WIFI_AP_PASSWORD) == 0) {
    memset(wifi_ap_config.ap.password, 0, sizeof(wifi_ap_config.ap.password));
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
    ESP_ERROR_CHECK(esp_wifi_start()); // Start Wi-Fi in AP mode

    ESP_LOGI(TAG, "SoftAP Wi-Fi initialized. SSID: %s", PROV_ESP_WIFI_AP_SSID);
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(prov_ap_netif, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG,"SoftAP IP Address: " IPSTR, IP2STR(&ip_info.ip));
    }

    return ESP_OK;
}

static void softap_prov_deinit_wifi(void) {
    ESP_LOGI(TAG, "Deinitializing SoftAP provisioning Wi-Fi...");
    stop_webserver(); // Stop HTTP server first

     // Unregister event handlers registered by this module
     esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &softap_prov_ap_event_handler);
     esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &softap_prov_ap_event_handler);

    // Stop Wi-Fi
    // Check if wifi is initialized before stopping?
     wifi_mode_t mode;
     if (esp_wifi_get_mode(&mode) == ESP_OK) { // Check if wifi is running
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
     }


     // Destroy the provisioning-specific AP netif
     if (prov_ap_netif) {
         esp_netif_destroy(prov_ap_netif);
         prov_ap_netif = NULL;
     }
     // Don't deinit the whole Wi-Fi stack (esp_wifi_deinit) here,
     // let the main wifi_manager handle the overall lifecycle.
     ESP_LOGI(TAG,"SoftAP provisioning Wi-Fi deinit complete.");
}


// --- Public Function ---
esp_err_t start_softap_provisioning(TickType_t timeout_ticks) {
    ESP_LOGI(TAG, "Starting SoftAP provisioning sequence...");

    // Create internal event group if it doesn't exist
    if (prov_event_group == NULL) {
        prov_event_group = xEventGroupCreate();
        if (prov_event_group == NULL) {
             ESP_LOGE(TAG, "Failed to create provisioning event group");
             return ESP_FAIL;
        }
    }
    xEventGroupClearBits(prov_event_group, PROV_DONE_BIT | PROV_FAIL_BIT);

    // Initialize Wi-Fi in AP mode for provisioning
    if (softap_prov_init_wifi() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SoftAP Wi-Fi for provisioning.");
        // No need to call deinit here as init failed during setup
        return ESP_FAIL;
    }

    // Start the web server
    if (start_webserver() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start webserver for provisioning.");
        softap_prov_deinit_wifi(); // Cleanup Wi-Fi if webserver fails
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Waiting for provisioning data via HTTP POST (Timeout: %lu ticks)...", (unsigned long)timeout_ticks);
    EventBits_t bits = xEventGroupWaitBits(prov_event_group,
                                           PROV_DONE_BIT | PROV_FAIL_BIT,
                                           pdFALSE, // Don't clear bits on exit
                                           pdFALSE, // Wait for EITHER bit
                                           timeout_ticks); // Use provided timeout

    esp_err_t result;
    if (bits & PROV_DONE_BIT) {
        ESP_LOGI(TAG, "Provisioning data received and saved successfully.");
        result = ESP_OK;
    } else if (bits & PROV_FAIL_BIT) {
        ESP_LOGE(TAG, "Provisioning process failed internally (e.g., save error).");
        result = ESP_FAIL;
    } else {
        // Timeout occurred
        ESP_LOGW(TAG, "Provisioning timed out.");
        result = ESP_ERR_TIMEOUT;
    }

    // Cleanup provisioning resources regardless of outcome
    softap_prov_deinit_wifi();

    // Optionally delete event group if not reused
    // if (prov_event_group) {
    //    vEventGroupDelete(prov_event_group);
    //    prov_event_group = NULL;
    // }

    ESP_LOGI(TAG, "SoftAP provisioning sequence finished with status: %d", result);
    return result;
}