/* SoftAP Provisioning Example - Corrected */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include <esp_http_server.h>
#include <ctype.h>
#include "esp_mac.h"

static const char *TAG = "WIFI_PROV"; // 改個名字避免混淆

// --- NVS儲存 Wi-Fi 憑證的 Key ---
#define NVS_NAMESPACE "storage"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"

// --- SoftAP 設定 ---
#define EXAMPLE_ESP_WIFI_AP_SSID      "ESP-Calendar-Setup"
#define EXAMPLE_ESP_WIFI_AP_PASSWORD  ""
#define EXAMPLE_ESP_WIFI_AP_CHANNEL   1
#define EXAMPLE_ESP_MAX_STA_CONN      1

// --- Event Group for Wi-Fi events ---
// 這個 Event Group 用於同步主流程的連接狀態
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT      = BIT1;

// --- Provisioning Event Group (內部使用) ---
static EventGroupHandle_t prov_event_group = NULL;
const int PROV_DONE_BIT = BIT0;
const int PROV_FAIL_BIT = BIT1; // 表示內部發生錯誤

static int s_retry_num = 0;

// --- HTTP Server Handle ---
static httpd_handle_t server = NULL;

// --- Netif handles (全局持有) ---
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;


// --- Forward declarations ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t start_softap_provisioning(void);
static esp_err_t save_wifi_credentials(const char* ssid, const char* password);
static esp_err_t read_wifi_credentials(char* ssid, size_t ssid_len, char* password, size_t pass_len);
static void wifi_init_sta(const char* ssid, const char* password); // 改為接收參數
static void wifi_init_softap(void);
static void url_decode_helper(const char* src, char* dst, size_t dst_size);
static esp_err_t root_get_handler(httpd_req_t *req);
static esp_err_t connect_post_handler(httpd_req_t *req);
static esp_err_t start_webserver(void);
static void stop_webserver(void);
static void softap_prov_ap_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data); // SoftAP專用事件處理
static void softap_prov_deinit_wifi(void);

// --- 網頁 HTML ---
// 簡單的 HTML 表單，讓使用者輸入 SSID 和密碼
const char* html_form =
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

// HTTP GET / handler - 回傳 HTML 表單
esp_err_t root_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Serving provisioning page");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_form, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// --- 靜態 URL 解碼輔助函數 ---
static void url_decode_helper(const char* src, char* dst, size_t dst_size) {
    char a, b;
    size_t len = 0;
    while (*src && len < dst_size - 1) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit((unsigned char)a) && isxdigit((unsigned char)b))) { // 確保使用 isxdigit
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10); else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10); else b -= '0';
            *dst++ = 16*a + b;
            src+=3;
            len++;
        } else if (*src == '+') {
            *dst++ = ' '; // handle '+' as space
            src++;
            len++;
        } else {
            *dst++ = *src++;
            len++;
        }
    }
    *dst = '\0'; // Null-terminate the destination buffer
}

