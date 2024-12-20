/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */



/****************************************************************************
*
* This file is for gatt client. It can scan ble device, connect multiple devices,
* The gattc_multi_connect demo can connect three ble slaves at the same time.
* Modify the name of gatt_server demo named ESP_GATTS_DEMO_a,
* ESP_GATTS_DEMO_b and ESP_GATTS_DEMO_c,then run three demos,the gattc_multi_connect demo will connect
* the three gatt_server demos, and then exchange data.
* Of course you can also modify the code to connect more devices, we default to connect
* up to 4 devices, more than 4 you need to modify menuconfig.
*
****************************************************************************/

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "uart.h"
#include  <string.h>
#include "esp_system.h"

//微雪组件
#include "ST7789.h"
#include "SD_MMC.h"
#include "RGB.h"
#include "Wireless.h"
#include "LVGL_Example.h"

//button组件
#include "iot_button.h"
#define BOOT_BUTTON_NUM         0

#define BUTTON_ACTIVE_LEVEL     0

// #include "event_groups.h"
const char *GATTC_TAG = "GATTC_MULTIPLE_DEMO";

 
// 定义事件组句柄
EventGroupHandle_t xEventGroup;
 
// 定义事件位
#define EVENT_BIT_READY_ON  (1<<0)
#define EVENT_BIT_READY_OFF (1<<1)


#define CTL 0x0c40//功能订阅标识
#define UploadFrequency     2//上传频率，#数据主动上报的传输帧率[取值0-250HZ], 0表示0.5HZ


#define REMOTE_SERVICE_UUID        0xae30
#define REMOTE_NOTIFY_CHAR_UUID    0xae02

#define Battery_Service 0x180F
#define Battery_Level   0x2A19
/* register three profiles, each profile corresponds to one connection,
   which makes it easy to handle each connection event */
#define PROFILE_NUM 3
#define PROFILE_A_APP_ID 0
#define PROFILE_B_APP_ID 1
#define PROFILE_C_APP_ID 2
#define INVALID_HANDLE   0

void Cmd_RxUnpack(unsigned char *buf, unsigned char Dlen,float *quanshu,int *adcValue,uint8_t *gpioValue);
void Cmd_RxUnpack_A(unsigned char *buf, unsigned char Dlen,float *quanshu,int *adcValue,uint8_t *gpioValue);
/* Declare static functions */
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_a_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_b_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_c_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

static esp_bt_uuid_t remote_filter_service_uuid = {
    .len = ESP_UUID_LEN_16,
        .uuid = {.uuid32 = REMOTE_SERVICE_UUID},
};

static esp_bt_uuid_t remote_filter_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid32 = REMOTE_NOTIFY_CHAR_UUID},
};

static esp_bt_uuid_t remote_battery_service_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid32 = Battery_Service},
};

static esp_bt_uuid_t remote_battery_level_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid32 = Battery_Level},
};

static esp_bt_uuid_t keep_connect_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid32 = 0xae01},
};

static esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = 0x2902,},
};
//设置蓝牙设备工作参数的标志
static uint8_t set_device_a = 0;
static uint8_t set_device_b = 0;
static uint8_t set_device_c = 0;

static float laps_num_a;
static float laps_num_b;
static float laps_num_c;

static bool conn_device_a   = false;
static bool conn_device_b   = false;
static bool conn_device_c   = false;

static bool get_service_a   = false;
static bool get_service_b   = false;
static bool get_service_c   = false;

static bool Isconnecting    = false;
static bool stop_scan_done  = false;

static esp_gattc_char_elem_t  *char_elem_result_a   = NULL;
static esp_gattc_descr_elem_t *descr_elem_result_a  = NULL;
static esp_gattc_char_elem_t  *char_elem_result_b   = NULL;
static esp_gattc_descr_elem_t *descr_elem_result_b  = NULL;
static esp_gattc_char_elem_t  *char_elem_result_c   = NULL;
static esp_gattc_descr_elem_t *descr_elem_result_c  = NULL;

static const char remote_device_name[3][30] = {"NodeA-20241101-V3.11","NodeB-20241101-V3.11", "NodeC-20241101-V3.11"};

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t char_handle;
    esp_bd_addr_t remote_bda;
};

/* One gatt-based profile one app_id and one gattc_if, this array will store the gattc_if returned by ESP_GATTS_REG_EVT */
static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = gattc_profile_a_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
    [PROFILE_B_APP_ID] = {
        .gattc_cb = gattc_profile_b_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
    [PROFILE_C_APP_ID] = {
        .gattc_cb = gattc_profile_c_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },

};

