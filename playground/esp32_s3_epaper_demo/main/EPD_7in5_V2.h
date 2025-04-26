#ifndef _EPD_7IN5_V2_H_
#define _EPD_7IN5_V2_H_

#include <inttypes.h>

// Display resolution
#define EPD_7IN5_V2_WIDTH       800
#define EPD_7IN5_V2_HEIGHT      480

uint8_t EPD_7IN5_V2_Init(void);
uint8_t EPD_7IN5_V2_Init_Fast(void);
uint8_t EPD_7IN5_V2_Init_Part(void);
uint8_t EPD_7IN5_V2_Init_4Gray(void);
void EPD_7IN5_V2_Clear(void);
void EPD_7IN5_V2_ClearBlack(void);
void EPD_7IN5_V2_Display(uint8_t *blackimage);
void EPD_7IN5_V2_Display_Part(uint8_t *blackimage,uint32_t x_start, uint32_t y_start, uint32_t x_end, uint32_t y_end);
void EPD_7IN5_V2_Display_4Gray(const uint8_t *Image);
void EPD_7IN5_V2_WritePicture_4Gray(const uint8_t *Image);
void EPD_7IN5_V2_Sleep(void);

#endif