// HTTP POST /connect handler
esp_err_t connect_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret, remaining = req->content_len;

    // 清空 buffer，避免舊數據干擾
    memset(buf, 0, sizeof(buf));

    if (remaining >= sizeof(buf)) {
        ESP_LOGE(TAG, "POST Content too long (%d bytes)", remaining);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        if(prov_event_group) xEventGroupSetBits(prov_event_group, PROV_FAIL_BIT);
        return ESP_FAIL;
    }
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) { httpd_resp_send_408(req); }
        ESP_LOGE(TAG, "Error receiving POST data (ret: %d)", ret);
        if(prov_event_group) xEventGroupSetBits(prov_event_group, PROV_FAIL_BIT);
        return ESP_FAIL;
    }
    buf[ret] = '\0'; // 確保字串結尾
    ESP_LOGI(TAG, "Received raw POST data (%d bytes): [%s]", ret, buf); // 打印原始數據

    char ssid[33] = {0};
    char password[65] = {0};
    char encoded_ssid[129] = {0}; // 稍微加大 encoded buffer
    char encoded_password[193] = {0}; // 稍微加大 encoded buffer

    // 清空目標 buffer
    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
    memset(encoded_ssid, 0, sizeof(encoded_ssid));
    memset(encoded_password, 0, sizeof(encoded_password));


    // --- 解析 ---
    esp_err_t parse_err = httpd_query_key_value(buf, "ssid", encoded_ssid, sizeof(encoded_ssid));
    if (parse_err != ESP_OK) {
         ESP_LOGW(TAG,"Could not find 'ssid' key in POST data. Raw: [%s]", buf);
         // 不直接報錯，繼續嘗試 password
    }

    parse_err = httpd_query_key_value(buf, "password", encoded_password, sizeof(encoded_password));
     if (parse_err != ESP_OK) {
         ESP_LOGW(TAG,"Could not find 'password' key in POST data. Raw: [%s]", buf);
         // 密碼可以為空，不報錯，但 encoded_password 會是空字串
     }

    // 檢查 SSID 是否為空 (解析後或原本就沒給)
     if (strlen(encoded_ssid) == 0) {
         ESP_LOGE(TAG, "SSID parameter is missing or empty in POST data.");
          httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or empty SSID parameter");
          if(prov_event_group) xEventGroupSetBits(prov_event_group, PROV_FAIL_BIT);
          return ESP_FAIL;
     }

    ESP_LOGI(TAG, "Value from httpd_query_key_value for SSID: [%s] (len %d)", encoded_ssid, strlen(encoded_ssid));
    ESP_LOGI(TAG, "Value from httpd_query_key_value for Password: [%s] (len %d)", encoded_password, strlen(encoded_password));

    // --- URL 解碼 ---
    url_decode_helper(encoded_ssid, ssid, sizeof(ssid));
    url_decode_helper(encoded_password, password, sizeof(password));

    // --- !!! TEMPORARY DEBUG LOGGING !!! ---
    ESP_LOGW(TAG, "------ Decoding Result ------");
    ESP_LOGW(TAG, "Decoded SSID: [%s] (len %d)", ssid, strlen(ssid));
    // --- !!! 安全警告：僅調試用，發布前移除 !!! ---
    ESP_LOGW(TAG, "Decoded Password: [%s] (len %d)", password, strlen(password));
    ESP_LOGW(TAG, "-----------------------------");
    // --- !!! END DEBUG LOGGING !!! ---


    // 再次檢查解碼後的 SSID 是否為空
    if (strlen(ssid) == 0) {
         ESP_LOGE(TAG, "Decoded SSID is empty (Decoding failed?).");
         httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to decode SSID");
         if(prov_event_group) xEventGroupSetBits(prov_event_group, PROV_FAIL_BIT);
         return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Final Parsed SSID for saving: [%s]", ssid);
    ESP_LOGI(TAG, "Final Parsed Password length for saving: %d", strlen(password));


    // --- 3. 儲存憑證到 NVS ---
    // 使用解碼後的 ssid 和 password
    esp_err_t save_err = save_wifi_credentials(ssid, password);
    if (save_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Wi-Fi credentials to NVS (%s)", esp_err_to_name(save_err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save credentials");
        if(prov_event_group) xEventGroupSetBits(prov_event_group, PROV_FAIL_BIT);
        return ESP_FAIL;
    }

    // --- 4. 回應瀏覽器 ---
    const char* resp_str = "Credentials received. Please wait for connection attempt..."; // 修改回應訊息
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    // --- 5. 標記配網(資料接收)完成 ---
    ESP_LOGI(TAG, "Setting PROV_DONE_BIT");
    // 延遲一小段時間確保 HTTP 回應發送完成
    vTaskDelay(pdMS_TO_TICKS(100));
    if(prov_event_group) xEventGroupSetBits(prov_event_group, PROV_DONE_BIT);

    return ESP_OK;
}

// URI Handler 結構
const httpd_uri_t uri_get = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = root_get_handler,
    .user_ctx = NULL
};

const httpd_uri_t uri_post = {
    .uri      = "/connect",
    .method   = HTTP_POST,
    .handler  = connect_post_handler,
    .user_ctx = NULL
};

