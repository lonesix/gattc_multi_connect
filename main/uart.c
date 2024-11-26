#include "uart.h"
#include <string.h>

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
// 函数：将浮点数格式化为保留两位小数的字符串
char* format_number_to_string(float number) {
    // 创建一个足够大的缓冲区来存储格式化后的字符串
    char buffer[50];
    // 使用 snprintf 函数进行格式化，%.2f 表示保留两位小数
    snprintf(buffer, sizeof(buffer), "%.2f", number);
    // 返回格式化后的字符串（注意：返回的字符串在静态缓冲区中，不应长时间保存或多次使用）
    return strdup(buffer); // 使用 strdup 分配新内存并复制字符串
}

//4.2V电池电池电压与电量转换
typedef struct {  
    
    float remaining_capacity; 
		float voltage;  
} VoltageCapacityPair;  
// 电池电压与剩余容量的关系表
    VoltageCapacityPair capacityTable[] = {
						{100,   4.16},
						{99,    4.15},
						{97,    4.14},
						{96,    4.13},
						{95,    4.12},
						{93,    4.11},
						{92,    4.10},
						{91,    4.09},
						{90,    4.08},
						{89,    4.07},
						{88,    4.06},
						{87,    4.05},
						{86,    4.04},
						{85,    4.03},
						{84,    4.02},
						{83,    4.01},
						{82,    4.00},
						{81,    3.99},
						{80,    3.97},
						{79,    3.96},
						{78,    3.95},
						{77,    3.94},
						{76,    3.93},
						{75,    3.92},
						{73,    3.91},
						{70,    3.90},
						{69,    3.89},
						{67,    3.88},
						{65,    3.87},
						{64,    3.86},
						{62,    3.85},
						{60,    3.84},
						{58,    3.83},
						{57,    3.82},
						{55,    3.81},
						{53,    3.80},
						{50,    3.79},
						{47,    3.78},
						{45,    3.77},
						{42,    3.76},
						{38,    3.75},
						{35,    3.74},
						{30,    3.73},
						{25,    3.72},
						{20,    3.71},
						{17,    3.70},
						{15,    3.69},
						{14,    3.68},
						{13,    3.67},
						{12,    3.66},
						{10,    3.65},
						{8,     3.64},
						{5,     3.63},
						{4,     3.62},
						{3,     3.61},
						{1,     3.59},
						{0,     3.58}


    };
		
		// 根据电压返回剩余容量的函数
int getRemainingCapacity(float voltage) {
    // 查找电压对应的剩余容量
    for (int i = 0; i < sizeof(capacityTable) / sizeof(capacityTable[0]); i++) {
        if (capacityTable[i].voltage <= voltage) {
            return capacityTable[i].remaining_capacity;
        }
    }

    return 0; // 如果电压低于任何已知值，则返回0%
}
/// @brief 
/// @param A 
/// @param B 
/// @param C 
void sendStateJson(Device A,Device B,Device C)
{
    // 创建根 JSON 对象（一个数组）
    cJSON *root = cJSON_CreateArray();
    char *formatted_number;
    // 创建第一个设备对象
    cJSON *device1 = cJSON_CreateObject();
    cJSON_AddStringToObject(device1, "device_id", A.device_id);
    cJSON_AddStringToObject(device1, "name", A.name);
    cJSON_AddNumberToObject(device1, "connection_status", A.blueTooth_state);
    formatted_number = format_number_to_string(A.circles);
    cJSON_AddStringToObject(device1, "circles",formatted_number);
    free(formatted_number); // 释放格式化字符串的内存
    A.battery_level = getRemainingCapacity((float) A.adc_value/1000);
    cJSON_AddNumberToObject(device1, "battery_level", A.battery_level);
    cJSON_AddNumberToObject(device1, "switch_state", A.state);
    cJSON_AddItemToArray(root, device1);
 
    // 创建第二个设备对象
    cJSON *device2 = cJSON_CreateObject();
    cJSON_AddStringToObject(device2, "device_id", B.device_id);
    cJSON_AddStringToObject(device2, "name", B.name);
    cJSON_AddNumberToObject(device2, "connection_status", B.blueTooth_state);
    formatted_number = format_number_to_string(B.circles);
    cJSON_AddStringToObject(device2, "circles",formatted_number);
    free(formatted_number); // 释放格式化字符串的内存
    B.battery_level = getRemainingCapacity((float) B.adc_value/1000);
    cJSON_AddNumberToObject(device2, "battery_level", B.battery_level);
    cJSON_AddNumberToObject(device2, "switch_state", B.state);
    cJSON_AddItemToArray(root, device2);
 
    // 创建第三个设备对象
    cJSON *device3 = cJSON_CreateObject();
    cJSON_AddStringToObject(device3, "device_id", C.device_id);
    cJSON_AddStringToObject(device3, "name", C.name);
    cJSON_AddNumberToObject(device3, "connection_status", C.blueTooth_state);
    formatted_number = format_number_to_string(C.circles);
    cJSON_AddStringToObject(device3, "circles",formatted_number);
    free(formatted_number); // 释放格式化字符串的内存
    C.battery_level = getRemainingCapacity((float) C.adc_value/1000);
    cJSON_AddNumberToObject(device3, "battery_level", C.battery_level);
    cJSON_AddNumberToObject(device3, "switch_state", C.state);
    cJSON_AddItemToArray(root, device3);
 
    // 将 JSON 对象转换为字符串并打印
    char *json_string = cJSON_Print(root);
    // printf("%s\n", json_string);
    uart2_printf("%s\n", json_string);
    // 释放内存
    cJSON_Delete(root);
    free(json_string);

}