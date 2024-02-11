#define CONFIG_BOARD_ESP32CAM_AITHINKER 1
#define RED_LED GPIO_NUM_33
#define WHITE_LED GPIO_NUM_4
#define CONFIG_XCLK_FREQ 20000000 
#define IDLE_COUNTER_TIMER_COUNT 10000 //(100,1000,10000)
#define IDLE_COUNTER_TOTAL_PERCENT (2000000/IDLE_COUNTER_TIMER_COUNT)
#define IDLE_COUNTER_DIVIDER (20000/IDLE_COUNTER_TIMER_COUNT)
#define PART_BOUNDARY "123456789000000000000987654321"
#define HTTP_QUERY_KEY_MAX_LEN 255
#define NGX_UNESCAPE_URI (1)
#define NGX_UNESCAPE_REDIRECT (2)

#define LEDC_HS_TIMER          LEDC_TIMER_1
#define LEDC_HS_MODE           LEDC_HIGH_SPEED_MODE
#define LEDC_HS_CH0_GPIO       (RED_LED)
#define LEDC_HS_CH0_CHANNEL    LEDC_CHANNEL_1
#define LEDC_TEST_DUTY         (4000)
#define LEDC_TEST_FADE_TIME    (3000)

#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/gptimer.h"
#include "esp_system.h"
#include "esp_types.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "camera_pins.h"

#include "private_data.h"

#define ESP_RETURN_ON_FALSE(expr, ret, tag, fmt, ...) do { \
    if (!(expr)) { \
        ESP_LOGE(tag, fmt, ##__VA_ARGS__); \
        return (ret); \
    } \
} while(0)


void connect_wifi();
int wifi_connect_status();
