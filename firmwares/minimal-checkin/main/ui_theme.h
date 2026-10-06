// main/ui_theme.h —— 极简打卡 UI 主题：配色 + 拟物辅助控件
//
// 风格：拟物、扁平、简约、浅色护眼，阴影制造立体感。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

// ---------------- 色板（浅色护眼） ----------------
#define UI_PAPER      0xF4F0E6   // 米纸底色（页面背景）
#define UI_PANEL      0xFFFFFF   // 卡片白
#define UI_INK        0x3A3F47   // 主文字（深灰蓝）
#define UI_INK_SOFT   0x8A8578   // 次要文字（暖灰）
#define UI_GREEN      0x58A55C   // 完成 ✓
#define UI_GREEN_BG   0xE4F0E4   // 绿的浅底
#define UI_RED        0xD96357   // 未完成 ✗
#define UI_RED_BG     0xF6E5E1   // 红的浅底
#define UI_STROKE     0xD8D2C4   // 描边（米灰）
#define UI_STANDBY_BG 0x000000   // 休眠页黑底
#define UI_STANDBY_FG 0xFFFFFF   // 休眠页白字

// ---------------- 面板 ----------------
// 创建带阴影的圆角卡片（拟物立体感）。返回内容容器，往里面加子控件。
lv_obj_t *ui_panel_create(lv_obj_t *parent, int x, int y, int w, int h);

// 全屏页面容器：不透明底色、无滚动、置顶。z 序=创建序。
lv_obj_t *ui_page_create(lv_obj_t *parent);

// ---------------- 电池控件 ----------------
// 顶栏电池显示：自绘电池图形（外壳+按电量填充）+ 百分比文字。
// 返回容器；调用 ui_battery_refresh() 更新读数。
lv_obj_t *ui_battery_create(lv_obj_t *parent, int x, int y);

// 读取 bsp_battery_soc() 并刷新显示；SOC 不可用时显示 "--"
void ui_battery_refresh(lv_obj_t *battery_widget);

// 深色底（休眠页）配色开关：切换外壳/文字为浅色
void ui_battery_set_dark(lv_obj_t *battery_widget, bool dark);

// ---------------- WiFi 图标 ----------------
// 返回 LV_SYMBOL_WIFI 标签（montserrat_16）
lv_obj_t *ui_wifi_icon_create(lv_obj_t *parent, int x, int y);

// ---------------- 顶栏状态栏 ----------------
// 在 lv_layer_top() 上构建一次：WiFi 图标 + 状态文字 + 电池。
// 悬于所有页面之上，因此每个页面（含弹窗/休眠）都能看到。
void ui_statusbar_build(void);

// 每秒调用：内部 5 秒一次刷新电池读数
void ui_statusbar_tick(void);

// 更新 WiFi 显示（文字 + 文字色 + 图标色）
void ui_statusbar_set_wifi(const char *text, uint32_t text_color, uint32_t icon_color);

// 深色底（休眠页）配色开关：墨色系文字换为柔灰，绿/红保留
void ui_statusbar_set_dark(bool dark);