static void start_scan(void)
{
    stop_scan_done = false;
    Isconnecting = false;
    uint32_t duration = 30;
    esp_ble_gap_start_scanning(duration);
}
static void gattc_profile_a_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{   
    const char *GATTC_TAG = "GATTC_MULTIPLE_DEMO--Device-a";
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event) {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(GATTC_TAG, "REG_EVT");
        esp_err_t scan_ret = esp_ble_gap_set_scan_params(&ble_scan_params);
        if (scan_ret){
            ESP_LOGE(GATTC_TAG, "set scan params error, error code = %x", scan_ret);
        }
        break;
    /* one device connect successfully, all profiles callback function will get the ESP_GATTC_CONNECT_EVT,
     so must compare the mac address to check which device is connected, so it is a good choice to use ESP_GATTC_OPEN_EVT. */
    case ESP_GATTC_CONNECT_EVT:
        break;
    case ESP_GATTC_OPEN_EVT:
        if (p_data->open.status != ESP_GATT_OK){
            //open failed, ignore the first device, connect the second device
            ESP_LOGE(GATTC_TAG, "connect device failed, status %d", p_data->open.status);
            conn_device_a = false;
            //start_scan();
            break;
        }
        memcpy(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, p_data->open.remote_bda, 6);
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = p_data->open.conn_id;
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_OPEN_EVT conn_id %d, if %d, status %d, mtu %d", p_data->open.conn_id, gattc_if, p_data->open.status, p_data->open.mtu);
        ESP_LOGI(GATTC_TAG, "REMOTE BDA:");
        esp_log_buffer_hex(GATTC_TAG, p_data->open.remote_bda, sizeof(esp_bd_addr_t));
        esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req (gattc_if, p_data->open.conn_id);
        if (mtu_ret){
            ESP_LOGE(GATTC_TAG, "config MTU error, error code = %x", mtu_ret);
        }
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        if (param->cfg_mtu.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG,"Config mtu failed");
        }
        ESP_LOGI(GATTC_TAG, "Status %d, MTU %d, conn_id %d", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote_filter_service_uuid);
        break;
    case ESP_GATTC_SEARCH_RES_EVT: {
        ESP_LOGI(GATTC_TAG, "SEARCH RES: conn_id = %x is primary service %d", p_data->search_res.conn_id, p_data->search_res.is_primary);
        ESP_LOGI(GATTC_TAG, "start handle %d end handle %d current handle value %d", p_data->search_res.start_handle, p_data->search_res.end_handle, p_data->search_res.srvc_id.inst_id);
        if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p_data->search_res.srvc_id.uuid.uuid.uuid16 == REMOTE_SERVICE_UUID) {
            ESP_LOGI(GATTC_TAG, "UUID16: %x", p_data->search_res.srvc_id.uuid.uuid.uuid16);
            get_service_a = true;
            gl_profile_tab[PROFILE_A_APP_ID].service_start_handle = p_data->search_res.start_handle;
            gl_profile_tab[PROFILE_A_APP_ID].service_end_handle = p_data->search_res.end_handle;
        }
        if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p_data->search_res.srvc_id.uuid.uuid.uuid16 == Battery_Service) {
            ESP_LOGI(GATTC_TAG, "UUID16: %x", p_data->search_res.srvc_id.uuid.uuid.uuid16);
            get_service_a = true;
            gl_profile_tab[PROFILE_A_APP_ID].service_start_handle = p_data->search_res.start_handle;
            gl_profile_tab[PROFILE_A_APP_ID].service_end_handle = p_data->search_res.end_handle;
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (p_data->search_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "search service failed, error status = %x", p_data->search_cmpl.status);
            break;
        }
        if (get_service_a){
            uint16_t count = 0;
            esp_gatt_status_t status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                     p_data->search_cmpl.conn_id,
                                                                     ESP_GATT_DB_CHARACTERISTIC,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                                     INVALID_HANDLE,
                                                                     &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
                break;
            }
            if (count > 0) {
                ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
                char_elem_result_a = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
                if (!char_elem_result_a){
                    ESP_LOGE(GATTC_TAG, "gattc no mem");
                    break;
                }else {

                    if (set_device_a ==3)
                    {
                        /****************电量特征值****************** */                
                        status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                                p_data->search_cmpl.conn_id,
                                                                gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                                gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                                remote_battery_level_uuid,
                                                                char_elem_result_a,
                                                                &count);  
                        a.blueTooth_state = true; 
                        change_led_color(a.led,Green_led);
                    }
                    else if(set_device_a != 4)
                    {
                        /****************数据通知特征值****************** */
                        status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                                p_data->search_cmpl.conn_id,
                                                                gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                                gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                                remote_filter_char_uuid,
                                                                char_elem_result_a,
                                                                &count);
                    }
                    else if (set_device_a == 4)
                    {
                        /****************设置工作参数****************** */
                    status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_a,
                                                        &count);
 
                    }

                    if (status != ESP_GATT_OK){
                        ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                        free(char_elem_result_a);
                        char_elem_result_a = NULL;
                        break;
                    }
                    if (set_device_a == 4)
                    {
                                    // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
                        ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
                        for (size_t i = 0; i < count; i++)
                        {
                            ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_a[i].uuid.uuid.uuid16);
                        }
                        gl_profile_tab[PROFILE_A_APP_ID].char_handle = char_elem_result_a[0].char_handle;
                        
                        uint8_t params[3] ;
                        params[0] = 0x51;
                        params[1] = 0xAA ;      //#静止状态加速度阀值
                        params[2] = 0xBB;     //#静止归零速度(单位cm/s) 0:不归零 255:立即归零
                        
                        esp_ble_gattc_write_char( gattc_if,
                                                    gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                    char_elem_result_a[0].char_handle,
                                                    sizeof(params),
                                                    params,
                                                    ESP_GATT_WRITE_TYPE_NO_RSP,
                                                    ESP_GATT_AUTH_REQ_NONE);
                        ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
                        
                    }

                    // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
                    ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
                    for (size_t i = 0; i < count; i++)
                    {
                        ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_a[i].uuid.uuid.uuid16);
                    }
                    
                    /*  Every service have only one char in our 'ESP_GATTS_DEMO' demo, so we used first 'char_elem_result' */
                    if (count > 0 && (char_elem_result_a[0].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY && set_device_a != 4)){
                        gl_profile_tab[PROFILE_A_APP_ID].char_handle = char_elem_result_a[0].char_handle;
                        esp_ble_gattc_register_for_notify (gattc_if, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, char_elem_result_a[0].char_handle);
                        ESP_LOGI("ESP_GATTC_SEARCH_CMPL_EVT","register_for_notify");
                    }
                }
                /* free char_elem_result */
                free(char_elem_result_a);
                char_elem_result_a = NULL;
            }else {
                ESP_LOGE(GATTC_TAG, "no char found");
            }
        }
        break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (p_data->reg_for_notify.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "reg notify failed, error status =%x", p_data->reg_for_notify.status);
            break;
        }
        uint16_t count = 0;
        uint16_t notify_en = 1;
        esp_gatt_status_t ret_status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                     ESP_GATT_DB_DESCRIPTOR,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                                                     &count);
        if (ret_status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
        }
        if (count > 0){
            descr_elem_result_a = (esp_gattc_descr_elem_t *)malloc(sizeof(esp_gattc_descr_elem_t) * count);
            if (!descr_elem_result_a){
                ESP_LOGE(GATTC_TAG, "malloc error, gattc no mem");
            }else{
                
                ret_status = esp_ble_gattc_get_descr_by_char_handle( gattc_if,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                     p_data->reg_for_notify.handle,
                                                                     notify_descr_uuid,
                                                                     descr_elem_result_a,
                                                                     &count);    
                if (ret_status != ESP_GATT_OK){
                    ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_descr_by_char_handle error");
                }
                ESP_LOGW(GATTC_TAG, "DESCRIPTOR count %d",count);
                ESP_LOGW(GATTC_TAG, "DESCRIPTOR uuid 0x%x",descr_elem_result_a[0].uuid.uuid.uuid16);
                /* Every char has only one descriptor in our 'ESP_GATTS_DEMO' demo, so we used first 'descr_elem_result' */
                if (count > 0 && descr_elem_result_a[0].uuid.len == ESP_UUID_LEN_16 && descr_elem_result_a[0].uuid.uuid.uuid16 == 0x2902){
                    ret_status = esp_ble_gattc_write_char_descr( gattc_if,
                                                                 gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                 descr_elem_result_a[0].handle,
                                                                 sizeof(notify_en),
                                                                 (uint8_t *)&notify_en,
                                                                 ESP_GATT_WRITE_TYPE_RSP,
                                                                 ESP_GATT_AUTH_REQ_NONE);
                }

                if (ret_status != ESP_GATT_OK){
                    ESP_LOGE(GATTC_TAG, "esp_ble_gattc_write_char_descr error");
                }

                /* free descr_elem_result */
                free(descr_elem_result_a);


            }
        }
        else{
            ESP_LOGE(GATTC_TAG, "decsr not found");
        }
        break;
    }
    case ESP_GATTC_NOTIFY_EVT:
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_NOTIFY_EVT, Receive notify value:");
        esp_log_buffer_hex(GATTC_TAG, p_data->notify.value, p_data->notify.value_len);
        if (p_data->notify.value_len == 1 )
        {
            if (p_data->notify.value[0] != 0x29 && p_data->notify.value[0] != 0x12 && p_data->notify.value[0] != 0x51)
            {
                // a.battery_level = p_data->notify.value[0];
                // ESP_LOGI(GATTC_TAG,"电量:%d",a.battery_level);
            }else
            {
                
            }
            
            


        }
        else
        {
            Cmd_RxUnpack_A(p_data->notify.value,p_data->notify.value_len,&a.circles,&a.adc_value,&a.gpio_value);
            ESP_LOGI(GATTC_TAG,"圈数：%.3f,adc:%d,gpio:%d",a.circles,a.adc_value,a.gpio_value);
            ESP_LOGI(GATTC_TAG,"蓝牙连接状态：%d,阀门状态%d",a.blueTooth_state,a.state);
        }
        
        
        break;
    case ESP_GATTC_WRITE_DESCR_EVT:
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "write descr failed, error status = %x", p_data->write.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "write descr success");
        if (set_device_a == 0)
        {
        uint16_t count = 2;
        esp_gatt_status_t status;
        char_elem_result_a = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result_a){
            ESP_LOGE(GATTC_TAG, "gattc no mem");
            break;
        }else{
            /****************保持连接特征值****************** */
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_a,
                                                        &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result_a);
                char_elem_result_a = NULL;
                break;
            }
            // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
            ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
            for (size_t i = 0; i < count; i++)
            {
                ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_a[i].uuid.uuid.uuid16);
            }
            gl_profile_tab[PROFILE_A_APP_ID].char_handle = char_elem_result_a[0].char_handle;
            //每次连接模块后，要尽快发送1次保持蓝牙连接指令(即0x29指令)，否则模块会在30秒后自动断开连接，
            //发送数据注意用write no response方式。
            uint8_t write_char_data[] = {0x29};
            esp_ble_gattc_write_char( gattc_if,
                                        gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                        char_elem_result_a[0].char_handle,
                                        sizeof(write_char_data),
                                        write_char_data,
                                        ESP_GATT_WRITE_TYPE_NO_RSP,
                                        ESP_GATT_AUTH_REQ_NONE);
            ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
            /* free char_elem_result */
                free(char_elem_result_a);
                char_elem_result_a = NULL;                           
            /****************保持连接特征值****************** */
        }
        }

        break;
    case ESP_GATTC_WRITE_CHAR_EVT:
    {
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "write char failed, error status = %x", p_data->write.status);
        }else{
            ESP_LOGI(GATTC_TAG, "write char success");
        }
        if (set_device_a == 3)
        {
            // start_scan();
            a.blueTooth_state = true; 
            change_led_color(a.led,Green_led);
            // xEventGroupSetBits(xEventGroup, EVENT_BIT_READY_ON);
        }
        if (set_device_a == 0)
        {
        uint16_t count = 2;
        esp_gatt_status_t status;
        char_elem_result_a = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result_a){
            ESP_LOGE(GATTC_TAG, "gattc no mem");
            break;
        }else{
            /****************保持连接特征值****************** */
            /****************设置工作参数****************** */
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_a,
                                                        &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result_a);
                char_elem_result_a = NULL;
                break;
            }
            // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
            ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
            for (size_t i = 0; i < count; i++)
            {
                ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_a[i].uuid.uuid.uuid16);
            }
            gl_profile_tab[PROFILE_A_APP_ID].char_handle = char_elem_result_a[0].char_handle;
            //每次连接模块后，要尽快发送1次保持蓝牙连接指令(即0x29指令)，否则模块会在30秒后自动断开连接，
            //发送数据注意用write no response方式。
            uint8_t isCompassOn = 0;// #使用磁场融合姿态
            uint8_t barometerFilter = 2;//
            uint16_t Cmd_ReportTag = CTL;// # 功能订阅标识
            uint8_t params[11] ;
            params[0] = 0x12;
            params[1] = 5 ;      //#静止状态加速度阀值
            params[2] = 255;     //#静止归零速度(单位cm/s) 0:不归零 255:立即归零
            params[3] = 0  ;     //#动态归零速度(单位cm/s) 0:不归零
            params[4] = ((barometerFilter&3)<<1) | (isCompassOn&1);   
            params[5] = UploadFrequency;      //#数据主动上报的传输帧率[取值0-250HZ], 0表示0.5HZ
            params[6] = 1 ;      //#陀螺仪滤波系数[取值0-2],数值越大越平稳但实时性越差
            params[7] = 3 ;      //#加速计滤波系数[取值0-4],数值越大越平稳但实时性越差
            params[8] = 5  ;     //#磁力计滤波系数[取值0-9],数值越大越平稳但实时性越差
            params[9] = Cmd_ReportTag&0xff;
            params[10] = (Cmd_ReportTag>>8)&0xff;
            esp_ble_gattc_write_char( gattc_if,
                                        gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                        char_elem_result_a[0].char_handle,
                                        sizeof(params),
                                        params,
                                        ESP_GATT_WRITE_TYPE_NO_RSP,
                                        ESP_GATT_AUTH_REQ_NONE);
            ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
            /* free char_elem_result */
                free(char_elem_result_a);
                char_elem_result_a = NULL;     
            set_device_a = 1;               
            /****************设置工作参数****************** */           
            /****************保持连接特征值****************** */
        }
        }

        if (set_device_a == 1)
        {
        uint16_t count = 2;
        esp_gatt_status_t status;
        char_elem_result_a = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result_a){
            ESP_LOGE(GATTC_TAG, "gattc no mem");
            break;
        }else{
            /****************保持连接特征值****************** */
            /****************设置工作参数****************** */
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_a,
                                                        &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result_a);
                char_elem_result_a = NULL;
                break;
            }
            // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
            ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
            for (size_t i = 0; i < count; i++)
            {
                ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_a[i].uuid.uuid.uuid16);
            }
            gl_profile_tab[PROFILE_A_APP_ID].char_handle = char_elem_result_a[0].char_handle;
            
            uint8_t params[3] ;
            params[0] = 0x51;
            params[1] = 0xAA ;      //#静止状态加速度阀值
            params[2] = 0xBB;     //#静止归零速度(单位cm/s) 0:不归零 255:立即归零
            
            esp_ble_gattc_write_char( gattc_if,
                                        gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                        char_elem_result_a[0].char_handle,
                                        sizeof(params),
                                        params,
                                        ESP_GATT_WRITE_TYPE_NO_RSP,
                                        ESP_GATT_AUTH_REQ_NONE);
            ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
            /* free char_elem_result */
                free(char_elem_result_a);
                char_elem_result_a = NULL;     
            set_device_a = 2;               
            /****************设置工作参数****************** */           
            /****************保持连接特征值****************** */
        }
        }

        if (set_device_a == 2)
        {
            // esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote_battery_service_uuid);


        uint16_t count = 2;
        esp_gatt_status_t status;
        char_elem_result_a = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result_a){
            ESP_LOGE(GATTC_TAG, "gattc no mem");
            break;
        }else{
            /****************保持连接特征值****************** */
            /****************设置工作参数****************** */
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_a,
                                                        &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result_a);
                char_elem_result_a = NULL;
                break;
            }
            // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
            ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
            for (size_t i = 0; i < count; i++)
            {
                ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_a[i].uuid.uuid.uuid16);
            }
            gl_profile_tab[PROFILE_A_APP_ID].char_handle = char_elem_result_a[0].char_handle;
            
            
            uint8_t params[2] ;
            params[0] = 0x27;
            params[1] = 0x10 ;      //#静止状态加速度阀值
            
            esp_ble_gattc_write_char( gattc_if,
                                        gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                        char_elem_result_a[0].char_handle,
                                        sizeof(params),
                                        params,
                                        ESP_GATT_WRITE_TYPE_NO_RSP,
                                        ESP_GATT_AUTH_REQ_NONE);
            ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
            /* free char_elem_result */
                free(char_elem_result_a);
                char_elem_result_a = NULL;  
            set_device_a = 3;
        }
        }
    }
        break;
    case ESP_GATTC_SRVC_CHG_EVT: {
        esp_bd_addr_t bda;
        memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:%08x%04x",(bda[0] << 24) + (bda[1] << 16) + (bda[2] << 8) + bda[3],
                 (bda[4] << 8) + bda[5]);
        break;
    }
    case ESP_GATTC_DISCONNECT_EVT:
        //Start scanning again
        start_scan();
        if (memcmp(p_data->disconnect.remote_bda, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, 6) == 0){
            ESP_LOGI(GATTC_TAG, "device a disconnect");
            conn_device_a = false;
            get_service_a = false;
            set_device_a = 0;
            a.blueTooth_state = false;
            change_led_color(a.led,Red_led);
        }
        break;
    default:
        break;
    }
}

