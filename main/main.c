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

/*
esp_err_t esp_http_ota(const esp_http_client_config_t *config) {
    esp_err_t err;
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE("ota", "Failed to find OTA partition");
        return ESP_FAIL;
    }

    ESP_LOGI("ota", "Starting OTA... partition subtype %d at offset 0x%" PRIx32, update_partition->subtype, update_partition->address);
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE("ota", "Failed esp_ota_begin: %s", esp_err_to_name(err));
        return err;
    }
    esp_http_client_handle_t client = esp_http_client_init(config);
    if (client == NULL) {
        ESP_LOGE("ota", "Failed to initialise HTTP connection");
        return ESP_FAIL;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE("ota", "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int http_status = esp_http_client_get_status_code(client);
    ESP_LOGI("ota", "HTTP status: %i", http_status);
    if (http_status > 299 || http_status < 200) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    char ota_write_data[OTA_BUF_SIZE]; // Буфер для записи данных OTA, локально в функции
    int data_read;
    int binary_file_length = 0;

    while (1) {
        int data_read = esp_http_client_read(client, ota_write_data, OTA_BUF_SIZE);
        if (data_read < 0) {
            ESP_LOGE("ota", "Error: data read error");
            esp_http_client_cleanup(client);
            esp_ota_abort(update_handle);
            return ESP_FAIL;
        } else if (data_read > 0) {
            err = esp_ota_write(update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) {
                ESP_LOGE("ota", "Error: ota_write failed");
                esp_http_client_cleanup(client);
                esp_ota_abort(update_handle);
                return ESP_FAIL;
            }
            binary_file_length += data_read;
            ESP_LOGI("ota", "Written image length %d", binary_file_length);
        } else if (data_read == 0) {
            if (esp_http_client_is_complete_data_received(client) == true) {
                ESP_LOGI("ota", "Download completed");
                break;
            }
        }
    }

    // Проверка, был ли получен полный файл
    if (esp_http_client_is_complete_data_received(client) != true) {
        ESP_LOGE("ota", "Error: complete file not received");
        esp_http_client_cleanup(client);
        esp_ota_abort(update_handle);
        return ESP_FAIL;
    }
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE("ota", "esp_ota_end failed: %s", esp_err_to_name(err));
        err = ESP_FAIL;
    } else if (err == ESP_OK) { // Ensure previous steps succeeded
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE("ota", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI("ota", "OTA succeeded");
        }
    }

    esp_http_client_cleanup(client);
    return err;
}
*/

esp_err_t esp_http_ota(const char *url) {
    esp_err_t err;
    esp_ota_handle_t update_handle = 0;

    // ota update init
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE("ota", "Failed to find OTA partition");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI("ota", "Starting OTA... partition subtype %d at offset 0x%" PRIx32, update_partition->subtype, update_partition->address);
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE("ota", "Failed esp_ota_begin: %s", esp_err_to_name(err));
        return err;
    }

    // http client init
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = CONFIG_OTA_RECV_TIMEOUT,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE("ota", "Failed to open HTTP connection: %s, aborting", esp_err_to_name(err));
        esp_ota_abort(update_handle);
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI("ota", "Downloading %i bytes fw binary...", content_length);
    if (content_length < 0) {
        ESP_LOGE("ota", "Failed to fetch HTTP headers, aborting");
        esp_ota_abort(update_handle);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FINISHED;
    }

    int http_status = esp_http_client_get_status_code(client);
    ESP_LOGI("ota", "HTTP status: %i", http_status);
    if (http_status != 200) {
        ESP_LOGE("ota", "HTTP status is not 200, aborting");
        esp_ota_abort(update_handle);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // read data from HTTP and write to flash
    char buffer[OTA_BUF_SIZE];
    int read_len;
    int total_read = 0;
    while ((read_len = esp_http_client_read(client, buffer, OTA_BUF_SIZE)) > 0) {
        ESP_LOGV("ota", "Read %i bytes", read_len);
        total_read += read_len;
        err = esp_ota_write(update_handle, (const void *)buffer, read_len);
            if (err != ESP_OK) {
                ESP_LOGE("ota", "esp_ota_write() failed: %s", esp_err_to_name(err));
                esp_ota_abort(update_handle);
                esp_http_client_cleanup(client);
                return err;
            }
    }
    ESP_LOGI("ota", "TOTAL READ %i bytes", total_read);

    // check if downloaded correctly
    if (read_len < 0) {
        ESP_LOGE("ota", "Error in reading HTTP response, aborting");
        esp_ota_abort(update_handle);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FINISHED;
    }

    if (esp_http_client_is_complete_data_received(client) != true) {
        ESP_LOGE("ota", "File received incomplete, aborting");
        esp_ota_abort(update_handle);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FINISHED;
    }

    // finishing ota and change boot partition
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE("ota", "esp_ota_end() failed: %s, aborting", esp_err_to_name(err));
        esp_ota_abort(update_handle);
        esp_http_client_cleanup(client);
        return err;
    } else {
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE("ota", "esp_ota_set_boot_partition() failed: %s", esp_err_to_name(err));
            esp_ota_abort(update_handle);
            esp_http_client_cleanup(client);
            return err;
        } else {
            ESP_LOGI("ota", "OTA succeeded");
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}


