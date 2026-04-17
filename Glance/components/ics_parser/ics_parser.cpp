#include "ics_parser.hpp"
#include "esp_log.h"
#include <algorithm>
#include <cstring>

static const char *TAG = "IcsParser";

void IcsEvent::print() const {
    ESP_LOGI(TAG, "Event: %s", summary.c_str());
    ESP_LOGI(TAG, "  Start:    %s", start_time.c_str());
    ESP_LOGI(TAG, "  End:      %s", end_time.c_str());
    ESP_LOGI(TAG, "  Location: %s", location.c_str());
}

time_t IcsEvent::get_start_time_t() const {
    if (start_time.length() < 8) return 0;

    struct tm tm_time;
    memset(&tm_time, 0, sizeof(struct tm));

    // Basic ISO 8601 parsing: YYYYMMDDTHHMMSS
    // Format: 20260417T100000Z
    int year, month, day;
    if (sscanf(start_time.substr(0, 8).c_str(), "%4d%2d%2d", &year, &month, &day) != 3) {
        return 0;
    }

    tm_time.tm_year = year - 1900;
    tm_time.tm_mon = month - 1;
    tm_time.tm_mday = day;
    
    if (start_time.length() >= 15 && start_time[8] == 'T') {
        int hour, min, sec;
        if (sscanf(start_time.substr(9, 6).c_str(), "%2d%2d%2d", &hour, &min, &sec) == 3) {
            tm_time.tm_hour = hour;
            tm_time.tm_min = min;
            tm_time.tm_sec = sec;
        }
    }

    // mktime converts to time_t using local timezone settings
    return mktime(&tm_time);
}

IcsParser::IcsParser(size_t max) : max_events(max), in_event(false) {
}

IcsParser::~IcsParser() {
}

void IcsParser::clear() {
    while(!event_heap.empty()) event_heap.pop();
    in_event = false;
    unfolded_line.clear();
}

void IcsParser::parse_line(const std::string& line) {
    if (line.empty()) return;

    if (line[0] == ' ' || line[0] == '\t') {
        unfolded_line += line.substr(1);
    } else {
        if (!unfolded_line.empty()) {
            process_unfolded_line(unfolded_line);
        }
        unfolded_line = line;
    }
}

void IcsParser::flush() {
    if (!unfolded_line.empty()) {
        process_unfolded_line(unfolded_line);
        unfolded_line.clear();
    }
}

void IcsParser::process_unfolded_line(const std::string& line) {
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos) return;

    std::string key = line.substr(0, colon_pos);
    std::string value = line.substr(colon_pos + 1);

    size_t semi_pos = key.find(';');
    std::string pure_key = (semi_pos != std::string::npos) ? key.substr(0, semi_pos) : key;

    if (pure_key == "BEGIN" && value == "VEVENT") {
        in_event = true;
        current_event = IcsEvent();
    } else if (pure_key == "END" && value == "VEVENT") {
        if (in_event) {
            add_event_to_heap(current_event);
            in_event = false;
        }
    } else if (in_event) {
        if (pure_key == "SUMMARY") {
            current_event.summary = value;
        } else if (pure_key == "DTSTART") {
            current_event.start_time = value;
        } else if (pure_key == "DTEND") {
            current_event.end_time = value;
        } else if (pure_key == "LOCATION") {
            current_event.location = value;
        }
    }
}

void IcsParser::add_event_to_heap(const IcsEvent& event) {
    time_t now = time(nullptr);
    time_t event_time = event.get_start_time_t();

    // Filter: ignore past events
    if (event_time < now) {
        return;
    }

    if (event_heap.size() < max_events) {
        event_heap.push(event);
    } else {
        // If heap is full, compare with the furthest event (top of max heap)
        if (event < event_heap.top()) {
            event_heap.pop();
            event_heap.push(event);
        }
    }
}

std::vector<IcsEvent> IcsParser::get_events() const {
    std::vector<IcsEvent> result;
    std::priority_queue<IcsEvent> temp_heap = event_heap;
    
    while (!temp_heap.empty()) {
        result.push_back(temp_heap.top());
        temp_heap.pop();
    }
    
    // Result is currently latest to earliest (because it's a max heap)
    // Reverse it to get earliest to latest
    std::reverse(result.begin(), result.end());
    return result;
}

void IcsParser::print_events() const {
    std::vector<IcsEvent> sorted_events = get_events();
    ESP_LOGI(TAG, "Total events in memory (closest %u): %u", (unsigned int)max_events, (unsigned int)sorted_events.size());
    for (const auto& event : sorted_events) {
        event.print();
    }
}