static void gattc_profile_b_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    const char *GATTC_TAG = "GATTC_MULTIPLE_DEMO--Device-b";
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event) {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(GATTC_TAG, "REG_EVT");
        break;
    case ESP_GATTC_CONNECT_EVT:
        break;
    case ESP_GATTC_OPEN_EVT:
        if (p_data->open.status != ESP_GATT_OK){
            //open failed, ignore the second device, connect the third device
            ESP_LOGE(GATTC_TAG, "connect device failed, status %d", p_data->open.status);
            conn_device_b = false;
            //start_scan();
            break;
        }
        memcpy(gl_profile_tab[PROFILE_B_APP_ID].remote_bda, p_data->open.remote_bda, 6);
        gl_profile_tab[PROFILE_B_APP_ID].conn_id = p_data->open.conn_id;
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_OPEN_EVT conn_id %d, if %d, status %d, mtu %d", p_data->open.conn_id, gattc_if, p_data->open.status, p_data->open.mtu);
        ESP_LOGI(GATTC_TAG, "REMOTE BDA:");
        esp_log_buffer_hex(GATTC_TAG, p_data->open.remote_bda, sizeof(esp_bd_addr_t));
        esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req (gattc_if, p_data->open.conn_id);
        if (mtu_ret){
            ESP_LOGE(GATTC_TAG, "config MTU error, error code = %x", mtu_ret);
        }
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        if (param->cfg_mtu.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG,"Config mtu failed");
        }
        ESP_LOGI(GATTC_TAG, "Status %d, MTU %d, conn_id %d", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote_filter_service_uuid);
        break;
    case ESP_GATTC_SEARCH_RES_EVT: {
        ESP_LOGI(GATTC_TAG, "SEARCH RES: conn_id = %x is primary service %d", p_data->search_res.conn_id, p_data->search_res.is_primary);
        ESP_LOGI(GATTC_TAG, "start handle %d end handle %d current handle value %d", p_data->search_res.start_handle, p_data->search_res.end_handle, p_data->search_res.srvc_id.inst_id);
        if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p_data->search_res.srvc_id.uuid.uuid.uuid16 == REMOTE_SERVICE_UUID) {
            ESP_LOGI(GATTC_TAG, "UUID16: %x", p_data->search_res.srvc_id.uuid.uuid.uuid16);
            get_service_b = true;
            gl_profile_tab[PROFILE_B_APP_ID].service_start_handle = p_data->search_res.start_handle;
            gl_profile_tab[PROFILE_B_APP_ID].service_end_handle = p_data->search_res.end_handle;
        }
        if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p_data->search_res.srvc_id.uuid.uuid.uuid16 == Battery_Service) {
            ESP_LOGI(GATTC_TAG, "UUID16: %x", p_data->search_res.srvc_id.uuid.uuid.uuid16);
            get_service_b = true;
            gl_profile_tab[PROFILE_B_APP_ID].service_start_handle = p_data->search_res.start_handle;
            gl_profile_tab[PROFILE_B_APP_ID].service_end_handle = p_data->search_res.end_handle;
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (p_data->search_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "search service failed, error status = %x", p_data->search_cmpl.status);
            break;
        }
        if (get_service_b){
            uint16_t count = 0;
            esp_gatt_status_t status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                     p_data->search_cmpl.conn_id,
                                                                     ESP_GATT_DB_CHARACTERISTIC,
                                                                     gl_profile_tab[PROFILE_B_APP_ID].service_start_handle,
                                                                     gl_profile_tab[PROFILE_B_APP_ID].service_end_handle,
                                                                     INVALID_HANDLE,
                                                                     &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
            }

            if (count > 0){
                char_elem_result_b = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
                if (!char_elem_result_b){
                    ESP_LOGE(GATTC_TAG, "gattc no mem");
                    break;
                }else{
                    if (set_device_b ==3)
                    {
                    /****************电量特征值****************** */
                    status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                            p_data->search_cmpl.conn_id,
                                                            gl_profile_tab[PROFILE_B_APP_ID].service_start_handle,
                                                            gl_profile_tab[PROFILE_B_APP_ID].service_end_handle,
                                                            remote_battery_level_uuid,
                                                            char_elem_result_b,
                                                            &count); 
                    b.blueTooth_state = true;   
                    change_led_color(b.led,Green_led);                                       
                    }
                    else if(set_device_b != 4)
                    {
                    /****************数据通知特征值****************** */
                    status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                             p_data->search_cmpl.conn_id,
                                                             gl_profile_tab[PROFILE_B_APP_ID].service_start_handle,
                                                             gl_profile_tab[PROFILE_B_APP_ID].service_end_handle,
                                                             remote_filter_char_uuid,
                                                             char_elem_result_b,
                                                             &count);
                    } else if (set_device_b == 4)
                    {
                        /****************设置工作参数****************** */
                    status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_B_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_B_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_b,
                                                        &count);
 
                    }


                    if (status != ESP_GATT_OK){
                        ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                        free(char_elem_result_b);
                        char_elem_result_b = NULL;
                        break;
                    }
                    if (set_device_b == 4)
                    {
                                    // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
                        ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
                        for (size_t i = 0; i < count; i++)
                        {
                            ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_b[i].uuid.uuid.uuid16);
                        }
                        gl_profile_tab[PROFILE_B_APP_ID].char_handle = char_elem_result_b[0].char_handle;
                        
                        uint8_t params[3] ;
                        params[0] = 0x51;
                        params[1] = 0xAA ;      //#静止状态加速度阀值
                        params[2] = 0xBB;     //#静止归零速度(单位cm/s) 0:不归零 255:立即归零
                        
                        esp_ble_gattc_write_char( gattc_if,
                                                    gl_profile_tab[PROFILE_B_APP_ID].conn_id,
                                                    char_elem_result_b[0].char_handle,
                                                    sizeof(params),
                                                    params,
                                                    ESP_GATT_WRITE_TYPE_NO_RSP,
                                                    ESP_GATT_AUTH_REQ_NONE);
                        ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
                        
                    }
                    // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
                    ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
                    for (size_t i = 0; i < count; i++)
                    {
                        ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_b[i].uuid.uuid.uuid16);
                    }

                    /*  Every service have only one char in our 'ESP_GATTS_DEMO' demo, so we used first 'char_elem_result' */
                    if (count > 0 && (char_elem_result_b[0].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY && set_device_b != 4)){
                        gl_profile_tab[PROFILE_B_APP_ID].char_handle = char_elem_result_b[0].char_handle;
                        esp_ble_gattc_register_for_notify (gattc_if, gl_profile_tab[PROFILE_B_APP_ID].remote_bda, char_elem_result_b[0].char_handle);
                    }
                }
                /* free char_elem_result */
                free(char_elem_result_b);
                char_elem_result_b = NULL;
            }else{
                ESP_LOGE(GATTC_TAG, "no char found");
            }
        }
        break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {

        if (p_data->reg_for_notify.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "reg notify failed, error status =%x", p_data->reg_for_notify.status);
            break;
        }
        uint16_t count = 0;
        uint16_t notify_en = 1;
        esp_gatt_status_t ret_status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                     gl_profile_tab[PROFILE_B_APP_ID].conn_id,
                                                                     ESP_GATT_DB_DESCRIPTOR,
                                                                     gl_profile_tab[PROFILE_B_APP_ID].service_start_handle,
                                                                     gl_profile_tab[PROFILE_B_APP_ID].service_end_handle,
                                                                     gl_profile_tab[PROFILE_B_APP_ID].char_handle,
                                                                     &count);
        if (ret_status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
        }
        if (count > 0){
            descr_elem_result_b = (esp_gattc_descr_elem_t *)malloc(sizeof(esp_gattc_descr_elem_t) * count);
            if (!descr_elem_result_b){
                ESP_LOGE(GATTC_TAG, "malloc error, gattc no mem");
            }else{
                ret_status = esp_ble_gattc_get_descr_by_char_handle( gattc_if,
                                                                     gl_profile_tab[PROFILE_B_APP_ID].conn_id,
                                                                     p_data->reg_for_notify.handle,
                                                                     notify_descr_uuid,
                                                                     descr_elem_result_b,
                                                                     &count);
                if (ret_status != ESP_GATT_OK){
                    ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_descr_by_char_handle error");
                    free(descr_elem_result_b);
                    descr_elem_result_b = NULL;
                    break;
                }

                /* Every char has only one descriptor in our 'ESP_GATTS_DEMO' demo, so we used first 'descr_elem_result' */
                if (count > 0 && descr_elem_result_b[0].uuid.len == ESP_UUID_LEN_16 && descr_elem_result_b[0].uuid.uuid.uuid16 == 0x2902){
                    ret_status = esp_ble_gattc_write_char_descr( gattc_if,
                                                                 gl_profile_tab[PROFILE_B_APP_ID].conn_id,
                                                                 descr_elem_result_b[0].handle,
                                                                 sizeof(notify_en),
                                                                 (uint8_t *)&notify_en,
                                                                 ESP_GATT_WRITE_TYPE_RSP,
                                                                 ESP_GATT_AUTH_REQ_NONE);
                }

                if (ret_status != ESP_GATT_OK){
                    ESP_LOGE(GATTC_TAG, "esp_ble_gattc_write_char_descr error");
                }

                /* free descr_elem_result */
                free(descr_elem_result_b);
                descr_elem_result_b = NULL;
            }
        }
        else{
            ESP_LOGE(GATTC_TAG, "decsr not found");
        }
        break;
    }
    case ESP_GATTC_NOTIFY_EVT:
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_NOTIFY_EVT, Receive notify value:");
        esp_log_buffer_hex(GATTC_TAG, p_data->notify.value, p_data->notify.value_len);
        if (p_data->notify.value_len < 3)
        {
            // b.battery_level = p_data->notify.value[0];
            // ESP_LOGI(GATTC_TAG,"电量:%d",b.battery_level);
        }
        else
        {
        Cmd_RxUnpack(p_data->notify.value,p_data->notify.value_len,&b.circles,&b.adc_value,&b.gpio_value);
        ESP_LOGI(GATTC_TAG,"圈数：%.3f,adc:%d,gpio:%d",b.circles,b.adc_value,b.gpio_value);
        ESP_LOGI(GATTC_TAG,"蓝牙连接状态：%d,阀门状态%d",b.blueTooth_state,b.state);
        }        

        break;
    case ESP_GATTC_WRITE_DESCR_EVT:
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "write descr failed, error status = %x", p_data->write.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "write descr success");
        if (set_device_b == 0)
        {
        uint16_t count = 2;
        esp_gatt_status_t status;
        char_elem_result_b = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result_b){
            ESP_LOGE(GATTC_TAG, "gattc no mem");
            break;
        }else{
            /****************保持连接特征值****************** */
            /****************保持连接特征值****************** */
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_B_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_B_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_b,
                                                        &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result_b);
                char_elem_result_b = NULL;
                break;
            }
            // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
            ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
            for (size_t i = 0; i < count; i++)
            {
                ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_b[i].uuid.uuid.uuid16);
            }
            gl_profile_tab[PROFILE_B_APP_ID].char_handle = char_elem_result_b[0].char_handle;
            //每次连接模块后，要尽快发送1次保持蓝牙连接指令(即0x29指令)，否则模块会在30秒后自动断开连接，
            //发送数据注意用write no response方式。
            uint8_t write_char_data[] = {0x29};        
        esp_ble_gattc_write_char( gattc_if,
                                  gl_profile_tab[PROFILE_B_APP_ID].conn_id,
                                  gl_profile_tab[PROFILE_B_APP_ID].char_handle,
                                  sizeof(write_char_data),
                                  write_char_data,
                                  ESP_GATT_WRITE_TYPE_NO_RSP,
                                  ESP_GATT_AUTH_REQ_NONE);
                    ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
            /* free char_elem_result */
                free(char_elem_result_b);
                char_elem_result_b = NULL;    
        }
        }
        break;
    case ESP_GATTC_WRITE_CHAR_EVT:
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Write char failed, error status = %x", p_data->write.status);
        }else{
            ESP_LOGI(GATTC_TAG, "Write char success");
        }
        if (set_device_b == 3)
        {
            b.blueTooth_state = true; 
            change_led_color(b.led,Green_led);
        }
        if (set_device_b == 0)
        {
        uint16_t count = 2;
        esp_gatt_status_t status;
        char_elem_result_b = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result_b){
            ESP_LOGE(GATTC_TAG, "gattc no mem");
            break;
        }else{
            /****************保持连接特征值****************** */
            /****************设置工作参数****************** */
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_B_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_B_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_b,
                                                        &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result_b);
                char_elem_result_b = NULL;
                break;
            }
            // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
            ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
            for (size_t i = 0; i < count; i++)
            {
                ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_b[i].uuid.uuid.uuid16);
            }
            gl_profile_tab[PROFILE_B_APP_ID].char_handle = char_elem_result_b[0].char_handle;
            //每次连接模块后，要尽快发送1次保持蓝牙连接指令(即0x29指令)，否则模块会在30秒后自动断开连接，
            //发送数据注意用write no response方式。
            uint8_t isCompassOn = 0;// #使用磁场融合姿态
            uint8_t barometerFilter = 2;//
            uint16_t Cmd_ReportTag = CTL;// # 功能订阅标识
            uint8_t params[11] ;
            params[0] = 0x12;
            params[1] = 5 ;      //#静止状态加速度阀值
            params[2] = 255;     //#静止归零速度(单位cm/s) 0:不归零 255:立即归零
            params[3] = 0  ;     //#动态归零速度(单位cm/s) 0:不归零
            params[4] = ((barometerFilter&3)<<1) | (isCompassOn&1);   
            params[5] = UploadFrequency;      //#数据主动上报的传输帧率[取值0-250HZ], 0表示0.5HZ
            params[6] = 1 ;      //#陀螺仪滤波系数[取值0-2],数值越大越平稳但实时性越差
            params[7] = 3 ;      //#加速计滤波系数[取值0-4],数值越大越平稳但实时性越差
            params[8] = 5  ;     //#磁力计滤波系数[取值0-9],数值越大越平稳但实时性越差
            params[9] = Cmd_ReportTag&0xff;
            params[10] = (Cmd_ReportTag>>8)&0xff;
            esp_ble_gattc_write_char( gattc_if,
                                        gl_profile_tab[PROFILE_B_APP_ID].conn_id,
                                        char_elem_result_b[0].char_handle,
                                        sizeof(params),
                                        params,
                                        ESP_GATT_WRITE_TYPE_NO_RSP,
                                        ESP_GATT_AUTH_REQ_NONE);
            ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
            /* free char_elem_result */
                free(char_elem_result_b);
                char_elem_result_b = NULL;     
            set_device_b = 1;               
            /****************设置工作参数****************** */           
            /****************保持连接特征值****************** */
        }
        }

        if (set_device_b == 1)
        {
        uint16_t count = 2;
        esp_gatt_status_t status;
        char_elem_result_b = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result_b){
            ESP_LOGE(GATTC_TAG, "gattc no mem");
            break;
        }else{
            /****************保持连接特征值****************** */
            /****************设置工作参数****************** */
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_B_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_B_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_b,
                                                        &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result_b);
                char_elem_result_b = NULL;
                break;
            }
            // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
            ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
            for (size_t i = 0; i < count; i++)
            {
                ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_b[i].uuid.uuid.uuid16);
            }
            gl_profile_tab[PROFILE_B_APP_ID].char_handle = char_elem_result_b[0].char_handle;
            
            uint8_t params[3] ;
            params[0] = 0x51;
            params[1] = 0xAA ;      //#静止状态加速度阀值
            params[2] = 0xBB;     //#静止归零速度(单位cm/s) 0:不归零 255:立即归零
            
            esp_ble_gattc_write_char( gattc_if,
                                        gl_profile_tab[PROFILE_B_APP_ID].conn_id,
                                        char_elem_result_b[0].char_handle,
                                        sizeof(params),
                                        params,
                                        ESP_GATT_WRITE_TYPE_NO_RSP,
                                        ESP_GATT_AUTH_REQ_NONE);
            ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
            /* free char_elem_result */
                free(char_elem_result_b);
                char_elem_result_b = NULL;     
            set_device_b = 2;               
            /****************设置工作参数****************** */           
            /****************保持连接特征值****************** */
        }
        }
        if (set_device_b == 2)
        {
            // esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote_battery_service_uuid);


        uint16_t count = 2;
        esp_gatt_status_t status;
        char_elem_result_b = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result_b){
            ESP_LOGE(GATTC_TAG, "gattc no mem");
            break;
        }else{
            /****************保持连接特征值****************** */
            /****************设置工作参数****************** */
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_B_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_B_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_b,
                                                        &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result_b);
                char_elem_result_b = NULL;
                break;
            }
            // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
            ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
            for (size_t i = 0; i < count; i++)
            {
                ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_b[i].uuid.uuid.uuid16);
            }
            gl_profile_tab[PROFILE_B_APP_ID].char_handle = char_elem_result_b[0].char_handle;
            
            
            uint8_t params[2] ;
            params[0] = 0x27;
            params[1] = 0x10 ;      //#静止状态加速度阀值
            
            esp_ble_gattc_write_char( gattc_if,
                                        gl_profile_tab[PROFILE_B_APP_ID].conn_id,
                                        char_elem_result_b[0].char_handle,
                                        sizeof(params),
                                        params,
                                        ESP_GATT_WRITE_TYPE_NO_RSP,
                                        ESP_GATT_AUTH_REQ_NONE);
            ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
            /* free char_elem_result */
                free(char_elem_result_b);
                char_elem_result_b = NULL;  
            set_device_b = 3;
        }
        }
        break;
    case ESP_GATTC_SRVC_CHG_EVT: {
        esp_bd_addr_t bda;
        memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:%08x%04x",(bda[0] << 24) + (bda[1] << 16) + (bda[2] << 8) + bda[3],
                 (bda[4] << 8) + bda[5]);
        break;
    }
    case ESP_GATTC_DISCONNECT_EVT:
        if (memcmp(p_data->disconnect.remote_bda, gl_profile_tab[PROFILE_B_APP_ID].remote_bda, 6) == 0){
            ESP_LOGI(GATTC_TAG, "device b disconnect");
            conn_device_b = false;
            get_service_b = false;
            b.blueTooth_state = false;
            set_device_b = 0;
            change_led_color(b.led,Red_led);
        }
        break;
    default:
        break;
    }
}