void ngx_unescape_uri(u_char **dst, u_char **src, size_t size, unsigned int type)
{
    u_char  *d, *s, ch, c, decoded;
    enum {
        sw_usual = 0,
        sw_quoted,
        sw_quoted_second
    } state;

    d = *dst;
    s = *src;

    state = 0;
    decoded = 0;

    while (size--) {

        ch = *s++;

        switch (state) {
        case sw_usual:
            if (ch == '?'
                    && (type & (NGX_UNESCAPE_URI | NGX_UNESCAPE_REDIRECT))) {
                *d++ = ch;
                goto done;
            }

            if (ch == '%') {
                state = sw_quoted;
                break;
            }

            *d++ = ch;
            break;
        case sw_quoted:

            if (ch >= '0' && ch <= '9') {
                decoded = (u_char) (ch - '0');
                state = sw_quoted_second;
                break;
            }

            c = (u_char) (ch | 0x20);
            if (c >= 'a' && c <= 'f') {
                decoded = (u_char) (c - 'a' + 10);
                state = sw_quoted_second;
                break;
            }

            /* the invalid quoted character */

            state = sw_usual;

            *d++ = ch;

            break;

        case sw_quoted_second:

            state = sw_usual;

            if (ch >= '0' && ch <= '9') {
                ch = (u_char) ((decoded << 4) + (ch - '0'));

                if (type & NGX_UNESCAPE_REDIRECT) {
                    if (ch > '%' && ch < 0x7f) {
                        *d++ = ch;
                        break;
                    }

                    *d++ = '%'; *d++ = *(s - 2); *d++ = *(s - 1);

                    break;
                }

                *d++ = ch;

                break;
            }

            c = (u_char) (ch | 0x20);
            if (c >= 'a' && c <= 'f') {
                ch = (u_char) ((decoded << 4) + (c - 'a') + 10);

                if (type & NGX_UNESCAPE_URI) {
                    if (ch == '?') {
                        *d++ = ch;
                        goto done;
                    }

                    *d++ = ch;
                    break;
                }

                if (type & NGX_UNESCAPE_REDIRECT) {
                    if (ch == '?') {
                        *d++ = ch;
                        goto done;
                    }

                    if (ch > '%' && ch < 0x7f) {
                        *d++ = ch;
                        break;
                    }

                    *d++ = '%'; *d++ = *(s - 2); *d++ = *(s - 1);
                    break;
                }

                *d++ = ch;

                break;
            }

            /* the invalid quoted character */

            break;
        }
    }

done:

    *dst = d;
    *src = s;
}


void decode_uri(char *dest, const char *src, size_t len) {
    if (!src || !dest) {
        return;
    }

    unsigned char *src_ptr = (unsigned char *)src;
    unsigned char *dst_ptr = (unsigned char *)dest;
    ngx_unescape_uri(&dst_ptr, &src_ptr, len, NGX_UNESCAPE_URI);
}


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
        ESP_LOGV(TAG, "idleCounter: %lu, load %u%%", currentIdleCounter, loadPercentage);
        uint32_t dutyCycle = 31 - (31 * loadPercentage) / 100; // invert for active low-level
        ledc_set_duty(LEDC_HS_MODE, LEDC_HS_CH0_CHANNEL, dutyCycle);
        ledc_update_duty(LEDC_HS_MODE, LEDC_HS_CH0_CHANNEL);
        if (wdt_user_handle) {
            esp_task_wdt_reset_user(wdt_user_handle);
        }
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
        .fb_count = CONFIG_CAMERA_FRAMEBUFFERS_COUNT,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY};//CAMERA_GRAB_LATEST. Sets when buffers should be filled
    esp_err_t err = esp_camera_init(&camera_config);
    return err;
}


