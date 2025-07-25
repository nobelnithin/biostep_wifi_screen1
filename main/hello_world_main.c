#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "esp_timer.h"


static TimerHandle_t no_device_timer;  // Timer handle
static TimerHandle_t disconnect_timer;

#define NO_DEVICE_TIMEOUT 60000  // 2 minutes in milliseconds




#define MIN(a, b) ((a) < (b) ? (a) : (b)) 
static const char *TAG = "ESP32_AP";

// Simulated data
char model_number[] = "BIOSTEP-2025";
char serial_number[] = "SN1234567890";
int battery_level = 65; // start at 75%




void disconnect_timer_callback(TimerHandle_t xTimer)
{

    ESP_LOGI(TAG, "Stopping Wi-Fi as no device connected within timeout");
    esp_wifi_stop();
        // wifi_started = false;
    
}


void no_device_timer_callback(TimerHandle_t xTimer)
{
   
    ESP_LOGI(TAG, "Stopping Wi-Fi as no device connected within timeout");
    esp_wifi_stop();
        // wifi_started = false;
        
   

}


static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Station connected");
        xTimerStop(no_device_timer, 0);  // Stop the timer if a device connects
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "Station disconnected");
        disconnect_timer = xTimerCreate("DisconnectDeviceTimer", pdMS_TO_TICKS(20000), pdFALSE, NULL, disconnect_timer_callback);
        xTimerStart(disconnect_timer, 0); // Station Disconnected, thus timer start again
    }
}



esp_err_t update_post_handler(httpd_req_t *req) {
    char content[256];  // make buffer large enough
    int total_len = req->content_len;
    int received = 0;
    int ret;

    // Receive full content
    while (received < total_len) {
        ret = httpd_req_recv(req, content + received, sizeof(content) - received - 1);
        if (ret <= 0) {
            ESP_LOGE(TAG, "Failed to receive post data");
            return ESP_FAIL;
        }
        received += ret;
    }
    content[received] = '\0';  // Null terminate

   // ESP_LOGI(TAG, "Raw received string: %s", content);
    ESP_LOG_BUFFER_HEXDUMP(TAG, content, received, ESP_LOG_INFO);

    // Parse JSON
    cJSON *root = cJSON_Parse(content);
    if (!root) {
        ESP_LOGE(TAG, "JSON Parse Error");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid JSON");
        return ESP_FAIL;
    }

    // Extract fields
    cJSON *serial = cJSON_GetObjectItem(root, "new_serial");
    if (serial && cJSON_IsString(serial)) {
        strncpy(serial_number, serial->valuestring, sizeof(serial_number)-1);
        serial_number[sizeof(serial_number)-1] = '\0';
        ESP_LOGI(TAG, "Updated serial_number: %s", serial_number);
    } else {
        ESP_LOGW(TAG, "No valid 'new_serial' found");
    }

    cJSON *battery = cJSON_GetObjectItem(root, "battery");
    if (battery && cJSON_IsNumber(battery)) {
        battery_level = battery->valueint;
        ESP_LOGI(TAG, "Updated battery_level: %d", battery_level);
    } else {
        ESP_LOGW(TAG, "No valid 'battery' found");
    }

    cJSON_Delete(root);

    httpd_resp_send(req, "Updated", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


// HTTP Handlers
esp_err_t info_get_handler(httpd_req_t *req) {
    char response[128];
    snprintf(response, sizeof(response),
             "{\"model\":\"%s\",\"serial\":\"%s\"}",
             model_number, serial_number);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

esp_err_t angle_get_handler(httpd_req_t *req) {
    char response[128];
    snprintf(response, sizeof(response),
             "{\"%s\", \" %s\"}",
             roll, pitch);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

esp_err_t battery_get_handler(httpd_req_t *req) {
    battery_level = (battery_level + 1) % 100;  // Simulate battery
    char response[32];
    snprintf(response, sizeof(response), "{\"battery\":%d}", battery_level);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// Start HTTP server
httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t info_uri = {
            .uri = "/info",
            .method = HTTP_GET,
            .handler = info_get_handler
        };
        httpd_uri_t battery_uri = {
            .uri = "/battery",
            .method = HTTP_GET,
            .handler = battery_get_handler
        };
                httpd_uri_t angle_uri = {
            .uri = "/wristAngle",
            .method = HTTP_GET,
            .handler = angle_get_handler
        };
        httpd_uri_t update_uri = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = update_post_handler
        };
        httpd_register_uri_handler(server, &info_uri);
        httpd_register_uri_handler(server, &battery_uri);
        httpd_register_uri_handler(server, &angle_uri);
        httpd_register_uri_handler(server, &update_uri);
    }
    return server;
}

// Wi-Fi as AP
void wifi_init_softap(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "Biostep+",
            .ssid_len = strlen("Biostep+"),
            .password = "biostim@123",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Wi-Fi AP started. SSID: Biostep+");

    no_device_timer = xTimerCreate("NoDeviceTimer", pdMS_TO_TICKS(NO_DEVICE_TIMEOUT), pdFALSE, NULL, no_device_timer_callback);
    xTimerStart(no_device_timer, 0);
}

void app_main(void) {
    nvs_flash_init();
    wifi_init_softap();
    start_webserver();
}