static void gattc_profile_c_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    const char *GATTC_TAG = "GATTC_MULTIPLE_DEMO--Device-c";
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event) {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(GATTC_TAG, "REG_EVT");
        break;
    case ESP_GATTC_CONNECT_EVT:
        break;
    case ESP_GATTC_OPEN_EVT:
        if (p_data->open.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "connect device failed, status %d", p_data->open.status);
            conn_device_c = false;
            //start_scan();
            break;
        }
        memcpy(gl_profile_tab[PROFILE_C_APP_ID].remote_bda, p_data->open.remote_bda, 6);
        gl_profile_tab[PROFILE_C_APP_ID].conn_id = p_data->open.conn_id;
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_OPEN_EVT conn_id %d, if %d, status %d, mtu %d", p_data->open.conn_id, gattc_if, p_data->open.status, p_data->open.mtu);
        ESP_LOGI(GATTC_TAG, "REMOTE BDA:");
        esp_log_buffer_hex(GATTC_TAG, p_data->open.remote_bda, sizeof(esp_bd_addr_t));
        esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req (gattc_if, p_data->open.conn_id);
        if (mtu_ret){
            ESP_LOGE(GATTC_TAG, "config MTU error, error code = %x", mtu_ret);
        }
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        if (param->cfg_mtu.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG,"Config mtu failed");
        }
        ESP_LOGI(GATTC_TAG, "Status %d, MTU %d, conn_id %d", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote_filter_service_uuid);
        break;
    case ESP_GATTC_SEARCH_RES_EVT: {
        ESP_LOGI(GATTC_TAG, "SEARCH RES: conn_id = %x is primary service %d", p_data->search_res.conn_id, p_data->search_res.is_primary);
        ESP_LOGI(GATTC_TAG, "start handle %d end handle %d current handle value %d", p_data->search_res.start_handle, p_data->search_res.end_handle, p_data->search_res.srvc_id.inst_id);
        if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p_data->search_res.srvc_id.uuid.uuid.uuid16 == REMOTE_SERVICE_UUID) {
            ESP_LOGI(GATTC_TAG, "UUID16: %x", p_data->search_res.srvc_id.uuid.uuid.uuid16);
            get_service_c = true;
            gl_profile_tab[PROFILE_C_APP_ID].service_start_handle = p_data->search_res.start_handle;
            gl_profile_tab[PROFILE_C_APP_ID].service_end_handle = p_data->search_res.end_handle;
        }
        if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p_data->search_res.srvc_id.uuid.uuid.uuid16 == Battery_Service) {
            ESP_LOGI(GATTC_TAG, "UUID16: %x!", p_data->search_res.srvc_id.uuid.uuid.uuid16);
            get_service_c = true;
            gl_profile_tab[PROFILE_C_APP_ID].service_start_handle = p_data->search_res.start_handle;
            gl_profile_tab[PROFILE_C_APP_ID].service_end_handle = p_data->search_res.end_handle;
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (p_data->search_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "search service failed, error status = %x", p_data->search_cmpl.status);
            break;
        }
        if (get_service_c){
            uint16_t count = 0;
            esp_gatt_status_t status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                     p_data->search_cmpl.conn_id,
                                                                     ESP_GATT_DB_CHARACTERISTIC,
                                                                     gl_profile_tab[PROFILE_C_APP_ID].service_start_handle,
                                                                     gl_profile_tab[PROFILE_C_APP_ID].service_end_handle,
                                                                     INVALID_HANDLE,
                                                                     &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
            }

            if (count > 0){
                char_elem_result_c = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
                if (!char_elem_result_c){
                    ESP_LOGE(GATTC_TAG, "gattc no mem");
                    break;
                }else{
                    if (set_device_c == 3)
                    {
                        // printf("esp_ble_gattc_get_char_by_uuid remote_battery_level_uuid");
                    /****************电量特征值****************** */
                    status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                            p_data->search_cmpl.conn_id,
                                                            gl_profile_tab[PROFILE_C_APP_ID].service_start_handle,
                                                            gl_profile_tab[PROFILE_C_APP_ID].service_end_handle,
                                                            remote_battery_level_uuid,
                                                            char_elem_result_c,
                                                            &count); 
                    c.blueTooth_state = true;
                    change_led_color(c.led,Green_led);
                    }
                    else if(set_device_c != 4)
                    {
                    status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                             p_data->search_cmpl.conn_id,
                                                             gl_profile_tab[PROFILE_C_APP_ID].service_start_handle,
                                                             gl_profile_tab[PROFILE_C_APP_ID].service_end_handle,
                                                             remote_filter_char_uuid,
                                                             char_elem_result_c,
                                                             &count);
                    }
                    else if (set_device_c == 4)
                    {
                        /****************设置工作参数****************** */
                    status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_C_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_C_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_c,
                                                        &count);
 
                    }
                    

                    if (status != ESP_GATT_OK){
                        ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error!%d",set_device_c);
                        free(char_elem_result_c);
                        char_elem_result_c = NULL;
                        break;
                    }
                    if (set_device_c == 4)
                    {
                                    // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
                        ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
                        for (size_t i = 0; i < count; i++)
                        {
                            ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_c[i].uuid.uuid.uuid16);
                        }
                        gl_profile_tab[PROFILE_C_APP_ID].char_handle = char_elem_result_c[0].char_handle;
                        
                        uint8_t params[3] ;
                        params[0] = 0x51;
                        params[1] = 0xAA ;      //#静止状态加速度阀值
                        params[2] = 0xBB;     //#静止归零速度(单位cm/s) 0:不归零 255:立即归零
                        
                        esp_ble_gattc_write_char( gattc_if,
                                                    gl_profile_tab[PROFILE_C_APP_ID].conn_id,
                                                    char_elem_result_c[0].char_handle,
                                                    sizeof(params),
                                                    params,
                                                    ESP_GATT_WRITE_TYPE_NO_RSP,
                                                    ESP_GATT_AUTH_REQ_NONE);
                        ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
                        
                    }
                    
                    /*  Every service have only one char in our 'ESP_GATTS_DEMO' demo, so we used first 'char_elem_result' */
                    if (count > 0 && (char_elem_result_c[0].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) && set_device_c != 4){
                        gl_profile_tab[PROFILE_C_APP_ID].char_handle = char_elem_result_c[0].char_handle;
                        esp_ble_gattc_register_for_notify (gattc_if, gl_profile_tab[PROFILE_C_APP_ID].remote_bda, char_elem_result_c[0].char_handle);
                    }
                }
                /* free char_elem_result */
                free(char_elem_result_c);
                char_elem_result_c = NULL;
            }else{
                ESP_LOGE(GATTC_TAG, "no char found");
            }
        }
        break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (p_data->reg_for_notify.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "reg notify failed, error status =%x", p_data->reg_for_notify.status);
            break;
        }
        uint16_t count = 0;
        uint16_t notify_en = 1;
        esp_gatt_status_t ret_status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                     gl_profile_tab[PROFILE_C_APP_ID].conn_id,
                                                                     ESP_GATT_DB_DESCRIPTOR,
                                                                     gl_profile_tab[PROFILE_C_APP_ID].service_start_handle,
                                                                     gl_profile_tab[PROFILE_C_APP_ID].service_end_handle,
                                                                     gl_profile_tab[PROFILE_C_APP_ID].char_handle,
                                                                     &count);
        if (ret_status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
        }
        if (count > 0){
            descr_elem_result_c = (esp_gattc_descr_elem_t *)malloc(sizeof(esp_gattc_descr_elem_t) * count);
            if (!descr_elem_result_c){
                ESP_LOGE(GATTC_TAG, "malloc error, gattc no mem");
                break;
            }else{
                ret_status = esp_ble_gattc_get_descr_by_char_handle( gattc_if,
                                                                     gl_profile_tab[PROFILE_C_APP_ID].conn_id,
                                                                     p_data->reg_for_notify.handle,
                                                                     notify_descr_uuid,
                                                                     descr_elem_result_c,
                                                                     &count);
                if (ret_status != ESP_GATT_OK){
                    ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_descr_by_char_handle error");
                    free(descr_elem_result_c);
                    descr_elem_result_c = NULL;
                    break;
                }

                /* Every char has only one descriptor in our 'ESP_GATTS_DEMO' demo, so we used first 'descr_elem_result' */
                if (count > 0 && descr_elem_result_c[0].uuid.len == ESP_UUID_LEN_16 && descr_elem_result_c[0].uuid.uuid.uuid16 == 0x2902){
                    ret_status = esp_ble_gattc_write_char_descr( gattc_if,
                                                                 gl_profile_tab[PROFILE_C_APP_ID].conn_id,
                                                                 descr_elem_result_c[0].handle,
                                                                 sizeof(notify_en),
                                                                 (uint8_t *)&notify_en,
                                                                 ESP_GATT_WRITE_TYPE_RSP,
                                                                 ESP_GATT_AUTH_REQ_NONE);
                }

                if (ret_status != ESP_GATT_OK){
                    ESP_LOGE(GATTC_TAG, "esp_ble_gattc_write_char_descr error");
                }

                /* free descr_elem_result */
                free(descr_elem_result_c);
                descr_elem_result_c = NULL;
            }
        }
        else{
            ESP_LOGE(GATTC_TAG, "decsr not found");
        }
        break;
    }
    case ESP_GATTC_NOTIFY_EVT:
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_NOTIFY_EVT, Receive notify value:");
        esp_log_buffer_hex(GATTC_TAG, p_data->notify.value, p_data->notify.value_len);
        if (p_data->notify.value_len == 1 )
        {
            // c.battery_level = p_data->notify.value[0];
            // ESP_LOGI(GATTC_TAG,"电量:%d",c.battery_level);
        }
        else
        {
        Cmd_RxUnpack(p_data->notify.value,p_data->notify.value_len,&c.circles,&c.adc_value,&c.gpio_value);
        ESP_LOGI(GATTC_TAG,"圈数：%.3f,adc:%d,gpio:%d",c.circles,c.adc_value,c.gpio_value);
        ESP_LOGI(GATTC_TAG,"蓝牙连接状态：%d,阀门状态%d",c.blueTooth_state,c.state);
        } 

        break;
    case ESP_GATTC_WRITE_DESCR_EVT:
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "write descr failed, error status = %x", p_data->write.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "write descr success");
        if (set_device_c == 0)
        {
        uint16_t count = 2;
        esp_gatt_status_t status;
        char_elem_result_c = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result_c){
            ESP_LOGE(GATTC_TAG, "gattc no mem");
            break;
        }else{
            /****************保持连接特征值****************** */
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_C_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_C_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_c,
                                                        &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result_c);
                char_elem_result_c = NULL;
                break;
            }
            // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
            ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
            for (size_t i = 0; i < count; i++)
            {
                ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_c[i].uuid.uuid.uuid16);
            }
            gl_profile_tab[PROFILE_C_APP_ID].char_handle = char_elem_result_c[0].char_handle;
            //每次连接模块后，要尽快发送1次保持蓝牙连接指令(即0x29指令)，否则模块会在30秒后自动断开连接，
            //发送数据注意用write no response方式。


                uint8_t write_char_data[] = {0x29};

                esp_ble_gattc_write_char( gattc_if,
                            gl_profile_tab[PROFILE_C_APP_ID].conn_id,
                            gl_profile_tab[PROFILE_C_APP_ID].char_handle,
                            sizeof(write_char_data),
                            write_char_data,
                            ESP_GATT_WRITE_TYPE_NO_RSP,
                            ESP_GATT_AUTH_REQ_NONE);

            ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
            /* free char_elem_result */
                free(char_elem_result_c);
                char_elem_result_c = NULL;                           
            /****************保持连接特征值****************** */
        }
        }
        break;
    case ESP_GATTC_WRITE_CHAR_EVT:
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Write char failed, error status = %x", p_data->write.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Write char success%d",set_device_c);
        if (set_device_c == 3)
        {
            // start_scan();
            c.blueTooth_state = true; 
            change_led_color(c.led,Green_led);
        }
        if (set_device_c == 0)
        {
        uint16_t count = 2;
        esp_gatt_status_t status;
        char_elem_result_c = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result_c){
            ESP_LOGE(GATTC_TAG, "gattc no mem");
            break;
        }else{
            /****************保持连接特征值****************** */
            /****************设置工作参数****************** */
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_C_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_C_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_c,
                                                        &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result_c);
                char_elem_result_c = NULL;
                break;
            }
            // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
            ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
            for (size_t i = 0; i < count; i++)
            {
                ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_c[i].uuid.uuid.uuid16);
            }
            gl_profile_tab[PROFILE_C_APP_ID].char_handle = char_elem_result_c[0].char_handle;
            //每次连接模块后，要尽快发送1次保持蓝牙连接指令(即0x29指令)，否则模块会在30秒后自动断开连接，
            //发送数据注意用write no response方式。
            uint8_t isCompassOn = 0;// #使用磁场融合姿态
            uint8_t barometerFilter = 2;//
            uint16_t Cmd_ReportTag = CTL;// # 功能订阅标识
            uint8_t params[11] ;
            params[0] = 0x12;
            params[1] = 5 ;      //#静止状态加速度阀值
            params[2] = 255;     //#静止归零速度(单位cm/s) 0:不归零 255:立即归零
            params[3] = 0  ;     //#动态归零速度(单位cm/s) 0:不归零
            params[4] = ((barometerFilter&3)<<1) | (isCompassOn&1);   
            params[5] = UploadFrequency;      //#数据主动上报的传输帧率[取值0-250HZ], 0表示0.5HZ
            params[6] = 1 ;      //#陀螺仪滤波系数[取值0-2],数值越大越平稳但实时性越差
            params[7] = 3 ;      //#加速计滤波系数[取值0-4],数值越大越平稳但实时性越差
            params[8] = 5  ;     //#磁力计滤波系数[取值0-9],数值越大越平稳但实时性越差
            params[9] = Cmd_ReportTag&0xff;
            params[10] = (Cmd_ReportTag>>8)&0xff;
            esp_ble_gattc_write_char( gattc_if,
                                        gl_profile_tab[PROFILE_C_APP_ID].conn_id,
                                        char_elem_result_c[0].char_handle,
                                        sizeof(params),
                                        params,
                                        ESP_GATT_WRITE_TYPE_NO_RSP,
                                        ESP_GATT_AUTH_REQ_NONE);
            ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
            /* free char_elem_result */
                free(char_elem_result_c);
                char_elem_result_c = NULL;     
            set_device_c = 1;               
            /****************设置工作参数****************** */           
            /****************保持连接特征值****************** */
        }
        }

        if (set_device_c == 1)
        {
        uint16_t count = 2;
        esp_gatt_status_t status;
        char_elem_result_c = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result_c){
            ESP_LOGE(GATTC_TAG, "gattc no mem");
            break;
        }else{
            /****************保持连接特征值****************** */
            /****************设置工作参数****************** */
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_C_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_C_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_c,
                                                        &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result_c);
                char_elem_result_c = NULL;
                break;
            }
            // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
            ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
            for (size_t i = 0; i < count; i++)
            {
                ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_c[i].uuid.uuid.uuid16);
            }
            gl_profile_tab[PROFILE_C_APP_ID].char_handle = char_elem_result_c[0].char_handle;
            
            uint8_t params[3] ;
            params[0] = 0x51;
            params[1] = 0xAA ;      //#静止状态加速度阀值
            params[2] = 0xBB;     //#静止归零速度(单位cm/s) 0:不归零 255:立即归零
            
            esp_ble_gattc_write_char( gattc_if,
                                        gl_profile_tab[PROFILE_C_APP_ID].conn_id,
                                        char_elem_result_c[0].char_handle,
                                        sizeof(params),
                                        params,
                                        ESP_GATT_WRITE_TYPE_NO_RSP,
                                        ESP_GATT_AUTH_REQ_NONE);
            ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char%d",set_device_c); 
            /* free char_elem_result */
                free(char_elem_result_c);
                char_elem_result_c = NULL;     
            set_device_c = 2;               
            /****************设置工作参数****************** */           
            /****************保持连接特征值****************** */
        }
        }

        if (set_device_c == 2)
                {
            // esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote_battery_service_uuid);


        uint16_t count = 2;
        esp_gatt_status_t status;
        char_elem_result_c = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result_c){
            ESP_LOGE(GATTC_TAG, "gattc no mem");
            break;
        }else{
            /****************保持连接特征值****************** */
            /****************设置工作参数****************** */
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                        p_data->search_cmpl.conn_id,
                                                        gl_profile_tab[PROFILE_C_APP_ID].service_start_handle,
                                                        gl_profile_tab[PROFILE_C_APP_ID].service_end_handle,
                                                        keep_connect_uuid,
                                                        char_elem_result_c,
                                                        &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result_c);
                char_elem_result_c = NULL;
                break;
            }
            // esp_ble_gattc_write_char_req_t *write_req = (esp_ble_gattc_write_char_req_t *)malloc(sizeof(esp_ble_gattc_write_char_req_t) + 1);
            ESP_LOGI(GATTC_TAG,"ESP_GATT_DB_CHARACTERISTIC count:%d",count);
            for (size_t i = 0; i < count; i++)
            {
                ESP_LOGW(GATTC_TAG,"characteristic uuid:0x%x",char_elem_result_c[i].uuid.uuid.uuid16);
            }
            gl_profile_tab[PROFILE_C_APP_ID].char_handle = char_elem_result_c[0].char_handle;
            
            
            uint8_t params[2] ;
            params[0] = 0x27;
            params[1] = 0x10 ;      //#静止状态加速度阀值
            
            esp_ble_gattc_write_char( gattc_if,
                                        gl_profile_tab[PROFILE_C_APP_ID].conn_id,
                                        char_elem_result_c[0].char_handle,
                                        sizeof(params),
                                        params,
                                        ESP_GATT_WRITE_TYPE_NO_RSP,
                                        ESP_GATT_AUTH_REQ_NONE);
            ESP_LOGW(GATTC_TAG,"esp_ble_gattc_write_char"); 
            /* free char_elem_result */
                free(char_elem_result_c);
                char_elem_result_c = NULL;  
            set_device_c = 3;
        }
        }
        break;
    case ESP_GATTC_SRVC_CHG_EVT: {
        esp_bd_addr_t bda;
        memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:%08x%04x",(bda[0] << 24) + (bda[1] << 16) + (bda[2] << 8) + bda[3],
                 (bda[4] << 8) + bda[5]);
        break;
    }
    case ESP_GATTC_DISCONNECT_EVT:
        if (memcmp(p_data->disconnect.remote_bda, gl_profile_tab[PROFILE_C_APP_ID].remote_bda, 6) == 0){
            ESP_LOGI(GATTC_TAG, "device c disconnect");
            conn_device_c = false;
            get_service_c = false;
            c.blueTooth_state = false;
            set_device_c = 0;
            change_led_color(c.led,Red_led);
        }
        break;
    default:
        break;
    }
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;
    switch (event) {
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
         ESP_LOGI(GATTC_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.min_int,
                  param->update_conn_params.max_int,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        // esp_restart();
        esp_ble_gap_disconnect(param->update_conn_params.bda);
        break;
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
        //the unit of the duration is second
        uint32_t duration = 30;
        esp_ble_gap_start_scanning(duration);
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        //scan start complete event to indicate scan start successfully or failed
        if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(GATTC_TAG, "Scan start success");
        }else{
            ESP_LOGE(GATTC_TAG, "Scan start failed");
        }
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
            esp_log_buffer_hex(GATTC_TAG, scan_result->scan_rst.bda, 6);
            ESP_LOGI(GATTC_TAG, "Searched Adv Data Len %d, Scan Response Len %d", scan_result->scan_rst.adv_data_len, scan_result->scan_rst.scan_rsp_len);
            adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
            ESP_LOGI(GATTC_TAG, "Searched Device Name Len %d", adv_name_len);
            esp_log_buffer_char(GATTC_TAG, adv_name, adv_name_len);
            ESP_LOGI(GATTC_TAG, " ");
            if (Isconnecting){
                break;
            }
            if (conn_device_a && conn_device_b && conn_device_c && !stop_scan_done){
                stop_scan_done = true;
                esp_ble_gap_stop_scanning();
                ESP_LOGI(GATTC_TAG, "all devices are connected");
                break;
            }
            if (adv_name != NULL) {

                if (strlen(remote_device_name[0]) == adv_name_len && strncmp((char *)adv_name, remote_device_name[0], adv_name_len) == 0) {
                    if (conn_device_a == false) {
                        conn_device_a = true;
                        ESP_LOGI(GATTC_TAG, "Searched device %s", remote_device_name[0]);
                        esp_ble_gap_stop_scanning();
                        esp_ble_gattc_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, scan_result->scan_rst.bda, scan_result->scan_rst.ble_addr_type, true);
                        Isconnecting = true;
                    }
                    break;
                }
                else if (strlen(remote_device_name[1]) == adv_name_len && strncmp((char *)adv_name, remote_device_name[1], adv_name_len) == 0) {
                    if (conn_device_b == false) {
                        conn_device_b = true;
                        ESP_LOGI(GATTC_TAG, "Searched device %s", remote_device_name[1]);
                        esp_ble_gap_stop_scanning();
                        esp_ble_gattc_open(gl_profile_tab[PROFILE_B_APP_ID].gattc_if, scan_result->scan_rst.bda, scan_result->scan_rst.ble_addr_type, true);
                        Isconnecting = true;

                    }
                }
                else if (strlen(remote_device_name[2]) == adv_name_len && strncmp((char *)adv_name, remote_device_name[2], adv_name_len) == 0) {
                    if (conn_device_c == false) {
                        conn_device_c = true;
                        ESP_LOGI(GATTC_TAG, "Searched device %s", remote_device_name[2]);
                        esp_ble_gap_stop_scanning();
                        esp_ble_gattc_open(gl_profile_tab[PROFILE_C_APP_ID].gattc_if, scan_result->scan_rst.bda, scan_result->scan_rst.ble_addr_type, true);
                        Isconnecting = true;
                    }
                    break;
                }

            }
            break;
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            break;
        default:
            break;
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "Scan stop failed");
            break;
        }
        ESP_LOGI(GATTC_TAG, "Stop scan successfully");

        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "Adv stop failed");
            break;
        }
        ESP_LOGI(GATTC_TAG, "Stop adv successfully");
        break;

    default:
        break;
    }
}

