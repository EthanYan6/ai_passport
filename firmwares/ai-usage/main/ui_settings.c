// main/ui_settings.c —— 设置页实现
//
// 上半屏：刷新间隔（OK 短按在预设间循环，写 NVS 即时生效）。
// 下半屏：WiFi/信号/IP/时间/电量 状态行；底部提示长按 OK 重新配网。
#include "ui_settings.h"
#include "ui_common.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_refresh_val;
static lv_obj_t *s_wifi_val;
static lv_obj_t *s_rssi_val;
static lv_obj_t *s_ip_val;
static lv_obj_t *s_clock_val;
static lv_obj_t *s_batt_val;
static lv_obj_t *s_srv_val;

static const char *fmt_refresh(char *buf, size_t n, uint32_t seconds)
{
    if (seconds >= 3600) {
        snprintf(buf, n, "%lu小时", (unsigned long)(seconds / 3600));
    } else if (seconds >= 60 && seconds % 60 == 0) {
        snprintf(buf, n, "%lu分钟", (unsigned long)(seconds / 60));
    } else {
        snprintf(buf, n, "%lu秒", (unsigned long)seconds);
    }
    return buf;
}

void ui_settings_build(lv_obj_t *p)
{
    ui_put_label(p, "刷新间隔", 14, 4, &app_font_14, C_MUTED);
    s_refresh_val = ui_put_label(p, "1分钟", 14, 20, &app_font_20, C_CYAN);
    ui_put_label(p, "OK:切换", 160, 24, &app_font_14, C_MUTED);

    lv_obj_t *sep = lv_obj_create(p);
    lv_obj_set_size(sep, 216, 1);
    lv_obj_set_pos(sep, 12, 50);
    lv_obj_set_style_bg_color(sep, lv_color_hex(C_FRAME), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    static const char *const row_labels[] = {"WiFi", "IP", "信号", "时间", "电量",
                                             "服务器"};
    lv_obj_t **vals[] = {&s_wifi_val, &s_ip_val, &s_rssi_val, &s_clock_val,
                         &s_batt_val, &s_srv_val};
    for (int i = 0; i < 6; i++) {
        int y = 62 + i * 28;
        ui_put_label(p, row_labels[i], 14, y, &app_font_14, C_MUTED);
        lv_obj_t *val = ui_put_label(p, "--", 110, y, &app_font_14, C_TEXT);
        lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
        lv_obj_set_width(val, 116);
        *vals[i] = val;
    }

    ui_put_label(p, "长按OK 重新配网", 14, 232, &app_font_14, C_DIM);
}

void ui_settings_render(const ui_settings_info_t *info)
{
    char buf[24];
    ui_label_printf(s_refresh_val, "%s",
                    fmt_refresh(buf, sizeof(buf), info->refresh_s));

    lv_obj_t *targets[6] = {s_wifi_val, s_ip_val, s_rssi_val, s_clock_val,
                            s_batt_val, s_srv_val};
    const char *texts[6] = {
        info->wifi_ok && info->ssid[0] ? info->ssid : "未连接",
        info->wifi_ok && info->ip[0] ? info->ip : "--",
        "--",
        info->time_ok ? info->clock : "未同步",
        "--",
        "--",
    };
    for (int i = 0; i < 6; i++) {
        lv_label_set_text(targets[i], texts[i]);
    }

    if (info->wifi_ok) {
        ui_label_printf(s_rssi_val, "%d dBm", info->rssi);
    }
    if (info->batt_pct >= 0) {
        int v = info->batt_pct;
        ui_label_printf(s_batt_val, "%d%%", v);
        lv_obj_set_style_text_color(s_batt_val, lv_color_hex(v > 50 ? C_GREEN
                                                   : v > 20 ? C_AMBER : C_RED), 0);
    } else {
        lv_label_set_text(s_batt_val, "--");
    }
    ui_label_printf(s_srv_val, "%d/4 台 门户管理", info->srv_count);
}
