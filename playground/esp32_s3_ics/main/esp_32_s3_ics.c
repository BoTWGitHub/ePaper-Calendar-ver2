#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h> // For time functions
#include <sys/time.h> // For timeval
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_sntp.h"         // For SNTP
#include "esp_http_client.h"  // For HTTP Client
#include "esp_tls.h"          // For HTTPS
#include "esp_crt_bundle.h" // Include if using certificate bundle for validation

// --- 設定您的 Wi-Fi 和 ICS URL ---
#define WIFI_SSID      "HowDareYou" // Wi-Fi SSID
#define WIFI_PASS      "our_home@A5-17"
#define ICS_URL        "https://calendar.google.com/calendar/ical/k2345777%40gmail.com/private-b0c29d880dba4dc47d620ce09ac4ac85/basic.ics" // e.g., "https://calendar.google.com/calendar/ical/..." (必須是 HTTPS)

// --- 常數 ---
#define MAX_EVENTS              50   // 最多處理的未來事件數量
#define MAX_SUMMARY_LEN         100  // 事件摘要最大長度
#define MAX_HTTP_RECV_BUFFER    1024 // HTTP 接收緩衝區大小
#define MAX_ICS_LINE_LEN        256  // ICS 每行最大長度 (考慮折行前)
#define MAX_DT_STR_LEN          32   // 用於日期時間字串操作的緩衝區大小

static const char *TAG = "ICS_DEMO";

// --- Wi-Fi 連接狀態 Event Group ---
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT      = BIT1;
static int s_retry_num = 0;
#define WIFI_MAX_RETRY 5

// --- SNTP 同步狀態 ---
static bool sntp_synchronized = false;

// --- 事件結構 ---
typedef struct {
    time_t start_time;                // 事件開始時間 (UTC time_t)
    char summary[MAX_SUMMARY_LEN]; // 事件摘要
} calendar_event_t;

// --- 全局事件陣列和計數 ---
static calendar_event_t future_events[MAX_EVENTS];
static int future_event_count = 0;

// --- Forward Declarations ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void wifi_init_sta(void);
static void initialize_sntp(void);
static void time_sync_notification_cb(struct timeval *tv);
esp_err_t http_get_ics(const char *url);
static void parse_ics_data(const char *ics_data_chunk, size_t len);
static time_t parse_dtstart(const char* dtstart_str);
static int compare_events(const void *a, const void *b);
static void print_upcoming_events(int count);


// --- Wi-Fi Event Handler ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// --- Wi-Fi Initialization ---
static void wifi_init_sta(void) {
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Adjust if needed
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    // Wait for connection or failure
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    // Wi-Fi stack keeps event handlers registered after connect
}

// --- SNTP Initialization ---
static void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org"); // Use NTP pool server
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    // esp_sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH); // Optional: Smooth sync
    esp_sntp_init();

    // Set Timezone to CST (UTC+8) - China Standard Time (also Taiwan Standard Time)
    setenv("TZ", "CST-8", 1); // Format is std<offset>[dst[offset][,start[/time],end[/time]]]
    tzset(); // Apply the timezone setting
    ESP_LOGI(TAG, "Timezone set to CST-8");
}

// --- SNTP Time Sync Callback ---
static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "SNTP time synchronized: %ld seconds", tv->tv_sec);
    sntp_synchronized = true;
}

// --- HTTP Event Handler (for reading response body) ---
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // Process incoming data chunk by chunk
             if (evt->user_data) { // Check if context pointer is valid
                 parse_ics_data(evt->data, evt->data_len);
             }
             // Example of storing chunked data (if not parsing directly)
             /*
            if (!esp_http_client_is_chunked_response(evt->client)) {
                 // If user_data buffer is configured, copy the response into it
                 if (evt->user_data) {
                     memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                 } else {
                      if (output_buffer == NULL) {
                          output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                          output_len = 0;
                           if (output_buffer == NULL) {
                               ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                               return ESP_FAIL;
                           }
                      }
                      memcpy(output_buffer + output_len, evt->data, evt->data_len);
                 }
                 output_len += evt->data_len;
            }
            */
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
             // Cleanup example buffer
             /*
             if (output_buffer != NULL) {
                 free(output_buffer);
                 output_buffer = NULL;
             }
             output_len = 0;
             */
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
             // Cleanup example buffer
             /*
             int mbedtls_err = 0;
             esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
             if (err != 0) {
                 if (output_buffer != NULL) {
                     free(output_buffer);
                     output_buffer = NULL;
                 }
                 output_len = 0;
             }
             */
             // Signal completion if needed
             if (evt->user_data) {
                 // Set a bit or semaphore
             }
            break;
         case HTTP_EVENT_REDIRECT:
             ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
             esp_http_client_set_header(evt->client, "From", "user@example.com");
             esp_http_client_set_header(evt->client, "Accept", "text/html");
             esp_http_client_set_redirection(evt->client);
             break;

    }
    return ESP_OK;
}