static void button_event_cb(void *arg, void *data)
{
    start_scan();
}

void button_init(uint32_t button_num)
{
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = button_num,
            .active_level = BUTTON_ACTIVE_LEVEL,
#if CONFIG_GPIO_BUTTON_SUPPORT_POWER_SAVE
            .enable_power_save = true,
#endif
        },
    };
    button_handle_t btn = iot_button_create(&btn_cfg);
    assert(btn);
    esp_err_t err = iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, button_event_cb, NULL);
    // err |= iot_button_register_cb(btn, BUTTON_PRESS_UP, button_event_cb, NULL);
    // err |= iot_button_register_cb(btn, BUTTON_PRESS_REPEAT, button_event_cb, NULL);
    // err |= iot_button_register_cb(btn, BUTTON_PRESS_REPEAT_DONE, button_event_cb, NULL);
    // err |= iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, button_event_cb, NULL);
    // err |= iot_button_register_cb(btn, BUTTON_DOUBLE_CLICK, button_event_cb, NULL);
    // err |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, button_event_cb, NULL);
    // err |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_HOLD, button_event_cb, NULL);
    // err |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_UP, button_event_cb, NULL);
    // err |= iot_button_register_cb(btn, BUTTON_PRESS_END, button_event_cb, NULL);

#if CONFIG_ENTER_LIGHT_SLEEP_MODE_MANUALLY
    /*!< For enter Power Save */
    button_power_save_config_t config = {
        .enter_power_save_cb = button_enter_power_save,
    };
    err |= iot_button_register_power_save_cb(&config);
