#include "LVGL_Example.h"
static SemaphoreHandle_t xGuiSemaphore;
/**********************
 *      TYPEDEFS
 **********************/
typedef enum {
    DISP_SMALL,
    DISP_MEDIUM,
    DISP_LARGE,
} disp_size_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void Onboard_create(lv_obj_t * parent);

static void ta_event_cb(lv_event_t * e);
void example1_increase_lvgl_tick(lv_timer_t * t);
/**********************
 *  STATIC VARIABLES
 **********************/
static disp_size_t disp_size;

static lv_obj_t * tv;
lv_style_t style_text_muted;
lv_style_t style_title;
static lv_style_t style_icon;
static lv_style_t style_bullet;



static const lv_font_t * font_large;
static const lv_font_t * font_normal;

static lv_timer_t * auto_step_timer;

static lv_timer_t * meter2_timer;

lv_obj_t * SD_Size;
lv_obj_t * FlashSize;
lv_obj_t * Board_angle;
lv_obj_t * RTC_Time;
lv_obj_t * Wireless_Scan;




lv_obj_t * draw_filled_rectangle(lv_obj_t * parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_color_t color);
#include "uart.h"
static lv_obj_t *gif_anim = NULL;
lv_obj_t *ui_screen_main = NULL;
// 定义事件组句柄
extern EventGroupHandle_t xEventGroup;
 
// 定义事件位
#define EVENT_BIT_READY_ON  (1<<0)
#define EVENT_BIT_READY_OFF (1<<1)
//led参数
#define led_x 20


