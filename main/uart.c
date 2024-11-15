#include "uart.h"
#define UART_NUM UART_NUM_2
#define UART_GPIO_TX 17
#define UART_GPIO_RX 18
#define UART2_BUF_SIZE (1024 * 2)
static char uart2_tx_buf[UART2_BUF_SIZE] = {0};
void uart_init()
{
    const uart_port_t uart_num = UART_NUM_2;
uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_APB,
};
// Configure UART parameters
ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
// Set UART pins(TX: IO4, RX: IO5, RTS: IO18, CTS: IO19)
ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_GPIO_TX, UART_GPIO_RX, -1, -1));
// Setup UART buffered IO with event queue
const int uart_buffer_size = UART2_BUF_SIZE;
QueueHandle_t uart_queue;
// Install UART driver using an event queue here
ESP_ERROR_CHECK(uart_driver_install(UART_NUM, uart_buffer_size, \
                                        uart_buffer_size, 10, NULL, 0));
}

void Uart_Send(uint8_t *buf , uint16_t len)
{
    uart_write_bytes(UART_NUM, (const char*)buf, len);
}

// 简单的uart2_printf实现，仅支持%d和%s
void uart2_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
 
    char *buf = uart2_tx_buf;
    char *p = buf;
    const char *s;
    int i;
 
    while (*fmt) {
        if (*fmt == '%' && *(fmt + 1) != '\0') {
            fmt++;
            switch (*fmt) {
                case 'd':
                    i = va_arg(args, int);
                    p += sprintf(p, "%d", i);
                    break;
                case 's':
                    s = va_arg(args, const char *);
                    while (*s) {
                        *p++ = *s++;
                    }
                    break;
                // 可以添加更多格式支持
                default:
                    *p++ = '%';
                    *p++ = *fmt;
                    break;
            }
        } else {
            *p++ = *fmt;
        }
        fmt++;
    }
    *p = '\0';
 
    // 发送数据
    Uart_Send( (const uint8_t *)buf, p - buf);
 
    va_end(args);
}


Device a,b,c;

/// @brief 
/// @param A 
/// @param B 
/// @param C 
void sendStateJson(Device A,Device B,Device C)
{
    // 创建根 JSON 对象（一个数组）
    cJSON *root = cJSON_CreateArray();
 
    // 创建第一个设备对象
    cJSON *device1 = cJSON_CreateObject();
    cJSON_AddStringToObject(device1, "device_id", A.device_id);
    cJSON_AddStringToObject(device1, "name", A.name);
    cJSON_AddNumberToObject(device1, "circles", A.circles);
    cJSON_AddNumberToObject(device1, "battery_level", A.battery_level);
    cJSON_AddItemToArray(root, device1);
 
    // 创建第二个设备对象
    cJSON *device2 = cJSON_CreateObject();
    cJSON_AddStringToObject(device2, "device_id", B.device_id);
    cJSON_AddStringToObject(device2, "name", B.name);
    cJSON_AddNumberToObject(device2, "circles", B.circles);
    cJSON_AddNumberToObject(device2, "battery_level", B.battery_level);
    cJSON_AddItemToArray(root, device2);
 
    // 创建第三个设备对象
    cJSON *device3 = cJSON_CreateObject();
    cJSON_AddStringToObject(device3, "device_id", C.device_id);
    cJSON_AddStringToObject(device3, "name", C.name);
    cJSON_AddNumberToObject(device3, "circles", C.circles);
    cJSON_AddNumberToObject(device3, "battery_level", C.battery_level);
    cJSON_AddItemToArray(root, device3);
 
    // 将 JSON 对象转换为字符串并打印
    char *json_string = cJSON_Print(root);
    // printf("%s\n", json_string);
    uart2_printf("%s\n", json_string);
    // 释放内存
    cJSON_Delete(root);
    free(json_string);

}