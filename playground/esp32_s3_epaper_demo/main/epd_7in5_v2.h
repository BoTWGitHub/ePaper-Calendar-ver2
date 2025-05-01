#ifndef _EPD_7IN5_V2_H_
#define _EPD_7IN5_V2_H_

#include <inttypes.h>

// Display resolution
#define EPD_7IN5_V2_WIDTH       800
#define EPD_7IN5_V2_HEIGHT      480

void epd_7in5_v2_init(void);
void epd_7in5_v2_clear(void);
void epd_7in5_v2_clearblack(void);
void epd_7in5_v2_display(uint8_t *image);
void epd_7in5_v2_display_part(uint8_t *blackimage,uint32_t x_start, uint32_t y_start, uint32_t x_end, uint32_t y_end);
void epd_7in5_v2_sleep(void);

#endif