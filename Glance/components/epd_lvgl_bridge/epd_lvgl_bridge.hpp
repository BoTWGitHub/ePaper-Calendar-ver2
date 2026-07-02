#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include "lvgl.h"

/**
 * @brief Bridge between LVGL and the EPD framebuffer.
 *
 * This class initializes LVGL in a "one-shot render" mode suitable for E-Ink
 * displays. Instead of running a continuous refresh loop, the caller builds
 * a UI on the LVGL screen, calls render() to produce a monochrome framebuffer,
 * and then passes that buffer to the EPD driver.
 *
 * Usage:
 *   EpdLvglBridge bridge(800, 480);
 *   bridge.init();
 *   lv_obj_t *screen = bridge.get_screen();
 *   // ... build LVGL widgets on screen ...
 *   auto fb = bridge.render();
 *   epd.show(fb);
 *   bridge.deinit();
 */
class EpdLvglBridge {
public:
    EpdLvglBridge(uint32_t width, uint32_t height);
    ~EpdLvglBridge();

    /// Initialize LVGL and create the monochrome display
    void init();

    /// Deinitialize LVGL and free resources
    void deinit();

    /// Get the active LVGL screen to build UI on
    lv_obj_t* get_screen() const;

    /**
     * @brief Trigger a one-shot LVGL render and return the framebuffer.
     *
     * Calls lv_timer_handler() repeatedly until the full screen is rendered
     * into the internal framebuffer.
     *
     * @return A span pointing to the rendered monochrome bitmap (1bpp, packed).
     *         The data is valid until deinit() is called.
     *         The size is (width * height / 8) bytes.
     */
    std::span<const uint8_t> render();

    EpdLvglBridge(const EpdLvglBridge&) = delete;
    EpdLvglBridge& operator=(const EpdLvglBridge&) = delete;

private:
    static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
    static uint32_t tick_cb();

    uint32_t width;
    uint32_t height;
    size_t framebuffer_size;   // width * height / 8
    uint8_t *framebuffer;      // Full-screen framebuffer for EPD
    uint8_t *render_buf;       // LVGL render buffer (includes 8-byte palette prefix)
    lv_display_t *display;
    bool initialized;
    bool render_complete;
};