#endif

    ESP_ERROR_CHECK(err);
}
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    //ESP_LOGI(GATTC_TAG, "EVT %d, gattc if %d, app_id %d", event, gattc_if, param->reg.app_id);

    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
        } else {
            ESP_LOGI(GATTC_TAG, "Reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }

    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gattc_if == gl_profile_tab[idx].gattc_if) {
                if (gl_profile_tab[idx].gattc_cb) {
                    gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
                }
            }
        }
    } while (0);
}

void device_a_qingling()
{
    if (conn_device_a && set_device_a == 3)
    {
        set_device_a = 4;
    }
    if (conn_device_a && set_device_a == 4)
    {
        
        // printf("device_c_qingling");
        esp_ble_gattc_search_service(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,gl_profile_tab[PROFILE_A_APP_ID].conn_id, &remote_filter_service_uuid);
        
    }
    
}

void device_b_qingling()
{
    if (conn_device_b && set_device_b == 3)
    {
        set_device_b = 4;
    }
    if (conn_device_b && set_device_b == 4)
    {
        
        // printf("device_c_qingling");
        esp_ble_gattc_search_service(gl_profile_tab[PROFILE_B_APP_ID].gattc_if,gl_profile_tab[PROFILE_B_APP_ID].conn_id, &remote_filter_service_uuid);
        
    }
    
}