// 啟動 HTTP 伺服器
esp_err_t start_webserver(void) {
    if (server == NULL) { // 確保不會重複啟動
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true;
        ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
         if (httpd_start(&server, &config) == ESP_OK) {
             ESP_LOGI(TAG, "Registering URI handlers");
             httpd_register_uri_handler(server, &uri_get);
             httpd_register_uri_handler(server, &uri_post);
             return ESP_OK;
         }
          ESP_LOGE(TAG, "Error starting server!");
          return ESP_FAIL;
    }
    ESP_LOGW(TAG, "Webserver already started.");
    return ESP_OK; // 或者返回錯誤，取決於設計
}

// 停止 HTTP 伺服器
void stop_webserver(void) { // 改為無參數，使用全局 server handle
    if (server) {
        httpd_stop(server);
        ESP_LOGI(TAG, "HTTP server stopped.");
        server = NULL; // 清除 handle
    }
}

// --- 主 Wi-Fi 事件處理函式 (處理 STA 和 IP 事件) ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
        // 現在 esp_wifi_connect() 可以在這裡調用，或者在 wifi_init_sta 尾部調用也可以
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
        s_retry_num = 0;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED");
        if (s_retry_num < 5) {
            esp_wifi_connect(); // 自動重連
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP (%d/5)", s_retry_num);
        } else {
            ESP_LOGE(TAG, "Connection failed after multiple retries.");
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT); // 標記最終失敗
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// --- SoftAP 配網專用事件處理 (只處理 AP 事件) ---
static void softap_prov_ap_event_handler(void* arg, esp_event_base_t event_base,
             int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "[Provisioning] Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "[Provisioning] Station " MACSTR " left, AID=%d", MAC2STR(event->mac), event->aid);
    }
}


// 初始化 Wi-Fi SoftAP 模式 (用於配網)
void wifi_init_softap(void) {
    // Netif 應該已經在 app_main 初始化
    // ESP_ERROR_CHECK(esp_netif_init()); // 不再需要
    // ESP_ERROR_CHECK(esp_event_loop_create_default()); // 不再需要

    // esp_netif_create_default_wifi_ap(); // 不再需要
    // esp_netif_create_default_wifi_sta(); // 不再需要

    // --- 只需要初始化 Wi-Fi 配置 ---
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg)); // 確保只初始化一次或可重複初始化

    // --- 註冊 SoftAP 專用的事件處理 ---
    // 註銷可能存在的舊 handler? 或者使用 instance 變數管理
    // 為了簡單，假設主 handler 會在進入 provisioning 前被註銷，或允許同時存在但只關心 AP 事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &softap_prov_ap_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &softap_prov_ap_event_handler, NULL));


    // --- 設定 SoftAP 的配置 ---
    wifi_config_t wifi_ap_config = {
        .ap = {
            // vvv--- 改回 EXAMPLE_ 前綴 ---vvv
            .ssid = EXAMPLE_ESP_WIFI_AP_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_AP_SSID),
            .channel = EXAMPLE_ESP_WIFI_AP_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_AP_PASSWORD,
            .max_connection = EXAMPLE_ESP_MAX_STA_CONN,
            // ^^^--- 改回 EXAMPLE_ 前綴 ---^^^
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = { .required = false },
        },
    };
    // vvv--- 改回 EXAMPLE_ 前綴 ---vvv
    if (strlen(EXAMPLE_ESP_WIFI_AP_PASSWORD) > 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        strncpy((char *)wifi_ap_config.ap.password, EXAMPLE_ESP_WIFI_AP_PASSWORD, sizeof(wifi_ap_config.ap.password)-1);
    } else {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
        memset(wifi_ap_config.ap.password, 0, sizeof(wifi_ap_config.ap.password));
    }
    // ^^^--- 改回 EXAMPLE_ 前綴 ---^^^


    // --- 設定 Wi-Fi 模式為 AP ---
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    // --- 套用 SoftAP 配置 ---
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    // --- 啟動 Wi-Fi ---
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished.");
    ESP_LOGI(TAG, "Connect to SoftAP SSID: %s", EXAMPLE_ESP_WIFI_AP_SSID); // <--- 改回 EXAMPLE_

    // --- vvv 修正：使用 esp_netif API 獲取 IP --- vvv
    esp_netif_ip_info_t ip_info;
    // 從全局變數 ap_netif (假設它已被正確創建和賦值) 獲取 IP
    esp_err_t err = esp_netif_get_ip_info(ap_netif, &ip_info);
    if (err == ESP_OK) {
        ESP_LOGI(TAG,"SoftAP IP Address: " IPSTR, IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "Open browser and navigate to http://" IPSTR, IP2STR(&ip_info.ip));
    } else {
        ESP_LOGE(TAG, "Failed to get SoftAP IP info (%s)", esp_err_to_name(err));
    }
    // --- ^^^ 修正：使用 esp_netif API 獲取 IP --- ^^^
}


