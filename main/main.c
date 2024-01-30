#include "main.h"

static const char *TAG = "httpd";
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";


static esp_err_t init_camera(void)
{
    camera_config_t camera_config = {
        .pin_pwdn  = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,

        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        .xclk_freq_hz = CONFIG_XCLK_FREQ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_SVGA,

        .jpeg_quality = 10,
        .fb_count = 4,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY};//CAMERA_GRAB_LATEST. Sets when buffers should be filled
    esp_err_t err = esp_camera_init(&camera_config);
    return err;
}

esp_err_t jpg_stream_httpd_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len;
    uint8_t *_jpg_buf;
    char *part_buf[64];
    static int64_t last_frame = 0;
    if (!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }

        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;

        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (res == ESP_OK) {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }

        esp_camera_fb_return(fb);
        if (res != ESP_OK) {
            break;
        }
        int64_t fr_end = esp_timer_get_time();
        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        ESP_LOGI(TAG, "MJPG: %uKB %ums (%.1ffps)",
            (unsigned int)(_jpg_buf_len/1024),
            (unsigned int)frame_time, (float)(1000.0 / (uint32_t)frame_time));
    }

    last_frame = 0;
    return res;
}

httpd_uri_t uri_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = jpg_stream_httpd_handler,
    .user_ctx = NULL};
httpd_handle_t setup_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t stream_httpd  = NULL;

    if (httpd_start(&stream_httpd , &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd , &uri_get);
    }

    return stream_httpd;
}

static EventGroupHandle_t s_wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void connect_wifi() {
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
}

int wifi_connect_status() {
    wifi_ap_record_t wifidata;
    if (esp_wifi_sta_get_ap_info(&wifidata) == ESP_OK) {
        return 1;
    } else {
        return 0;
    }
}

#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
void cpu_load_task(void *pvParameter) {
    while (1) {
        TaskStatus_t *task_status_array;
        volatile UBaseType_t num_tasks;
        uint32_t total_run_time, total_idle_time = 0;

        num_tasks = uxTaskGetNumberOfTasks();
        task_status_array = pvPortMalloc(num_tasks * sizeof(TaskStatus_t));

        if (task_status_array != NULL) {
            num_tasks = uxTaskGetSystemState(task_status_array, num_tasks, &total_run_time);

            for (int i = 0; i < num_tasks; i++) {
                if (strstr(task_status_array[i].pcTaskName, "IDLE") != NULL) {
                    total_idle_time += task_status_array[i].ulRunTimeCounter;
                }
            }

            uint8_t load_percentage = (uint8_t)(((total_run_time - total_idle_time) * 100) / total_run_time);
            uint8_t duty_cycle = (255 * load_percentage) / 100;

            gpio_set_level(RED_LED, duty_cycle);

            vPortFree(task_status_array);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
#endif

void app_main() {
    #ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << RED_LED);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    xTaskCreate(cpu_load_task, "cpu_load_task", 2048, NULL, 5, NULL);
    #endif

    esp_err_t err;
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    // Initialize Wi-Fi
    connect_wifi();

    if (wifi_connect_status()) {
        ESP_LOGI(TAG, "Connected to WiFi");
        err = init_camera();
        if (err != ESP_OK)
        {
            printf("err: %s\n", esp_err_to_name(err));
            return;
        }
        setup_server();
        ESP_LOGI(TAG, "ESP32 CAM Web Server is up and running\n");
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
    }
}
