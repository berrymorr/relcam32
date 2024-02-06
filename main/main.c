#include "main.h"

static const char *TAG = "httpd";
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
volatile static EventGroupHandle_t s_wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

volatile esp_task_wdt_user_handle_t wdt_user_handle;
volatile uint32_t idleCounter = 0;
volatile TaskHandle_t idleTaskHandle[2];


void IRAM_ATTR timer_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    // Проверка, выполняется ли задача простоя
    if (idleTaskHandle[0] == xTaskGetCurrentTaskHandleForCPU(0)) {
        idleCounter++;
    }
    if (idleTaskHandle[1] == xTaskGetCurrentTaskHandleForCPU(1)) {
        idleCounter++;
    }
}


void cpu_load_task(void *pvParameter) {
    volatile static uint32_t currentIdleCounter;
    uint8_t loadPercentage;
    while (1) {
        currentIdleCounter = idleCounter;
        idleCounter = 0;
        loadPercentage = (uint8_t)((IDLE_COUNTER_TOTAL_PERCENT - currentIdleCounter)/IDLE_COUNTER_DIVIDER);
        ESP_LOGI(TAG, "idleCounter: %lu, load %u%%", currentIdleCounter, loadPercentage);
        uint32_t dutyCycle = 31 - (31 * loadPercentage) / 100; // Инвертирование для активного низкого уровня
        ledc_set_duty(LEDC_HS_MODE, LEDC_HS_CH0_CHANNEL, dutyCycle);
        ledc_update_duty(LEDC_HS_MODE, LEDC_HS_CH0_CHANNEL);
        ESP_ERROR_CHECK(esp_task_wdt_reset_user(wdt_user_handle));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void setup_idle_timer() {
    // Создание и настройка таймера
    gptimer_handle_t timer_handle;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1 МГц
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &timer_handle));

    // Регистрация обратного вызова
    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_callback,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer_handle, &cbs, NULL));

    // Настройка сигнала тревоги
    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = IDLE_COUNTER_TIMER_COUNT,
        .flags.auto_reload_on_alarm = true
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer_handle, &alarm_config));

    // Активация и запуск таймера
    ESP_ERROR_CHECK(gptimer_enable(timer_handle));
    ESP_ERROR_CHECK(gptimer_start(timer_handle));
}


void setup_red_led() {
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_5_BIT, // Минимальная разрядность 5 бит
        .freq_hz = 5000,                      // Частота 5 kHz
        .speed_mode = LEDC_HS_MODE,           // Режим высокой скорости
        .timer_num = LEDC_HS_TIMER            // Таймер 0
    };
    // Настройка таймера ШИМ
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .channel    = LEDC_HS_CH0_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_HS_CH0_GPIO,
        .speed_mode = LEDC_HS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_HS_TIMER
    };
    // Настройка ШИМ канала
    ledc_channel_config(&ledc_channel);
}


static esp_err_t init_camera(void) {
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
        ESP_LOGV(TAG, "MJPG: %uKB %llims (%ifps)",
            (_jpg_buf_len/1024),
            frame_time, (int)(1000 / (uint32_t)frame_time));
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


void app_main() {
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 15000, // Время тайм-аута в миллисекундах
        .trigger_panic = true, // Вызывать панику при тайм-ауте
    };

    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&wdt_config));
    ESP_ERROR_CHECK(esp_task_wdt_add_user("COMMON", &wdt_user_handle));

    idleTaskHandle[0] = xTaskGetIdleTaskHandleForCPU(0);
    idleTaskHandle[1] = xTaskGetIdleTaskHandleForCPU(1);

    setup_red_led();
    xTaskCreate(cpu_load_task, "cpu_load_task", 2048, NULL, 5, NULL);
    setup_idle_timer();
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
    ESP_ERROR_CHECK(esp_task_wdt_reset_user(wdt_user_handle));

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
    ESP_ERROR_CHECK(esp_task_wdt_reset_user(wdt_user_handle));
}
