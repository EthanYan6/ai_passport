// main/portal_http.h —— 配网门户：DNS 劫持 + 极简 HTTP 服务器
//
// 与 wifi_manager_start_ap() 配合：
//   DNS : 通配解析，所有域名都指向 192.168.4.1，触发手机"自动弹出门户"
//   HTTP: GET  任意路径   → 配网门户 HTML（WiFi 列表下拉 + 密码框 + 名片表单）
//         GET  /scan      → 附近 AP 的 JSON 列表
//         POST /save      → 保存 WiFi 凭据并连接（表单编码 ssid/password）
//         POST /card      → 保存名片（name/title/bio）
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// 启动门户（DNS + HTTP 各一个任务）。ap_name 用于页面展示，如 "Checkin-XXXX"。
esp_err_t portal_http_start(const char *ap_name);

// 停止门户并释放任务
esp_err_t portal_http_stop(void);

bool portal_http_running(void);
