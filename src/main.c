/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
   This example shows how to use ESPNOW.
   Prepare two device, one for sending ESPNOW data and another for receiving
   ESPNOW data.
*/
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "espnow_bench.h"

#define ESPNOW_MAXDELAY 512

#define CONFIG_ESPNOW_CHANNEL				1
#define ESPNOW_WIFI_MODE 					WIFI_MODE_STA
#define ESPNOW_WIFI_IF 						ESP_IF_WIFI_STA
#define MAX_ESPNOW_PACKET_SIZE				250

static const char *TAG = "espnow_example";

static QueueHandle_t s_example_espnow_queue;

static uint8_t s_example_peer_mac[ESP_NOW_ETH_ALEN] = { 16, 145, 168, 59, 157, 28 };
static uint8_t s_example_broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static uint8_t s_example_broadcast_data[1] = { 0x0 };

static void esp_now_bench_espnow_deinit();

extern const wpa_crypto_funcs_t g_wifi_default_wpa_crypto_funcs;

/* WiFi should start before using ESPNOW */
static void esp_now_bench_wifi_init(void)
{
    // ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = { \
        .osi_funcs = &g_wifi_osi_funcs, \
        .wpa_crypto_funcs = g_wifi_default_wpa_crypto_funcs, \
        .static_rx_buf_num = CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM,\
        .dynamic_rx_buf_num = CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM,\
        .tx_buf_type = CONFIG_ESP32_WIFI_TX_BUFFER_TYPE,\
        .static_tx_buf_num = WIFI_STATIC_TX_BUFFER_NUM,\
        .dynamic_tx_buf_num = WIFI_DYNAMIC_TX_BUFFER_NUM,\
        .cache_tx_buf_num = WIFI_CACHE_TX_BUFFER_NUM,\
        .csi_enable = 1,\
        .ampdu_rx_enable = 0,\
        .ampdu_tx_enable = 0,\
        .amsdu_tx_enable = 0,\
        .nvs_enable = 0,\
        .nano_enable = WIFI_NANO_FORMAT_ENABLED,\
        .rx_ba_win = WIFI_DEFAULT_RX_BA_WIN,\
        .wifi_task_core_id = WIFI_TASK_CORE_ID,\
        .beacon_max_len = WIFI_SOFTAP_BEACON_MAX_LEN, \
        .mgmt_sbuf_num = WIFI_MGMT_SBUF_NUM, \
        .feature_caps = g_wifi_feature_caps, \
        .sta_disconnected_pm = 0,  \
        .espnow_max_encrypt_num = CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM, \
        .magic = WIFI_INIT_CONFIG_MAGIC\
    };
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    // ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(ESPNOW_WIFI_MODE) );
    ESP_ERROR_CHECK( esp_wifi_start());
    // ESP_ERROR_CHECK( esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

// #if CONFIG_ESPNOW_ENABLE_LONG_RANGE
   //  ESP_ERROR_CHECK( esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
// #endif
}

/* ESPNOW sending or receiving callback function is called in WiFi task.
 * Users should not do lengthy operations from this task. Instead, post
 * necessary data to a queue and handle it from a lower priority task. */
static void example_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    example_espnow_event_t evt;
    example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    evt.id = EXAMPLE_ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

static void example_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    return;
}

static void example_espnow_task()
{
    example_espnow_event_t evt;

    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Start sending broadcast data");

    /* Start sending broadcast ESPNOW data. */
    int64_t before = 0;
    int64_t after = 0;
    int64_t dura = 0;
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    before = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
    
    int cnt = 0;
    int err_cnt = 0;
    if (esp_now_send(s_example_peer_mac, s_example_broadcast_data, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Send error");
        esp_now_bench_espnow_deinit();
        vTaskDelete(NULL);
    }

    while (xQueueReceive(s_example_espnow_queue, &evt, portMAX_DELAY) == pdTRUE) {
        switch (evt.id) {
            case EXAMPLE_ESPNOW_SEND_CB:
            {
                if (evt.info.send_cb.status == 0) {
                    err_cnt++;
                }
                cnt++;
                gettimeofday(&tv_now, NULL);
                after = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
                dura = dura + (after - before);
                if (cnt % 250 == 0) {
                    ESP_LOGI(TAG, "avg send time: %lld us, err: %d", dura / cnt, err_cnt);
                    err_cnt = 0;
                }
                example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

                ESP_LOGD(TAG, "Send data to "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);

                gettimeofday(&tv_now, NULL);
                before = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
                /* Send the next data after the previous data is sent. */
                if (esp_now_send(s_example_peer_mac, s_example_broadcast_data, 1) != ESP_OK) {
                    ESP_LOGE(TAG, "Send error");
                    esp_now_bench_espnow_deinit();
                    vTaskDelete(NULL);
                }
                break;
            }
            case EXAMPLE_ESPNOW_RECV_CB:
            {
                
            }
            default:
                ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                break;
        }
    }
}

static esp_err_t esp_now_bench_espnow_init(void)
{
    s_example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    if (s_example_espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(example_espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(example_espnow_recv_cb) );

    /* Add broadcast peer information to peer list. */
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, s_example_broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    memcpy(peer->peer_addr, s_example_peer_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);

    xTaskCreate(example_espnow_task, "example_espnow_task", 2048, NULL, 4, NULL);

    return ESP_OK;
}

static void esp_now_bench_espnow_deinit()
{
    vSemaphoreDelete(s_example_espnow_queue);
    esp_now_deinit();
}

void app_main(void)
{
    esp_now_bench_wifi_init();
    esp_now_bench_espnow_init();
}