void device_c_qingling()
{
    if (conn_device_c && set_device_c == 3)
    {
        set_device_c = 4;
    }
    if (conn_device_c && set_device_c == 4)
    {
        
        // printf("device_c_qingling");
        esp_ble_gattc_search_service(gl_profile_tab[PROFILE_C_APP_ID].gattc_if,gl_profile_tab[PROFILE_C_APP_ID].conn_id, &remote_filter_service_uuid);
        
    }
    
}

void device_qingling(uint8_t num)
{
        switch (num)
        {
        case PROFILE_A_APP_ID:
            {   
                device_a_qingling();
            }
        break;
        case PROFILE_B_APP_ID:
            {
                device_b_qingling();
            }
        break;
        case PROFILE_C_APP_ID:
            {
                device_c_qingling();
            }
        break;
        
        default:
            break;
        }

}

void scan_key(uint8_t num)
{
    static uint8_t a_old=2,b_old=2,c_old=2;
    static bool press_a,press_b,pressc;
    uint8_t *gpio_value = NULL;
    uint8_t *old = NULL;
    Device_State * state = NULL;
    switch (num)
    {
    case PROFILE_A_APP_ID:
        {   
            gpio_value = & a.gpio_value;
            old = & a_old;
            state = &a.state;
        }
    break;
    case PROFILE_B_APP_ID:
        {
            gpio_value = & b.gpio_value;
            old = & b_old;
            state = &b.state;
            
        }
    break;
    case PROFILE_C_APP_ID:
        {
            gpio_value = & c.gpio_value;
            old = & c_old;
            state = &c.state;
            
        }
    break;
    
    default:
        break;
    }
    // printf("gpio_value : %d,old:%d,state:%d/r/n",*gpio_value,*old,*state);

    switch (*state)
    {
    case Unknow:
        {
            if (*gpio_value == 0 && *old ==1 )
            {
                *state =  Close;
                device_qingling(num);
            }else if (*gpio_value == 0 && *old ==0)
            {
                *state =  Close;
                device_qingling(num);
            }    
        }
        break;
    case Close:
        {
            if ((*gpio_value == 1 && *old ==0) || (*gpio_value == 1 && *old ==1))
            {
                *state =  Open;
                
            }
            
        }
        break;
    case MidProcess:
        /* code */
        break;
    case Open:
        {
            if ((*gpio_value == 0 && *old ==1) || (*gpio_value == 0 && *old ==0))
            {
                *state =  Close;
                device_qingling(num);
            }
            
        }
        break;                    
    default:
        break;
    }

    
    // if (*gpio_value == 0 && *old ==0)
    // {
    //     /* midprocess */
    //     *state =  MidProcess;
    // }
    
    *old = *gpio_value;
    
    
    

}

