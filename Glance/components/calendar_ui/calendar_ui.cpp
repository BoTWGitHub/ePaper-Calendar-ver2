#include "calendar_ui.hpp"
#include "fonts.hpp"
#include "esp_log.h"

static const char *TAG = "CalendarUi";

// -------------------------------------------------------------------
// Font setup
//
// The Noto Sans TC fonts include both ASCII and CJK characters,
// so they're used directly for text fields that may contain Chinese.
// Montserrat is used for English-only fields (date, year) where
// its proportional metrics look better at large sizes.
// -------------------------------------------------------------------
static const lv_font_t *font_date    = &lv_font_montserrat_48;  // "Jun 28" (English only)
static const lv_font_t *font_weekday = &font_noto_tc_24;        // May contain CJK
static const lv_font_t *font_year    = &lv_font_montserrat_24;  // "2026" (digits only)
static const lv_font_t *font_time    = &lv_font_montserrat_20;  // "14:00" (digits only)
static const lv_font_t *font_title   = &font_noto_tc_20;        // Event title (CJK)
static const lv_font_t *font_empty   = &font_noto_tc_20;        // "No upcoming events"

// Layout constants
static constexpr int SCREEN_PAD      = 20;
static constexpr int HEADER_GAP      = 4;
static constexpr int EVENT_ROW_GAP   = 6;
static constexpr int TIME_COL_WIDTH  = 200;
static constexpr int DIVIDER_HEIGHT  = 2;

// -------------------------------------------------------------------
// Public
// -------------------------------------------------------------------

void CalendarUi::create(lv_obj_t *parent, const CalendarUiData &data) {
    ESP_LOGI(TAG, "Creating calendar UI: %s %s %s, %u events",
             data.weekday_str.c_str(), data.date_str.c_str(),
             data.year_str.c_str(), (unsigned)data.events.size());

    // Root container: fills the screen, column layout, no scrollbars
    lv_obj_set_size(parent, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(parent, SCREEN_PAD, 0);
    lv_obj_set_style_pad_row(parent, 10, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // Remove default border/outline from the screen object
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_outline_width(parent, 0, 0);

    create_header(parent, data);
    create_divider(parent);

    if (data.events.empty()) {
        create_no_events_message(parent);
    } else {
        create_event_list(parent, data.events);
    }
}

// -------------------------------------------------------------------
// Header: date + weekday + year
// -------------------------------------------------------------------

void CalendarUi::create_header(lv_obj_t *parent, const CalendarUiData &data) {
    // Top row: date (left) and year (right)
    lv_obj_t *top_row = lv_obj_create(parent);
    lv_obj_set_size(top_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN, 
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);

    // Date label (large)
    lv_obj_t *date_label = lv_label_create(top_row);
    lv_label_set_text(date_label, data.date_str.c_str());
    lv_obj_set_style_text_font(date_label, font_date, 0);

    // Year label (right-aligned)
    lv_obj_t *year_label = lv_label_create(top_row);
    lv_label_set_text(year_label, data.year_str.c_str());
    lv_obj_set_style_text_font(year_label, font_year, 0);

    // Weekday label (below the date)
    lv_obj_t *weekday_label = lv_label_create(parent);
    lv_label_set_text(weekday_label, data.weekday_str.c_str());
    lv_obj_set_style_text_font(weekday_label, font_weekday, 0);
    lv_obj_set_style_pad_top(weekday_label, HEADER_GAP, 0);
}

// -------------------------------------------------------------------
// Divider line
// -------------------------------------------------------------------

void CalendarUi::create_divider(lv_obj_t *parent) {
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, LV_PCT(100), DIVIDER_HEIGHT);
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
}

// -------------------------------------------------------------------
// Event list
// -------------------------------------------------------------------

void CalendarUi::create_event_list(lv_obj_t *parent, const std::vector<EventDisplay> &events) {
    // Scrollable container for events
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, EVENT_ROW_GAP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    for (const auto &event : events) {
        create_event_row(list, event);
    }
}

void CalendarUi::create_event_row(lv_obj_t *parent, const EventDisplay &event) {
    // Row container
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Time column (fixed width)
    lv_obj_t *time_label = lv_label_create(row);
    lv_label_set_text(time_label, event.time_str.c_str());
    lv_obj_set_style_text_font(time_label, font_time, 0);
    lv_obj_set_width(time_label, TIME_COL_WIDTH);

    // Title column (fills remaining space)
    lv_obj_t *title_label = lv_label_create(row);
    lv_label_set_text(title_label, event.title.c_str());
    lv_obj_set_style_text_font(title_label, font_title, 0);
    lv_obj_set_flex_grow(title_label, 1);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT); // Truncate with "..."
    lv_obj_set_style_max_width(title_label, 500, 0);
}

void CalendarUi::create_no_events_message(lv_obj_t *parent) {
    lv_obj_t *msg = lv_label_create(parent);
    lv_label_set_text(msg, "No upcoming events");
    lv_obj_set_style_text_font(msg, font_empty, 0);
    lv_obj_set_style_pad_top(msg, 20, 0);
}
