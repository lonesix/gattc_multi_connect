#pragma once

#include "lvgl.h"
#include "demos/lv_demos.h"

#include "LVGL_Driver.h"
#include "SD_MMC.h"
#include "Wireless.h"

#define EXAMPLE1_LVGL_TICK_PERIOD_MS  1000
#define Green_led lv_color_hex(0x00FF00) 
#define Red_led lv_color_hex(0xFF0000) 

void lvgl_task(void *pvParameters);
void Lvgl_Example1(void);
void change_led_color(lv_obj_t* led,lv_color_t color);
void ui_acquire(void);
void ui_release(void);