// 啟動 SoftAP 配網流程
static esp_err_t start_softap_provisioning(void) {
    ESP_LOGI(TAG, "Starting SoftAP provisioning sequence...");

    // 創建內部事件組
    if (!prov_event_group) {
        prov_event_group = xEventGroupCreate();
    }
    xEventGroupClearBits(prov_event_group, PROV_DONE_BIT | PROV_FAIL_BIT); // 清除標誌

    wifi_init_softap(); // 初始化並啟動 Wi-Fi AP 模式

    if (start_webserver() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start webserver for provisioning.");
        softap_prov_deinit_wifi(); // 清理 Wi-Fi
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Waiting for provisioning data via HTTP POST...");
    EventBits_t bits = xEventGroupWaitBits(prov_event_group,
                    PROV_DONE_BIT | PROV_FAIL_BIT,
                    pdFALSE,        // 不需要清除 bits
                    pdFALSE,        // 等待任一 bit
                    portMAX_DELAY); // 一直等待 (或加入超時)

    esp_err_t result = ESP_FAIL; // 預設失敗

    if (bits & PROV_DONE_BIT) {
        ESP_LOGI(TAG, "Provisioning data received and saved.");
        result = ESP_OK;
    } else if (bits & PROV_FAIL_BIT) {
        ESP_LOGE(TAG, "Provisioning process failed internally.");
        result = ESP_FAIL;
    } else {
        ESP_LOGW(TAG,"Exited provisioning wait loop unexpectedly (timeout?).");
        result = ESP_ERR_TIMEOUT; // 或者其他錯誤
    }

    // 清理 SoftAP 和 Web Server
    softap_prov_deinit_wifi();

    // 清理內部事件組 (可選，如果每次都創建的話)
    // if (prov_event_group) {
    //    vEventGroupDelete(prov_event_group);
    //    prov_event_group = NULL;
    // }

    return result; // 返回配網是否成功收到資料
}

// 清理 SoftAP 配網模式
static void softap_prov_deinit_wifi(void) {
    ESP_LOGI(TAG, "Deinitializing SoftAP provisioning Wi-Fi...");

    stop_webserver(); // <--- 加上這行調用

    // 註銷 SoftAP 事件處理器
    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &softap_prov_ap_event_handler);
    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &softap_prov_ap_event_handler);

    // 停止 Wi-Fi AP 模式
    // 最簡單的方式是讓主流程負責設置回 STA 模式並啟動
    // 或者直接停止 Wi-Fi，確保狀態乾淨
    ESP_ERROR_CHECK(esp_wifi_stop()); // 停止當前的 Wi-Fi 模式 (AP)

    ESP_LOGI(TAG,"SoftAP provisioning Wi-Fi deinit complete.");
}