void lvgl_task(void *pvParameters)
{
    xGuiSemaphore = xSemaphoreCreateMutex();

    LVGL_Init();   // returns the screen object
    BK_Light(50);
    Lvgl_Example1();
    
     while (1) {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        
        if(pdTRUE == xSemaphoreTake(xGuiSemaphore,portMAX_DELAY))    
        {
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
        xSemaphoreGive(xGuiSemaphore);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

}
void change_led_color(lv_obj_t* led,lv_color_t color)
{
  ui_acquire();
  lv_led_set_color(led, color); // 设置颜色
  ui_release();
}

void next_frame_task_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    bool active = (bool) event->param;
    static bool is_wakeup = false;
    static bool is_play_finish = false;
    // static uint8_t normal_cnt = NORMAL_EMOJI;

    switch (code)
    {
    case LV_EVENT_READY:
    {
        printf("----gif play finsh----\n");
        // lv_obj_del_delayed(lv_event_get_target(event), 10);
        /* normal loop */
        a.led = draw_filled_rectangle(gif_anim,20,105,15,15,Red_led);
        b.led = draw_filled_rectangle(gif_anim,20,105*2-5-2,15,15,Red_led);
        c.led = draw_filled_rectangle(gif_anim,20,105*3-10-4,15,15,Red_led);
        xEventGroupSetBits(xEventGroup, EVENT_BIT_READY_ON);

        

        break;
    }
    case LV_EVENT_VALUE_CHANGED:
    {
        /* wake up to emoji */
        // ((lv_gif_t *)gif_anim)->gif->loop_count = 1;
        // active == true ? is_wakeup = true : is_wakeup = false;
        break;
    }
    default:
        break;
    }
}

lv_obj_t * draw_filled_rectangle(lv_obj_t * parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_color_t color) {
    // 创建一个矩形对象
    lv_obj_t * rect = lv_led_create(parent); // 如果没有指定父对象，可以传递 NULL，但通常你会希望它有一个父容器
 
    // 设置矩形的位置和大小
    lv_obj_set_pos(rect, x, y);
    lv_obj_set_size(rect, w, h);
 
    // 设置矩形的填充颜色和边框颜色（如果需要边框的话）
    // 注意：在 LVGL 中，如果你只设置了填充颜色而没有设置边框宽度和颜色，那么边框将不会显示

    lv_led_set_brightness(rect, 200); // 例如，设置为中等亮度
 
    // 设置 LED 的颜色（使用十六进制颜色值）
    lv_led_set_color(rect, color); // 设置为绿色
    return rect;

    // lv_obj_set_style_border_color(rect, LV_STATE_DEFAULT, lv_color_hex(0xFF0000)); // 设置边框颜色
 
    // 刷新以显示矩形（通常，LVGL 的事件循环会自动处理刷新）
    // lv_obj_refresh(rect); // 在某些情况下可能需要手动刷新，但通常不需要
}

void Lvgl_Example1(void){

  disp_size = DISP_SMALL;                            

  font_large = LV_FONT_DEFAULT;                             
  font_normal = LV_FONT_DEFAULT;                         
  
  lv_coord_t tab_h;
  tab_h = 45;
  #if LV_FONT_MONTSERRAT_18
    font_large     = &lv_font_montserrat_18;
  #else
    LV_LOG_WARN("LV_FONT_MONTSERRAT_18 is not enabled for the widgets demo. Using LV_FONT_DEFAULT instead.");
  #endif
  #if LV_FONT_MONTSERRAT_12
    font_normal    = &lv_font_montserrat_12;
  #else
    LV_LOG_WARN("LV_FONT_MONTSERRAT_12 is not enabled for the widgets demo. Using LV_FONT_DEFAULT instead.");
  #endif
  
    LV_IMG_DECLARE(kaiji_gif);
    // lv_obj_t *img;
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    gif_anim = lv_gif_create(lv_scr_act());

        /* add event to gif_anim */
    lv_obj_add_event_cb(gif_anim, next_frame_task_cb, LV_EVENT_ALL, NULL);

    lv_gif_set_src(gif_anim, &kaiji_gif);
    lv_obj_set_pos(gif_anim, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    if (gif_anim == NULL) {
    // 处理错误，例如打印错误消息或退出程序
    printf( "gif_anim is NULL\n");
    
    }
    ((lv_gif_t *)gif_anim)->gif->loop_count = 1;



  
  
}

void Lvgl_Example1_close(void)
{
  /*Delete all animation*/
  lv_anim_del(NULL, NULL);

  lv_timer_del(meter2_timer);
  meter2_timer = NULL;

  lv_obj_clean(lv_scr_act());

  lv_style_reset(&style_text_muted);
  lv_style_reset(&style_title);
  lv_style_reset(&style_icon);
  lv_style_reset(&style_bullet);
}


/**********************
*   STATIC FUNCTIONS
**********************/

static void Onboard_create(lv_obj_t * parent)
{

  /*Create a panel*/
  lv_obj_t * panel1 = lv_obj_create(parent);
  lv_obj_set_height(panel1, LV_SIZE_CONTENT);

  lv_obj_t * panel1_title = lv_label_create(panel1);
  lv_label_set_text(panel1_title, "Onboard parameter");
  lv_obj_add_style(panel1_title, &style_title, 0);

  lv_obj_t * SD_label = lv_label_create(panel1);
  lv_label_set_text(SD_label, "SD Card");
  lv_obj_add_style(SD_label, &style_text_muted, 0);

  SD_Size = lv_textarea_create(panel1);
  lv_textarea_set_one_line(SD_Size, true);
  lv_textarea_set_placeholder_text(SD_Size, "SD Size");
  lv_obj_add_event_cb(SD_Size, ta_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_t * Flash_label = lv_label_create(panel1);
  lv_label_set_text(Flash_label, "Flash Size");
  lv_obj_add_style(Flash_label, &style_text_muted, 0);

  FlashSize = lv_textarea_create(panel1);
  lv_textarea_set_one_line(FlashSize, true);
  lv_textarea_set_placeholder_text(FlashSize, "Flash Size");
  lv_obj_add_event_cb(FlashSize, ta_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_t * Wireless_label = lv_label_create(panel1);
  lv_label_set_text(Wireless_label, "Wireless scan");
  lv_obj_add_style(Wireless_label, &style_text_muted, 0);

  Wireless_Scan = lv_textarea_create(panel1);
  lv_textarea_set_one_line(Wireless_Scan, true);
  lv_textarea_set_placeholder_text(Wireless_Scan, "Wireless number");
  lv_obj_add_event_cb(Wireless_Scan, ta_event_cb, LV_EVENT_ALL, NULL);

  // 器件布局
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(parent, grid_main_col_dsc, grid_main_row_dsc);


  /*Create the top panel*/
  static lv_coord_t grid_1_col_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_1_row_dsc[] = {LV_GRID_CONTENT, /*Avatar*/
                                        LV_GRID_CONTENT, /*Name*/
                                        LV_GRID_CONTENT, /*Description*/
                                        LV_GRID_CONTENT, /*Email*/
                                        LV_GRID_CONTENT, /*Phone number*/
                                        LV_GRID_CONTENT, /*Button1*/
                                        LV_GRID_CONTENT, /*Button2*/
                                        LV_GRID_TEMPLATE_LAST
                                        };

  lv_obj_set_grid_dsc_array(panel1, grid_1_col_dsc, grid_1_row_dsc);


  static lv_coord_t grid_2_col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_2_row_dsc[] = {
    LV_GRID_CONTENT,  /*Title*/
    5,                /*Separator*/
    LV_GRID_CONTENT,  /*Box title*/
    40,               /*Box*/
    LV_GRID_CONTENT,  /*Box title*/
    40,               /*Box*/
    LV_GRID_CONTENT,  /*Box title*/
    40,               /*Box*/
    LV_GRID_CONTENT,  /*Box title*/
    40,               /*Box*/
    LV_GRID_CONTENT,  /*Box title*/
    40,               /*Box*/
    LV_GRID_CONTENT,  /*Box title*/
    40,               /*Box*/
    LV_GRID_TEMPLATE_LAST               
  };

  // lv_obj_set_grid_dsc_array(panel2, grid_2_col_dsc, grid_2_row_dsc);
  // lv_obj_set_grid_dsc_array(panel3, grid_2_col_dsc, grid_2_row_dsc);

  lv_obj_set_grid_cell(panel1, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_START, 0, 1);
  lv_obj_set_grid_dsc_array(panel1, grid_2_col_dsc, grid_2_row_dsc);
  lv_obj_set_grid_cell(panel1_title, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(SD_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(SD_Size, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 3, 1);
  lv_obj_set_grid_cell(Flash_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 4, 1);
  lv_obj_set_grid_cell(FlashSize, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 5, 1);
  lv_obj_set_grid_cell(Wireless_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 6, 1);
  lv_obj_set_grid_cell(Wireless_Scan, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 7, 1);

  // 器件布局 END
  
  auto_step_timer = lv_timer_create(example1_increase_lvgl_tick, 100, NULL);
}

void example1_increase_lvgl_tick(lv_timer_t * t)
{
  char buf[100]={0}; 
  
  snprintf(buf, sizeof(buf), "%ld MB\r\n", SDCard_Size);
  lv_textarea_set_placeholder_text(SD_Size, buf);
  snprintf(buf, sizeof(buf), "%ld MB\r\n", Flash_Size);
  lv_textarea_set_placeholder_text(FlashSize, buf);
  if(Scan_finish)
    snprintf(buf, sizeof(buf), "W: %d  B: %d    OK.\r\n",WIFI_NUM,BLE_NUM);
    // snprintf(buf, sizeof(buf), "WIFI: %d     ..OK.\r\n",WIFI_NUM);
  else
    snprintf(buf, sizeof(buf), "W: %d  B: %d\r\n",WIFI_NUM,BLE_NUM);
    // snprintf(buf, sizeof(buf), "WIFI: %d  \r\n",WIFI_NUM);
  lv_textarea_set_placeholder_text(Wireless_Scan, buf);
}

static void ta_event_cb(lv_event_t * e)
{
}

void ui_acquire(void)
{
    
        xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);
    
}

void ui_release(void)
{

        xSemaphoreGive(xGuiSemaphore);
    
}






