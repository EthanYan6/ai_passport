// main/wifi_manager.h —— WiFi 管理：STA 自动重连 + SoftAP 配网
#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    WIFI_MGR_IDLE = 0,        // 未连接
    WIFI_MGR_CONNECTING,      // 正在连接（含自动重试）
    WIFI_MGR_CONNECTED,       // 已连上（拿到 IP）
    WIFI_MGR_FAILED,          // 连接失败（等用户重新配网或重试）
} wifi_mgr_state_t;

// 通知回调（运行于 WiFi 事件任务，只允许入队！）
typedef void (*wifi_mgr_cb_t)(wifi_mgr_state_t state, void *user);

// 初始化 netif/WiFi 栈并注册事件处理。cb 可为 NULL。
// 内部先创建 STA+AP netif（APSTA 常驻，启停只切换 AP 广播）。
esp_err_t wifi_manager_init(wifi_mgr_cb_t cb, void *user);

// 若 NVS 有凭据则连接 STA。无凭据返回 ESP_ERR_NOT_FOUND。
esp_err_t wifi_manager_connect_saved(void);

// 用指定凭据连接（写 NVS 并连接）。供配网门户 POST /save 调用。
esp_err_t wifi_manager_connect(const char *ssid, const char *pass);

// 当前状态
wifi_mgr_state_t wifi_manager_state(void);

// 当前连接的 SSID（未连接返回 false）
bool wifi_manager_get_ssid(char *ssid, size_t len);

// ---------------- 配网 AP ----------------
// 启动配网热点（SoftAP + DNS 劫持 + HTTP 门户由 portal_http 模块配合）。
// 返回后热点名写入 ap_name 缓冲（"Checkin-XXXX"）。
esp_err_t wifi_manager_start_ap(char *ap_name, size_t name_len);

// 停止配网热点
esp_err_t wifi_manager_stop_ap(void);

// 是否处于配网模式
bool wifi_manager_ap_active(void);
