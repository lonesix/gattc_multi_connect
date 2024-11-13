/**
 ****************************************************************************************************
 * @file        servo_uart.h
 * @author      Lonesix
 * @version     V1.0
 * @date        2024.09.13
 * @brief       uart
 * @license     
 ****************************************************************************************************
 * @attention

 ****************************************************************************************************
 */
#ifndef _UART_H
#define _UART_H
#include "stdio.h"
#include "driver/uart.h"
#include "cJSON.h"
#include "stdarg.h"
typedef enum{
    Close,
    MidProcess,
    Open
}Device_State;

typedef struct 
{
    char device_id[10];       // 假设device_id不会超过9个字符（加上'\0'）
    char name[50];            // 假设设备名称不会超过49个字符（加上'\0'）
    volatile float circles;              // 圈数或某种计数值
    volatile int battery_level;        // 电池电量百分比
    volatile bool contact_switch;      //接触开关是否触发
    int adc_value;            //adc值
    Device_State state;       //设备开关状态
}Device;
extern Device a,b,c;

void uart_init(void);
void Uart_Send(uint8_t *buf , uint16_t len);
void uart2_printf(const char *fmt, ...);
void sendStateJson(Device A,Device B,Device C);
#endif