// 初始化 Wi-Fi Station 模式 (使用傳入的憑證)
void wifi_init_sta(const char* ssid, const char* password) {
    ESP_LOGI(TAG, "Initializing Wi-Fi STA mode for SSID: [%s]", ssid);
    s_retry_num = 0; // 重置重試計數器
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT); // 清除狀態位

    // --- 初始化 Wi-Fi 配置 ---
    // 檢查 Wi-Fi 是否已初始化，避免重複初始化 (雖然 ESP_ERROR_CHECK 會處理)
    // esp_err_t init_err = esp_wifi_get_mode(NULL); // 檢查是否已 init? (更複雜)
    // 簡單起見，假設可以重複調用 init 或由主流程保證只 init 一次
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // --- 註冊主事件處理器 (處理 STA 和 IP 事件) ---
    // 考慮反註冊舊的 handler (如果存在) - 使用 instance handle 更佳
    // 為了簡單，假設可以重複註冊或舊 handler 已被清理
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // --- 設定 Wi-Fi 模式為 STA ---
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // --- 配置 STA ---
    wifi_config_t wifi_config = { 0 };
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = strlen(password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // --- 啟動 Wi-Fi STA ---
    ESP_ERROR_CHECK(esp_wifi_start()); // ***** 必須在 connect 之前 *****

    // --- 嘗試連接 ---
    esp_err_t connect_err = esp_wifi_connect();

    if (connect_err == ESP_OK) {
        ESP_LOGI(TAG, "esp_wifi_connect() called successfully for SSID [%s]", ssid);
    } else {
        ESP_LOGE(TAG, "esp_wifi_connect() failed (%s)", esp_err_to_name(connect_err));
        // 連接調用失敗，直接標記失敗
        xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
    }
     // ESP_LOGI(TAG, "Wi-Fi STA mode initialized and started. Trying to connect to SSID: [%s]", ssid); // Log 已移至 connect 後
}


// --- NVS Helper Functions ---
// (移至 wifi_storage.c/h)
// static esp_err_t save_wifi_credentials(...) { ... }
// static esp_err_t read_wifi_credentials(...) { ... }


// 主程式入口
void app_main(void) {
    //nvs_flash_erase(); //test only
    // --- 1. 初始化 NVS ---
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- 2. 初始化底層 TCP/IP stack 和 Event Loop (全局一次) ---
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // --- 3. 創建預設 Wi-Fi 網路介面 (全局一次) ---
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();
    assert(sta_netif); // 確保創建成功
    assert(ap_netif);

    // --- 4. 創建主 Event Group ---
    wifi_event_group = xEventGroupCreate();

    // --- 5. 嘗試讀取並連接 ---
    char ssid[33] = {0};
    char password[65] = {0};
    bool connected = false; // 追蹤是否成功連接

    esp_err_t read_status = read_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password));

    if (read_status == ESP_OK) {
        ESP_LOGI(TAG, "Found credentials in NVS. SSID: [%s]. Trying to connect...", ssid);
        wifi_init_sta(ssid, password); // 使用讀取到的憑證初始化並嘗試連接

        // 等待連接結果 (成功獲取 IP 或失敗)
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                        pdFALSE,
                        pdFALSE,
                        pdMS_TO_TICKS(20000)); // 等待 20 秒

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Successfully connected to Wi-Fi using stored credentials!");
            connected = true;
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGW(TAG, "Failed to connect using stored credentials (WIFI_FAIL_BIT set).");
            esp_wifi_stop(); // 停止 Wi-Fi 以準備進入 SoftAP
            // 可能需要註銷主事件處理器? 避免干擾 SoftAP 的事件處理
            esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
            esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);

        } else {
            ESP_LOGW(TAG, "Timeout waiting for connection using stored credentials.");
            esp_wifi_stop(); // 停止 Wi-Fi
            esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
            esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
        }
    } else {
        ESP_LOGI(TAG, "No valid credentials found in NVS.");
        // 不需要停止Wi-Fi，因為根本沒啟動
    }

    // --- 6. 如果未連接，則進入 SoftAP 配網 ---
    if (!connected) {
        ESP_LOGI(TAG, "Entering SoftAP provisioning mode...");
        esp_err_t prov_status = start_softap_provisioning(); // 啟動配網流程

        if (prov_status == ESP_OK) {
            ESP_LOGI(TAG, "SoftAP provisioning finished. Credentials saved. Trying to connect again...");
            // 重新讀取剛存的憑證
            read_status = read_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password));
            if (read_status == ESP_OK) {
                wifi_init_sta(ssid, password); // 再次嘗試初始化 STA 並連接

                // 再次等待連接結果
                EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE,
                                            pdFALSE,
                                            pdMS_TO_TICKS(20000));

                if (bits & WIFI_CONNECTED_BIT) {
                    ESP_LOGI(TAG, "Successfully connected to Wi-Fi after provisioning!");
                    connected = true;
                } else {
                    ESP_LOGE(TAG, "Failed to connect even after provisioning.");
                    // 此處可以決定是否重試 provisioning 或進入錯誤狀態
                }
            } else {
            ESP_LOGE(TAG, "Failed to read credentials immediately after saving!");
            // NVS 問題？
            }
        } else {
            ESP_LOGE(TAG, "SoftAP provisioning failed or timed out.");
            // 錯誤處理，例如提示用戶重啟或按鈕重試
        }
    }

    // --- 7. 如果最終連接成功，執行主任務 ---
    if (connected) {
        ESP_LOGI(TAG, "Device connected. Starting main application logic...");
        // --- !!! 在這裡加入您獲取 ICS 資料、更新 E-Paper 的主要應用程式邏輯 !!! ---
        while(1) {
            ESP_LOGI(TAG, "Main task running...");
            // Example: Fetch ICS, update display...
            vTaskDelay(pdMS_TO_TICKS(60000)); // Simulate work
        }
        // -----------------------------------------------------------------------
    } else {
        ESP_LOGE(TAG, "Device could not connect to Wi-Fi. Halting or restarting.");
        // 可以在這裡進行深度睡眠、顯示錯誤或延時後重啟
        // esp_restart();
        vTaskDelay(portMAX_DELAY); // 卡住或等待重啟
    }

    // 清理 Event Group (如果需要退出 app_main)
    vEventGroupDelete(wifi_event_group);
    if (prov_event_group) {
        vEventGroupDelete(prov_event_group);
    }
}

