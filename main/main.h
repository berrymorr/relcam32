#define CONFIG_BOARD_ESP32CAM_AITHINKER 1
#define RED_LED GPIO_NUM_33
#define WHITE_LED GPIO_NUM_4
#define CONFIG_XCLK_FREQ 20000000 
#define PART_BOUNDARY "123456789000000000000987654321"
#include <esp_system.h>
#include <nvs_flash.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "camera_pins.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "private_data.h"

void connect_wifi();
int wifi_connect_status();