esp_err_t ota_httpd_handler(httpd_req_t *req) {
    char param_value[HTTP_QUERY_KEY_MAX_LEN];
    char decoded_param_value[HTTP_QUERY_KEY_MAX_LEN] = {0};
    char*  buf;
    size_t buf_len;

    ESP_LOGI(TAG, "%s", req->uri);

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");
        if ((httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) &&
            (httpd_query_key_value(buf, "src", param_value, sizeof(param_value)) == ESP_OK)) {
                decode_uri(decoded_param_value, param_value, strnlen(param_value, HTTP_QUERY_KEY_MAX_LEN));
                if (strlen(decoded_param_value) >= 10) { //at least 'http://X/Y' format
                    ESP_LOGI(TAG, "Starting OTA fw update from %s ...", decoded_param_value);
                    esp_err_t result = esp_http_ota(decoded_param_value);
                    if (result == ESP_OK) {
                        ESP_LOGI("OTA", "OTA Update OK, restarting...");
                        httpd_resp_set_status(req, "204 No Content");
                        httpd_resp_send(req, "OTA Update OK, restarting...\n\n", -1);
                        ESP_ERROR_CHECK(esp_task_wdt_delete_user(wdt_user_handle));
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        esp_restart();
                    } else {
                        ESP_LOGE("OTA", "OTA Update failed: %s", esp_err_to_name(result));
                        // char err_desc[HTTP_QUERY_KEY_MAX_LEN];
                        // snprintf(err_desc, HTTP_QUERY_KEY_MAX_LEN, "OTA Update failed: %s\n\n", esp_err_to_name(result));
                        httpd_resp_set_status(req, "500 Internal Server Error");
                        // httpd_resp_send(req, err_desc, -1);
                        httpd_resp_send(req, "OTA Update failed!\n\n", -1);
                    }
                } else {
                    httpd_resp_set_status(req, "400 Bad Request");
                    httpd_resp_send(req, "Wrong OTA URL\n\n", -1);
                }
        } else {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "Wrong OTA URL\n\n", -1);
        }
        free(buf);
    }
    return ESP_OK;
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


void setup_server(void)
{
    httpd_uri_t mjpeg_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = jpg_stream_httpd_handler,
        .user_ctx = NULL
    };
    httpd_uri_t ota_uri = {
        .uri = "/ota",
        .method = HTTP_GET,
        .handler = ota_httpd_handler,
        .user_ctx = "OTA fw triggered"
    };
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    httpd_handle_t httpd_handle  = NULL;

    if (httpd_start(&httpd_handle , &config) == ESP_OK)
    {
        httpd_register_uri_handler(httpd_handle, &mjpeg_uri);
        httpd_register_uri_handler(httpd_handle, &ota_uri);
    }

    // return stream_httpd;
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
        .timeout_ms = CONFIG_WDT_COMMON_TIMEOUT, // Время тайм-аута в миллисекундах
        .trigger_panic = true, // Вызывать панику при тайм-ауте
    };

    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&wdt_config));
    ESP_ERROR_CHECK(esp_task_wdt_add_user("COMMON", &wdt_user_handle));

    idleTaskHandle[0] = xTaskGetIdleTaskHandleForCPU(0);
    idleTaskHandle[1] = xTaskGetIdleTaskHandleForCPU(1);

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
    // ESP_ERROR_CHECK(esp_task_wdt_reset_user(wdt_user_handle));

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
    // ESP_ERROR_CHECK(esp_task_wdt_reset_user(wdt_user_handle));
    setup_red_led();
    xTaskCreate(cpu_load_task, "cpu_load_task", 2048, NULL, 5, NULL);
    setup_idle_timer();
}
