#include <stdio.h>
#include "lvgl.h"
#include "device.h"
#include "epd_7in5_v2.h"
#include "esp_timer.h"

static void lv_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

void my_flush_cb(lv_display_t * display, const lv_area_t * area, uint8_t * px_map)
{
    printf("flushing...\n");
    epd_7in5_v2_display(px_map);
    /* IMPORTANT!!!
     * Inform LVGL that flushing is complete so buffer can be modified again. */
    lv_display_flush_ready(display);
}

void app_main(void)
{
    lv_init();
    esp_timer_handle_t tick_timer;
    const esp_timer_create_args_t tick_conf = {
        .callback = lv_tick_cb, .name = "lv_tick"};
    esp_timer_create(&tick_conf, &tick_timer);
    esp_timer_start_periodic(tick_timer, 1000);
    device_init();
    epd_7in5_v2_init();
    lv_display_t * display1 = lv_display_create(EPD_7IN5_V2_WIDTH, EPD_7IN5_V2_HEIGHT);
    lv_display_set_color_format(display1, LV_COLOR_FORMAT_I1);
    static uint8_t framebuf[EPD_7IN5_V2_WIDTH * EPD_7IN5_V2_HEIGHT / 8 + 8];
    lv_display_set_buffers(display1, framebuf, NULL, sizeof(framebuf), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display1, my_flush_cb);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), 0);

    extern const uint8_t subset_font_subset_jf_openhuninn_2_1_ttf[];
    extern const int subset_font_subset_jf_openhuninn_2_1_ttf_len;

    /*Create style with the new font*/
    static lv_style_t style;
    lv_style_init(&style);
    lv_font_t * font_30 = lv_tiny_ttf_create_data(subset_font_subset_jf_openhuninn_2_1_ttf, subset_font_subset_jf_openhuninn_2_1_ttf_len, 30);
    lv_font_t * font_40 = lv_tiny_ttf_create_data(subset_font_subset_jf_openhuninn_2_1_ttf, subset_font_subset_jf_openhuninn_2_1_ttf_len, 40);
    lv_font_t * font_50 = lv_tiny_ttf_create_data(subset_font_subset_jf_openhuninn_2_1_ttf, subset_font_subset_jf_openhuninn_2_1_ttf_len, 50);

    /*Create a label with the new style*/
    // lv_obj_t * label = lv_label_create(lv_screen_active());
    // lv_obj_add_style(label, &style, 0);
    // lv_label_set_text(label, "星期一猴子穿新衣");
    // lv_obj_center(label);

    lv_obj_t *lab30 = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lab30, font_30, 0);
    lv_label_set_text(lab30, "星期一猴子穿新衣");
    lv_obj_align(lab30, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    lv_obj_t *lab40 = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lab40, font_40, 0);
    lv_label_set_text(lab40, "星期一猴子穿新衣");
    lv_obj_align(lab40, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);

    lv_obj_t *lab50 = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lab50, font_50, 0);
    lv_label_set_text(lab50, "星期一猴子穿新衣");
    lv_obj_align(lab50, LV_ALIGN_OUT_BOTTOM_MID, 0, 100);

    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));  // 10 ms
    }
}
