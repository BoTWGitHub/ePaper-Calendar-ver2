#pragma once

#include <string>
#include <functional>
#include "esp_err.h"
#include "esp_http_client.h"

class HttpClient {
public:
    using LineCallback = std::function<void(const std::string&)>;

    HttpClient();
    ~HttpClient();

    // Fetch data from the specified URL
    esp_err_t fetch(const std::string& url);

    // Set callback for each line received
    void set_on_line_received(LineCallback callback) { on_line_received = callback; }

private:
    static esp_err_t http_event_handler(esp_http_client_event_t *evt);
    esp_err_t handle_event(esp_http_client_event_t *evt);

    void process_data_chunk(const char* data, int len);

    static constexpr size_t LINE_BUFFER_SIZE = 512;
    char line_buffer[LINE_BUFFER_SIZE];
    size_t line_buffer_pos;
    LineCallback on_line_received;
};
