#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "epd_7in5_v2.hpp"
#include "spi.hpp"
#include "glance_board.hpp"
#include "wifi.hpp"
#include "time_manager.hpp"
#include "config.hpp"
#include "storage.hpp"
#include "http_client.hpp"
#include "ics_parser.hpp"
#include "epd_lvgl_bridge.hpp"
#include "calendar_ui.hpp"

static const char *TAG = "main";

// ---------------------------------------------------------------
// Helper: convert IcsEvent list to CalendarUiData for rendering
// ---------------------------------------------------------------

static std::string format_event_time(const IcsEvent &event) {
    // Parse DTSTART to extract hour:minute
    const std::string &dt = event.start_time;

    // All-day events have date-only format: YYYYMMDD (length 8, no 'T')
    if (dt.length() <= 8 || dt.find('T') == std::string::npos) {
        return "All Day";
    }

    // Timed event: YYYYMMDDTHHMMSS or YYYYMMDDTHHMMSSZ
    int hour = 0, min = 0;
    if (dt.length() >= 13) {
        sscanf(dt.substr(9, 4).c_str(), "%2d%2d", &hour, &min);
    }

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, min);

    // Also append end time if available
    const std::string &end = event.end_time;
    if (end.length() >= 13 && end.find('T') != std::string::npos) {
        int eh = 0, em = 0;
        sscanf(end.substr(9, 4).c_str(), "%2d%2d", &eh, &em);
        char buf2[32];
        snprintf(buf2, sizeof(buf2), "%02d:%02d - %02d:%02d", hour, min, eh, em);
        return std::string(buf2);
    }

    return std::string(buf);
}

static CalendarUiData build_ui_data(const std::vector<IcsEvent> &events) {
    CalendarUiData data;

    // Get current time for the header
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    // Format date: "Jun 28"
    char date_buf[32];
    strftime(date_buf, sizeof(date_buf), "%b %d", &timeinfo);
    data.date_str = date_buf;

    // Format weekday: "Saturday"
    char weekday_buf[16];
    strftime(weekday_buf, sizeof(weekday_buf), "%A", &timeinfo);
    data.weekday_str = weekday_buf;

    // Format year: "2026"
    char year_buf[8];
    strftime(year_buf, sizeof(year_buf), "%Y", &timeinfo);
    data.year_str = year_buf;

    // Convert events
    for (const auto &ev : events) {
        EventDisplay disp;
        disp.time_str = format_event_time(ev);
        disp.title = ev.summary;
        disp.is_all_day = (disp.time_str == "All Day");
        data.events.push_back(disp);
    }

    return data;
}

// ---------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Glance e-ink calendar starting...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // ── 1. Mount storage and load config ──────────────────────
    AppConfig app_cfg;
    if (Storage::init("/spiffs", "storage") == ESP_OK) {
        Storage::list_files();
        ConfigManager cfg_mgr("/spiffs/config.json");
        if (cfg_mgr.load() == ESP_OK) {
            app_cfg = cfg_mgr.get();
            ESP_LOGI(TAG, "Config loaded successfully");
        }
    }

    // ── 2. Power on V33_2 rail (EPD, Flash, LED) ─────────────
    Gpio power(board::PWR, Gpio::Dir::output);
    power.write(true);
    vTaskDelay(pdMS_TO_TICKS(100));

    // ── 3. Connect WiFi → NTP → Fetch ICS ────────────────────
    std::vector<IcsEvent> events;

    Wifi& wifi = Wifi::get_instance();
    if (!app_cfg.wifi.ssid.empty()) {
        if (wifi.connect(app_cfg.wifi.ssid, app_cfg.wifi.password) == ESP_OK) {
            ESP_LOGI(TAG, "WiFi connected");

            // Sync time via NTP
            TimeManager& time_mgr = TimeManager::get_instance();
            if (time_mgr.sync() == ESP_OK) {
                time_mgr.set_timezone("CST-8");
                ESP_LOGI(TAG, "Time synced: %s", time_mgr.get_formatted_time().c_str());
            }

            // Fetch calendar data
            if (!app_cfg.calendar.urls.empty()) {
                HttpClient http_client;
                IcsParser ics_parser;

                http_client.set_on_line_received([&ics_parser](const std::string& line) {
                    ics_parser.parse_line(line);
                });

                ESP_LOGI(TAG, "Fetching calendar data...");
                if (http_client.fetch(app_cfg.calendar.urls[0]) == ESP_OK) {
                    ics_parser.flush();
                    events = ics_parser.get_events();
                    ESP_LOGI(TAG, "Parsed %u events", (unsigned)events.size());
                    ics_parser.print_events();
                }
            }
        } else {
            ESP_LOGE(TAG, "WiFi connection failed");
        }
    } else {
        ESP_LOGE(TAG, "WiFi SSID is empty in config");
    }

    // ── 4. Render calendar UI via LVGL ────────────────────────
    ESP_LOGI(TAG, "Rendering calendar UI...");
    CalendarUiData ui_data = build_ui_data(events);

    EpdLvglBridge bridge(800, 480);
    bridge.init();

    lv_obj_t *screen = bridge.get_screen();
    CalendarUi::create(screen, ui_data);

    auto framebuffer = bridge.render();
    ESP_LOGI(TAG, "Framebuffer ready: %u bytes", (unsigned)framebuffer.size());

    // ── 5. Display on EPD ─────────────────────────────────────
    SpiConfig spi_config;
    spi_config.mosi_pin = board::MOSI;
    spi_config.miso_pin = -1;
    spi_config.sclk_pin = board::CLK;
    spi_config.cs_pin = board::EPD_CS;
    spi_config.host_id = 1; // SPI2_HOST
    spi_config.clock_speed_hz = 1 * 1000 * 1000;
    spi_config.max_transfer_size = 800 * 480 / 8;

    Spi spi_device(spi_config);

    FrameSize frame = {800, 480, 1};
    EpdConfig epd_config(frame, spi_device, board::EPD_DC, board::EPD_RST, board::EPD_BUSY);

    Epd7in5V2 display(epd_config);

    ESP_LOGI(TAG, "Initializing EPD...");
    display.init();
    display.clear();
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Showing calendar on EPD...");
    display.show(framebuffer);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // ── 6. Cleanup ────────────────────────────────────────────
    bridge.deinit();

    ESP_LOGI(TAG, "Putting EPD to sleep...");
    display.init();
    display.clear();
    display.sleep();

    // Power off V33_2 rail
    power.write(false);
    ESP_LOGI(TAG, "V33_2 rail powered off");

    // Disconnect WiFi
    wifi.disconnect();
    ESP_LOGI(TAG, "WiFi disconnected");

    // TODO: Enter deep sleep with 24-hour RTC timer
    // esp_sleep_enable_timer_wakeup(24 * 60 * 60 * 1000000ULL);
    // esp_deep_sleep_start();
    ESP_LOGI(TAG, "All done. (Deep sleep not yet implemented)");

    // Temporary: idle loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