// --- Fetch ICS Data via HTTP GET ---
esp_err_t http_get_ics(const char *url) {
    future_event_count = 0; // Reset event counter for new fetch
    memset(future_events, 0, sizeof(future_events)); // Clear event array

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = future_events, // Pass a context pointer (can be anything) to signal parsing
        .disable_auto_redirect = false, // Handle redirects automatically
        // For HTTPS:
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach, // Uncomment for certificate validation
        .skip_cert_common_name_check = false, // ** Insecure! For testing only **
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
         ESP_LOGE(TAG, "Failed to initialize HTTP client");
         return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS GET Status = %d, content_length = %"PRId64,
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTPS GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// --- Manual DTSTART Parser ---
// Returns 1 on success, 0 on failure
// Fills t_out with parsed components
// Sets is_utc_out to true if 'Z' is present and time part exists
static int manual_parse_dtstart(const char* dt_str_in, struct tm *t_out, bool *is_utc_out) {
    memset(t_out, 0, sizeof(struct tm)); 
    if (is_utc_out) *is_utc_out = false;

    char cleaned_dt_str[MAX_DT_STR_LEN]; 
    const char *p_in = dt_str_in;
    int i = 0;

    // Trim leading whitespace
    while (*p_in && isspace((unsigned char)*p_in)) {
        p_in++;
    }
    // Copy to cleaned_dt_str
    while (*p_in && i < MAX_DT_STR_LEN - 1) {
        cleaned_dt_str[i++] = *p_in++;
    }
    cleaned_dt_str[i] = '\0';

    // Trim trailing whitespace
    i--;
    while (i >= 0 && isspace((unsigned char)cleaned_dt_str[i])) {
        cleaned_dt_str[i--] = '\0';
    }
    
    // ESP_LOGD(TAG, "Manual parsing cleaned DTSTART: [%s]", cleaned_dt_str);

    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    int consumed_chars = 0;
    int n_parsed_items = 0;

    // Try YYYYMMDDTHHMMSS format
    n_parsed_items = sscanf(cleaned_dt_str, "%4d%2d%2dT%2d%2d%2d%n",
               &year, &month, &day, &hour, &min, &sec, &consumed_chars);

    if (n_parsed_items == 6) { 
        if (is_utc_out && cleaned_dt_str[consumed_chars] == 'Z') {
            *is_utc_out = true;
        }
    }
    // Else, try YYYYMMDD format (all-day event)
    else {
        hour = 0; min = 0; sec = 0; consumed_chars = 0; // Reset time for all-day
        n_parsed_items = sscanf(cleaned_dt_str, "%4d%2d%2d%n", &year, &month, &day, &consumed_chars);
        if (n_parsed_items == 3) { 
            if (is_utc_out && cleaned_dt_str[consumed_chars] == 'Z') {
                 *is_utc_out = true; // Although 'Z' is less common for pure DATE
            }
        } else {
            ESP_LOGW(TAG, "Manual sscanf parse failed for DTSTART: [%s] (original: [%s])", cleaned_dt_str, dt_str_in);
            return 0; 
        }
    }

    // Basic validation of parsed components
    if (year < 1970 || year > 2038 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60) { // sec can be 60 for leap second
        ESP_LOGW(TAG, "Parsed date/time components out of typical valid range: Y%d M%d D%d H%d M%d S%d (from: [%s])", year, month, day, hour, min, sec, cleaned_dt_str);
        // Don't return fail immediately, mktime will also check validity
    }

    t_out->tm_year = year - 1900;
    t_out->tm_mon = month - 1; 
    t_out->tm_mday = day;
    t_out->tm_hour = hour;
    t_out->tm_min = min;
    t_out->tm_sec = sec;
    t_out->tm_isdst = -1; 

    return 1; 
}

// --- Basic ICS Parser (Processes data chunk by chunk) ---
static char line_buffer[MAX_ICS_LINE_LEN] = {0};
static int line_buffer_len = 0;
static bool in_vevent = false;
static calendar_event_t current_event = {0};

static void process_ics_line(const char *line) {
    // ESP_LOGD(TAG, "Processing line: [%s]", line); // DEBUG: See every line
    if (strncmp(line, "BEGIN:VEVENT", 12) == 0) {
        ESP_LOGD(TAG, "Found BEGIN:VEVENT");
        in_vevent = true;
        memset(&current_event, 0, sizeof(current_event)); 
        return;
    }
    if (strncmp(line, "END:VEVENT", 10) == 0) {
        ESP_LOGD(TAG, "Found END:VEVENT");
        if (in_vevent) {
            time_t now_utc;
            time(&now_utc); // time() returns UTC time_t
            
            ESP_LOGD(TAG, "Event Summary: [%s], Raw DTSTART time_t: %lld, Current UTC time_t: %lld", 
                     current_event.summary, (long long)current_event.start_time, (long long)now_utc);

            if (current_event.start_time > 0 && current_event.start_time > now_utc) {
                 if (future_event_count < MAX_EVENTS) {
                    memcpy(&future_events[future_event_count], &current_event, sizeof(calendar_event_t));
                    ESP_LOGI(TAG, "Added future event: [%s] at %lld", current_event.summary, (long long)current_event.start_time);
                    future_event_count++;
                 } else {
                      ESP_LOGW(TAG, "Max future events limit reached (%d)", MAX_EVENTS);
                 }
            } else if (current_event.start_time > 0) {
                 ESP_LOGD(TAG, "Event [%s] is in the past or now.", current_event.summary);
            }
        }
        in_vevent = false;
        return;
    }

    if (in_vevent) {
        if (strncmp(line, "SUMMARY", 7) == 0) { // Allow SUMMARY; or SUMMARY:
            const char *summary_start = strchr(line, ':');
            if (summary_start) {
                summary_start++; // Move past ':'
                // Trim leading spaces from summary_start if any
                while(*summary_start && isspace((unsigned char)*summary_start)) summary_start++;

                strncpy(current_event.summary, summary_start, MAX_SUMMARY_LEN - 1);
                current_event.summary[MAX_SUMMARY_LEN - 1] = '\0'; 
                // Trim trailing spaces from summary (less common but good practice)
                char *summary_end = current_event.summary + strlen(current_event.summary) - 1;
                while(summary_end >= current_event.summary && isspace((unsigned char)*summary_end)) *summary_end-- = '\0';

                ESP_LOGD(TAG, "Found Summary: [%s]", current_event.summary);
            }
        } else if (strncmp(line, "DTSTART", 7) == 0) {
            ESP_LOGD(TAG, "Found DTSTART line: [%s]", line);
            const char *dt_value_raw = strchr(line, ':');
            if (dt_value_raw) {
                dt_value_raw++; 

                struct tm parsed_tm;
                bool is_event_utc = false;
                if (manual_parse_dtstart(dt_value_raw, &parsed_tm, &is_event_utc)) {
                    time_t event_time_t;
                    char *original_tz_env = getenv("TZ"); // Get current TZ set by initialize_sntp

                    if (is_event_utc) {
                        // Convert UTC tm to UTC time_t
                        setenv("TZ", "UTC0", 1); // Temporarily set system TZ to UTC
                        tzset();
                        event_time_t = mktime(&parsed_tm); 
                        
                        // Restore original/application TZ
                        if (original_tz_env && strlen(original_tz_env) > 0) {
                            setenv("TZ", original_tz_env, 1);
                        } else { 
                            // If original was NULL or empty, revert to system default (often by unsetting)
                            // For this app, it means back to CST-8 if initialize_sntp set it
                            // If unsure, re-set to application default:
                            setenv("TZ", "CST-8", 1); // Or use original_timezone_str if saved globally
                        }
                        tzset();
                    } else {
                        // Time is floating or local. mktime will use ESP32's current TZ setting (e.g., "CST-8").
                        event_time_t = mktime(&parsed_tm);
                    }

                    if (event_time_t == (time_t)-1) {
                        ESP_LOGW(TAG, "mktime failed for parsed DTSTART value: %s", dt_value_raw);
                        current_event.start_time = (time_t)-1; 
                    } else {
                        current_event.start_time = event_time_t;
                         ESP_LOGI(TAG, "Parsed DTSTART [%s] (UTC flag: %s) -> UTC time_t: %lld", dt_value_raw, is_event_utc ? "Yes" : "No", (long long)event_time_t);
                    }
                } else {
                    // manual_parse_dtstart already logged the error
                    current_event.start_time = (time_t)-1; 
                }
            } else {
                 ESP_LOGW(TAG, "DTSTART line format error (no colon?): [%s]", line);
            }
        }
    }
}

// Function to handle incoming chunks and assemble lines
static void parse_ics_data(const char *ics_data_chunk, size_t len) {
    const char *p = ics_data_chunk;
    const char *end = ics_data_chunk + len;

    while (p < end) {
        char c = *p++;
        if (c == '\n') {
            if (line_buffer_len > 0 && line_buffer[line_buffer_len - 1] == '\r') {
                line_buffer[line_buffer_len - 1] = '\0'; // Remove CR if present
            } else {
                line_buffer[line_buffer_len] = '\0'; // Null-terminate
            }

            // Check for folded lines (next line starts with space or tab) - Basic handling
            // A more robust parser would handle unfolding properly *before* processing.
            // This simple version processes line by line.
            if (line_buffer_len > 0) {
                 // ESP_LOGD(TAG, "Processing line: %s", line_buffer);
                process_ics_line(line_buffer);
            }
            line_buffer_len = 0; // Reset buffer
        } else {
            if (line_buffer_len < MAX_ICS_LINE_LEN - 1) {
                line_buffer[line_buffer_len++] = c;
            } else {
                 // Line too long, discard or handle error
                 ESP_LOGW(TAG, "ICS line too long, discarding partial line.");
                 line_buffer_len = 0; // Reset on overflow
                 // Skip to next newline
                 while(p < end && *p != '\n') p++;
                 if (p < end) p++; // Consume the newline
            }
        }
    }
    // Keep any remaining partial line in the buffer for the next chunk
}


// --- Basic DTSTART Parser ---
// Handles YYYYMMDDTHHMMSSZ (UTC) and YYYYMMDDTHHMMSS (assumes local if TZ not set)
// Returns UTC time_t or -1 on error
static time_t parse_dtstart(const char* dtstart_str) {
    struct tm t = {0};
    const char *parse_end = NULL;
    bool is_utc = false;

    // Try parsing YYYYMMDDTHHMMSS format
    parse_end = strptime(dtstart_str, "%Y%m%dT%H%M%S", &t);

    if (parse_end == NULL) {
         // Try parsing YYYYMMDD format (all-day event)
         parse_end = strptime(dtstart_str, "%Y%m%d", &t);
         if (parse_end == NULL) {
              ESP_LOGW(TAG, "strptime failed for DTSTART: %s", dtstart_str);
              return (time_t)-1;
         }
         // All-day event, time components are 00:00:00
         // Fall through to check for 'Z' or calculate time_t
    }

     // Check if the next character after parsing is 'Z' indicating UTC
    if (*parse_end == 'Z') {
        is_utc = true;
    }

    // Convert struct tm to time_t
    time_t result_time;
    if (is_utc) {
        // timegm is non-standard but common for converting UTC struct tm to UTC time_t
        // If timegm is not available, mktime assumes local timezone based on TZ env var.
        // Since we set TZ=CST-8, mktime will interpret the struct tm as CST.
        // We need to adjust it back to UTC.
        #ifdef HAVE_TIMEGM // Check if toolchain provides timegm
             result_time = timegm(&t);
        #else
             // Workaround: mktime converts assuming local time. Adjust for offset.
             result_time = mktime(&t); // t is interpreted as local (CST+8)
             if (result_time != (time_t)-1) {
                   // Get current timezone offset (should be -8 * 3600 for CST-8)
                  // time_t now; time(&now); struct tm *local = localtime(&now); long offset = local->tm_gmtoff; // This relies on libc supporting tm_gmtoff
                  // Or simpler: since we set TZ=CST-8, the offset IS -8 hours
                  result_time -= (-8 * 3600); // Subtract the negative offset to get UTC
             }
        #endif
    } else {
        // No 'Z', assume time is in the timezone set by TZ environment variable (CST-8)
        // mktime converts local time struct tm to UTC time_t
        result_time = mktime(&t);
    }

    if (result_time == (time_t)-1) {
        ESP_LOGW(TAG, "mktime/timegm failed for DTSTART: %s", dtstart_str);
    }

    return result_time;
}

// --- Comparison function for qsort ---
static int compare_events(const void *a, const void *b) {
    calendar_event_t *eventA = (calendar_event_t *)a;
    calendar_event_t *eventB = (calendar_event_t *)b;
    if (eventA->start_time < eventB->start_time) return -1;
    if (eventA->start_time > eventB->start_time) return 1;
    return 0;
}

// --- Print Upcoming Events ---
static void print_upcoming_events(int count) {
    if (future_event_count == 0) {
        ESP_LOGI(TAG, "No upcoming events found in ICS.");
        return;
    }

    ESP_LOGI(TAG, "--- Upcoming Events (Max %d) ---", count);
    int print_count = (future_event_count < count) ? future_event_count : count;

    time_t now;
    time(&now);
    ESP_LOGI(TAG, "Current Time: %s", ctime(&now)); // ctime adds newline

    for (int i = 0; i < print_count; i++) {
        // Convert UTC start_time to local time string for printing
        // time_t local_start = future_events[i].start_time; // Already UTC time_t
        struct tm *local_tm = localtime(&future_events[i].start_time);
        char time_buf[64];
        if (local_tm) {
             strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S %Z", local_tm); // Format as local time with TZ name
        } else {
             snprintf(time_buf, sizeof(time_buf), "Invalid Time");
        }

        ESP_LOGI(TAG, "%d: %s - %s", i + 1, time_buf, future_events[i].summary);
    }
     ESP_LOGI(TAG, "-----------------------------");
}


// --- Main Application ---
void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Wi-Fi and connect
    ESP_LOGI(TAG, "Initializing Wi-Fi...");
    wifi_init_sta();

    // Wait until Wi-Fi is connected
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if (!(bits & WIFI_CONNECTED_BIT)) {
         ESP_LOGE(TAG, "Wi-Fi connection failed. Cannot proceed.");
         // Maybe block or restart?
         vTaskDelay(portMAX_DELAY);
         return; // Should not happen if wifi_init_sta blocks
    }
    ESP_LOGI(TAG, "Wi-Fi Connected.");

    // Initialize SNTP and wait for sync
    ESP_LOGI(TAG, "Initializing SNTP...");
    initialize_sntp();
    int max_sntp_wait_sec = 30;
    while (!sntp_synchronized && max_sntp_wait_sec-- > 0) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    if (!sntp_synchronized) {
        ESP_LOGE(TAG, "SNTP time synchronization failed. Event times may be incorrect.");
        // Proceed anyway? Or stop? For demo, proceed.
    } else {
         time_t now;
         time(&now);
         struct tm timeinfo;
         localtime_r(&now, &timeinfo);
         char strftime_buf[64];
         strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
         ESP_LOGI(TAG, "Current local time set: %s", strftime_buf);
    }


    // Fetch and parse ICS data
    ESP_LOGI(TAG, "Fetching ICS data from %s", ICS_URL);
    ret = http_get_ics(ICS_URL);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ICS data fetched successfully. Parsed %d future events.", future_event_count);

        // Sort events by start time
        if (future_event_count > 0) {
            qsort(future_events, future_event_count, sizeof(calendar_event_t), compare_events);
            ESP_LOGI(TAG, "Future events sorted.");
        }

        // Print the upcoming events
        print_upcoming_events(10); // Print the nearest 10

    } else {
        ESP_LOGE(TAG, "Failed to fetch or process ICS data.");
    }

    ESP_LOGI(TAG, "ICS Demo finished.");

     // Keep task running or enter deep sleep etc.
     while(1) {
         vTaskDelay(pdMS_TO_TICKS(60000));
     }
}