void updateState()
{
    // printf("a.blueTooth_state:%d\r\n",a.blueTooth_state);
    if (a.blueTooth_state == true)
    {
        //检测按键，上拉
        // printf("scan_key\r\n");
        scan_key(PROFILE_A_APP_ID);
    }else{
        a.state = Unknow;
    }

    if (b.blueTooth_state)
    {
        /* code */
        scan_key(PROFILE_B_APP_ID);
    }else{
        b.state = Unknow;
    }

    if (c.blueTooth_state)
    {
        /* code */
        scan_key(PROFILE_C_APP_ID);
    }else{
        c.state = Unknow;
    }
}

void JSONTask(void *pvParameters) {
    //初始化串口
    uart_init();
    int i = 0;
    for (;;) {
        // if (set_device_a ==3 && set_device_b ==3 && set_device_c==3)
        // {
        // printf("a.blueTooth_state:%d\r\n",a.blueTooth_state);
            updateState();
            sendStateJson(a,b,c);
            vTaskDelay(200/portTICK_PERIOD_MS);
        // }
        i++;
        if (i == 100)
        {
            // device_a_qingling();
            // device_b_qingling();
            // device_c_qingling();
            i=0;
        }
        
        
        
    }
    vTaskDelete(NULL);
}


// void updateStateTask(void *pvParameters) {

//     int i = 0;
//     for (;;) {
//         updateState();
//         sendStateJson(a,b,c);
//         vTaskDelay(200/portTICK_PERIOD_MS);
     
        
        
        
//     }
//     vTaskDelete(NULL);
// }

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    
    // 复制字符串到结构体的 name 成员
    strcpy(a.name, remote_device_name[0]);
    strcpy(b.name, remote_device_name[1]);
    strcpy(c.name, remote_device_name[2]);

    // 复制字符串到结构体的 device_id 成员
    strcpy(a.device_id, "a");
    strcpy(b.device_id, "b");
    strcpy(c.device_id, "c");

    a.state = Unknow;
    b.state = Unknow;
    c.state = Unknow;

    Flash_Searching();
    RGB_Init();
    RGB_Example();
    // SD_Init();
    LCD_Init();
    
    // lvgl_task(1);
    // 创建事件组
    xEventGroup = xEventGroupCreate();
    // lvgl_task(1);
    xTaskCreate(lvgl_task, "lvgl_task", 6*1024, NULL, 6, NULL);
    // vTaskDelay(3000/portTICK_PERIOD_MS);
    // 创建事件组
    
    if (xEventGroup == NULL) {
        ESP_LOGE("main","xEventGroup create fail");
        // 事件组创建失败，进入死循环
        while (1){}
    }
        EventBits_t uxBits;
    uxBits = xEventGroupWaitBits(
    xEventGroup,    // 事件组句柄
    EVENT_BIT_READY_ON | EVENT_BIT_READY_OFF, // 等待的事件标志位
    pdTRUE,         // 退出时清除事件标志位
    pdFALSE,        // 任意一个事件发生就退出等待
    portMAX_DELAY    // 无限等待
    );
    if (uxBits & EVENT_BIT_READY_ON) {
    // xTaskCreate(JSONTask, "JSONTask", 4*1024, NULL, 5, NULL);
    
    xTaskCreate(JSONTask, "JSONTask", 4*1024, NULL, 5, NULL);
    

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    //register the  callback function to the gap module
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret){
        ESP_LOGE(GATTC_TAG, "gap register error, error code = %x", ret);
        return;
    }

    //register the callback function to the gattc module
    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if(ret){
        ESP_LOGE(GATTC_TAG, "gattc register error, error code = %x", ret);
        return;
    }

    ret = esp_ble_gattc_app_register(PROFILE_A_APP_ID);
    if (ret){
        ESP_LOGE(GATTC_TAG, "gattc app register error, error code = %x", ret);
        return;
    }

    ret = esp_ble_gattc_app_register(PROFILE_B_APP_ID);
    if (ret){
        ESP_LOGE(GATTC_TAG, "gattc app register error, error code = %x", ret);
        return;
    }

    ret = esp_ble_gattc_app_register(PROFILE_C_APP_ID);
    if (ret){
        ESP_LOGE(GATTC_TAG, "gattc app register error, error code = %x", ret);
        return;
    }
    ret = esp_ble_gatt_set_local_mtu(200);
    if (ret){
        ESP_LOGE(GATTC_TAG, "set local  MTU failed, error code = %x", ret);
    }

    }
    button_init(BOOT_BUTTON_NUM);
}
    // xEventGroupSetBits(xEventGroup, EVENT_BIT_READY_ON);
    // xEventGroupClearBits
    // // 延时一段时间后触发另一个事件
    // vTaskDelay(pdMS_TO_TICKS(1000));
    // xEventGroupSetBits(xEventGroup, EVENT_BIT_LED_OFF);
    