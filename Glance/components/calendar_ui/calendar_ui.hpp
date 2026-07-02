#pragma once

#include <string>
#include <vector>
#include "lvgl.h"

/// Display data for a single calendar event
struct EventDisplay {
    std::string time_str;    // e.g. "14:00 - 15:30" or "All Day"
    std::string title;
    bool is_all_day;
};

/// All data needed to render the calendar UI
struct CalendarUiData {
    std::string date_str;        // e.g. "Jun 28"
    std::string weekday_str;     // e.g. "Saturday"
    std::string year_str;        // e.g. "2026"
    std::vector<EventDisplay> events;
};

/**
 * @brief Creates the calendar UI layout on an LVGL parent object.
 *
 * Layout (800x480):
 * ┌─────────────────────────────────────────┐
 * │  Jun 28                           2026  │  Header
 * │  Saturday                               │
 * ├─────────────────────────────────────────┤
 * │  14:00 - 15:30  │ Team Meeting          │  Event rows
 * │  16:00 - 17:00  │ Code Review           │
 * │  All Day        │ Company Holiday       │
 * │  ...                                    │
 * ├─────────────────────────────────────────┤
 * │                                         │  Footer (reserved)
 * └─────────────────────────────────────────┘
 */
class CalendarUi {
public:
    /**
     * @brief Build the calendar UI on the given parent.
     * @param parent  The LVGL object to add widgets to (typically the active screen).
     * @param data    The calendar data to display.
     */
    static void create(lv_obj_t *parent, const CalendarUiData &data);

private:
    static void create_header(lv_obj_t *parent, const CalendarUiData &data);
    static void create_divider(lv_obj_t *parent);
    static void create_event_list(lv_obj_t *parent, const std::vector<EventDisplay> &events);
    static void create_event_row(lv_obj_t *parent, const EventDisplay &event);
    static void create_no_events_message(lv_obj_t *parent);
};
