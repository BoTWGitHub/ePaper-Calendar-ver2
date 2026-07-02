#include "epd_lvgl_bridge.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char *TAG = "EpdLvglBridge";

// LVGL I1 format prepends 8 bytes of palette data to the render buffer.
static constexpr size_t LVGL_I1_PALETTE_SIZE = 8;

EpdLvglBridge::EpdLvglBridge(uint32_t w, uint32_t h)
    : width(w), height(h),
      framebuffer_size(w * h / 8),
      framebuffer(nullptr),
      render_buf(nullptr),
      display(nullptr),
      initialized(false),
      render_complete(false)
{
}

EpdLvglBridge::~EpdLvglBridge() {
    if (initialized) {
        deinit();
    }
}

uint32_t EpdLvglBridge::tick_cb() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void EpdLvglBridge::init() {
    if (initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return;
    }

    ESP_LOGI(TAG, "Initializing LVGL for %lux%lu monochrome display", 
             (unsigned long)width, (unsigned long)height);

    // Allocate framebuffer (final output for EPD, no palette prefix)
    framebuffer = new uint8_t[framebuffer_size];
    memset(framebuffer, 0xFF, framebuffer_size); // White background

    // Allocate LVGL render buffer (full-screen + 8 bytes palette overhead)
    size_t render_buf_size = framebuffer_size + LVGL_I1_PALETTE_SIZE;
    render_buf = new uint8_t[render_buf_size];

    // Initialize LVGL
    lv_init();
    lv_tick_set_cb(tick_cb);

    // Create display
    display = lv_display_create(width, height);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);
    lv_display_set_flush_cb(display, flush_cb);
    lv_display_set_user_data(display, this);

    // Use FULL render mode: LVGL renders the entire screen in one flush call.
    // This is ideal for e-ink since we need the complete framebuffer anyway.
    lv_display_set_buffers(display, render_buf, nullptr, 
                           render_buf_size, LV_DISPLAY_RENDER_MODE_FULL);

    // Apply simple theme (suitable for monochrome displays)
    lv_theme_t *theme = lv_theme_simple_init(display);
    lv_display_set_theme(display, theme);

    initialized = true;
    ESP_LOGI(TAG, "LVGL initialized (framebuffer: %u bytes)", (unsigned)framebuffer_size);
}

void EpdLvglBridge::deinit() {
    if (!initialized) return;

    if (display) {
        lv_display_delete(display);
        display = nullptr;
    }

    lv_deinit();

    delete[] framebuffer;
    framebuffer = nullptr;
    delete[] render_buf;
    render_buf = nullptr;

    initialized = false;
    ESP_LOGI(TAG, "LVGL deinitialized");
}

lv_obj_t* EpdLvglBridge::get_screen() const {
    if (!display) return nullptr;
    return lv_display_get_screen_active(display);
}

void EpdLvglBridge::flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    EpdLvglBridge *self = static_cast<EpdLvglBridge*>(lv_display_get_user_data(disp));
    if (!self) {
        lv_display_flush_ready(disp);
        return;
    }

    // Skip the 8-byte palette header that LVGL prepends for I1 format
    uint8_t *pixels = px_map + LVGL_I1_PALETTE_SIZE;

    // In FULL render mode, the area should cover the entire screen.
    // Copy the rendered pixel data directly to our framebuffer.
    uint32_t area_w = area->x2 - area->x1 + 1;
    uint32_t area_h = area->y2 - area->y1 + 1;

    if (area_w == self->width && area_h == self->height) {
        // Full screen flush — direct copy
        memcpy(self->framebuffer, pixels, self->framebuffer_size);
    } else {
        // Partial area flush (shouldn't happen in FULL mode, but handle it)
        uint32_t src_stride = (area_w + 7) / 8;
        uint32_t dst_stride = self->width / 8;

        for (uint32_t y = 0; y < area_h; y++) {
            uint32_t dst_y = area->y1 + y;
            // For partial copy, we need bit-level addressing.
            // Since width is 800 (divisible by 8) and we expect full-screen,
            // this simplified byte-copy works when x1 is byte-aligned.
            uint32_t dst_byte_offset = dst_y * dst_stride + (area->x1 / 8);
            memcpy(&self->framebuffer[dst_byte_offset],
                   &pixels[y * src_stride],
                   src_stride);
        }
    }

    self->render_complete = true;
    lv_display_flush_ready(disp);
}

std::span<const uint8_t> EpdLvglBridge::render() {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized, cannot render");
        return {};
    }

    render_complete = false;

    // Invalidate the entire screen to force a full redraw
    lv_obj_invalidate(lv_display_get_screen_active(display));

    // Call lv_timer_handler() until the flush callback has been invoked.
    // This is the "one-shot render" approach for e-ink displays.
    int max_iterations = 100;
    while (!render_complete && max_iterations-- > 0) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (!render_complete) {
        ESP_LOGW(TAG, "Render did not complete within expected iterations");
    } else {
        ESP_LOGI(TAG, "Render complete");
    }

    return std::span<const uint8_t>(framebuffer, framebuffer_size);
}