// --- NVS Helper Functions Implementations ---
static esp_err_t save_wifi_credentials(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    } else {
        err = nvs_set_str(nvs_handle, NVS_KEY_WIFI_SSID, ssid);
        if (err == ESP_OK) {
            err = nvs_set_str(nvs_handle, NVS_KEY_WIFI_PASS, password);
        }
        if (err == ESP_OK) {
            err = nvs_commit(nvs_handle);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Wi-Fi credentials saved to NVS.");
            } else {
                 ESP_LOGE(TAG, "Error (%s) committing NVS!", esp_err_to_name(err));
            }
        } else {
            ESP_LOGE(TAG, "Error (%s) writing to NVS!", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
        return err;
    }
}

static esp_err_t read_wifi_credentials(char* ssid, size_t ssid_len, char* password, size_t pass_len) {
    nvs_handle_t nvs_handle;
    memset(ssid, 0, ssid_len);
    memset(password, 0, pass_len);

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
         // Don't log error for NOT_FOUND or NOT_INITIALIZED
         if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_NVS_NOT_INITIALIZED) {
             ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
         }
        return err; // Return original error code
    } else {
        size_t required_ssid_len = ssid_len;
        err = nvs_get_str(nvs_handle, NVS_KEY_WIFI_SSID, ssid, &required_ssid_len);
        if (err == ESP_OK) {
             size_t required_pass_len = pass_len;
            esp_err_t pass_err = nvs_get_str(nvs_handle, NVS_KEY_WIFI_PASS, password, &required_pass_len);
            if (pass_err != ESP_OK && pass_err != ESP_ERR_NVS_NOT_FOUND) {
                 ESP_LOGE(TAG, "Error (%s) reading password from NVS!", esp_err_to_name(pass_err));
                 err = pass_err; // Propagate password read error
            } else if (pass_err == ESP_ERR_NVS_NOT_FOUND) {
                 ESP_LOGI(TAG,"Password not found in NVS (might be an open network).");
                 // Keep err as ESP_OK since SSID was found
            }
        } else if (err != ESP_ERR_NVS_NOT_FOUND) { // Don't log error for NOT_FOUND
             ESP_LOGE(TAG, "Error (%s) reading SSID from NVS!", esp_err_to_name(err));
        }

        nvs_close(nvs_handle);
        // Return ESP_OK only if SSID was successfully read, otherwise return the error (like ESP_ERR_NVS_NOT_FOUND)
        return (err == ESP_OK && strlen(ssid) > 0) ? ESP_OK : err;
    }
}
