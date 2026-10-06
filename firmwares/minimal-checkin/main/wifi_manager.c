// main/wifi_manager.c —— WiFi 管理实现
//
// STA: 从 NVS 读凭据连接，断开后 esp_timer 退避重试（不在事件任务里阻塞）。
// AP : Checkin-XXXX 热点，手机扫码直连后经配网门户提交新凭据。
// 事件只做通知（上层回调必须只入队）。
#include "wifi_manager.h"
#include "checkin_store.h"
#include "app_config.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "wifi_mgr";

static wifi_mgr_cb_t s_cb;
static void *s_user_data;
static volatile wifi_mgr_state_t s_state = WIFI_MGR_IDLE;
static volatile bool s_ap_active;
static volatile bool s_connect_requested;   // 是否应保持 STA 连接（防开机空连接乱触发重试）
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static bool s_stack_up;
static char s_connecting_ssid[33];
static int s_retry_count;
static esp_timer_handle_t s_retry_timer;

static void notify(wifi_mgr_state_t st)
{
    s_state = st;
    if (s_cb) s_cb(st, s_user_data);
}

// 退避重连定时器（esp_timer 任务，只调 esp_wifi_connect，无阻塞）
static void retry_timer_cb(void *arg)
{
    (void)arg;
    if (!s_connect_requested || s_state == WIFI_MGR_CONNECTED) return;
    ESP_LOGI(TAG, "尝试重连 WiFi…");
    esp_wifi_connect();
}

static void schedule_retry(void)
{
    if (s_retry_count >= 8) {
        ESP_LOGE(TAG, "WiFi 重连次数用尽，等待重新配网");
        s_connect_requested = false;
        notify(WIFI_MGR_FAILED);
        return;
    }
    s_retry_count++;
    uint32_t delay_s = s_retry_count < 3 ? 2 : (s_retry_count < 6 ? 5 : 15);
    ESP_LOGW(TAG, "WiFi 断开，%lus 后第 %d 次重试", (unsigned long)delay_s, s_retry_count);
    esp_timer_stop(s_retry_timer);   // 已在跑则重排
    esp_timer_start_once(s_retry_timer, (uint64_t)delay_s * 1000000);
    notify(WIFI_MGR_CONNECTING);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    switch (id) {
    case WIFI_EVENT_STA_START:
        if (s_connect_requested) esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        if (s_ap_active) {
            // 配网模式中：保持 AP，等用户在门户里重新提交
            notify(WIFI_MGR_FAILED);
            return;
        }
        if (s_connect_requested) schedule_retry();
        break;
    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "配网热点已启动");
        break;
    default:
        break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi 已连接 %s，IP: " IPSTR,
                 s_connecting_ssid, IP2STR(&evt->ip_info.ip));
        s_retry_count = 0;
        notify(WIFI_MGR_CONNECTED);
    }
}

esp_err_t wifi_manager_init(wifi_mgr_cb_t cb, void *user)
{
    s_cb = cb;
    s_user_data = user;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // STA + AP netif 常驻（APSTA），启停只切 AP，不动 STA
    esp_netif_config_t sta_cfg = ESP_NETIF_DEFAULT_WIFI_STA();
    s_sta_netif = esp_netif_new(&sta_cfg);
    if (!s_sta_netif) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_netif_attach_wifi_station(s_sta_netif));
    ESP_ERROR_CHECK(esp_wifi_set_default_wifi_sta_handlers());

    esp_netif_config_t ap_cfg = ESP_NETIF_DEFAULT_WIFI_AP();
    s_ap_netif = esp_netif_new(&ap_cfg);
    if (!s_ap_netif) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_netif_attach_wifi_ap(s_ap_netif));
    ESP_ERROR_CHECK(esp_wifi_set_default_wifi_ap_handlers());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              on_ip_event, NULL));

    const esp_timer_create_args_t timer_args = {
        .callback = retry_timer_cb,
        .name = "wifi_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_retry_timer));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_stack_up = true;

    // STA 断开自动重连由本模块接管
    wifi_config_t auto_cfg = { 0 };
    auto_cfg.sta.pmf_cfg.capable = true;
    auto_cfg.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &auto_cfg));
    return ESP_OK;
}

esp_err_t wifi_manager_connect_saved(void)
{
    char ssid[33] = { 0 }, pass[65] = { 0 };
    if (checkin_store_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) != ESP_OK
        || ssid[0] == '\0') {
        return ESP_ERR_NOT_FOUND;
    }
    strlcpy(s_connecting_ssid, ssid, sizeof(s_connecting_ssid));
    s_retry_count = 0;
    s_connect_requested = true;
    notify(WIFI_MGR_CONNECTING);

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    return esp_wifi_connect();
}

esp_err_t wifi_manager_connect(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    esp_err_t err = checkin_store_set_wifi(ssid, pass ? pass : "");
    if (err != ESP_OK) return err;
    return wifi_manager_connect_saved();
}

wifi_mgr_state_t wifi_manager_state(void)
{
    return s_state;
}

bool wifi_manager_get_ssid(char *ssid, size_t len)
{
    if (s_state != WIFI_MGR_CONNECTED || !ssid || len == 0) return false;
    strlcpy(ssid, s_connecting_ssid, len);
    return s_connecting_ssid[0] != '\0';
}

esp_err_t wifi_manager_start_ap(char *ap_name, size_t name_len)
{
    if (!s_stack_up) return ESP_ERR_INVALID_STATE;

    uint8_t mac[6] = { 0 };
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));

    char name[24];
    snprintf(name, sizeof(name), "Checkin-%02X%02X", mac[4], mac[5]);

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = { 0 },
            .password = APP_AP_PASS_8371,
            .ssid_len = 0,
            .channel = APP_AP_CHANNEL,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 2,
        },
    };
    strlcpy((char *)ap_cfg.ap.ssid, name, sizeof(ap_cfg.ap.ssid));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) return err;

    s_ap_active = true;
    if (ap_name && name_len) strlcpy(ap_name, name, name_len);
    ESP_LOGI(TAG, "配网热点启动: %s", name);
    return ESP_OK;
}

esp_err_t wifi_manager_stop_ap(void)
{
    if (!s_ap_active) return ESP_OK;
    s_ap_active = false;
    return esp_wifi_set_mode(WIFI_MODE_STA);
}

bool wifi_manager_ap_active(void)
{
    return s_ap_active;
}
