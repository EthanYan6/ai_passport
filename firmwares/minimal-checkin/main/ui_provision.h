// main/ui_provision.h —— WiFi 配网页（二维码 + 状态提示）
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 构建并显示配网页。ap_name 如 "Checkin-XXXX"，pass 如 "12345678"。
// QR 内容为 WIFI:T:WPA;S:<ap>;P:<pass>;; —— 扫码直连热点。
void ui_provision_show(const char *ap_name, const char *pass);

// 关闭配网页
void ui_provision_hide(void);

bool ui_provision_visible(void);

// 配网过程中状态文字：
//   0 = 等待配置  1 = 正在连接  2 = 已连接，即将返回  3 = 连接失败，请重试
void ui_provision_set_state(int state);
