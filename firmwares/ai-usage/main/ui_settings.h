// main/ui_settings.h —— 设置页：刷新间隔调整 + 连接状态
#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t refresh_s;
    bool wifi_ok;
    char ssid[33];
    int rssi;          // dBm，未连接填 0
    const char *ip;    // 未连接传 ""
    bool time_ok;
    const char *clock; // "HH:MM:SS"，未对时传 "--:--:--"
    int batt_pct;      // -1 未知
    int srv_count;     // 已配置的 SSH 监控服务器台数
} ui_settings_info_t;

void ui_settings_build(lv_obj_t *parent);
void ui_settings_render(const ui_settings_info_t *info);
