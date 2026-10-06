// main/ui_main.h —— 主页
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "wifi_manager.h"

// 在 screen 上构建主页（幂等：重复调用只刷新）
void ui_main_build(lv_obj_t *screen);

// 每秒刷新：时间/日期/电池/WiFi 状态/跨天检测
void ui_main_tick(void);

// 对指定事项打卡（done=完成/未完成；同事项当天只保留最新状态与时间）。
// 返回 true 表示已处理（时间有效）；时间无效返回 false。
bool ui_main_mark_item(int item, bool done);

// 强制下一秒重载今日记录（配网页改过事项列表后调用）
void ui_main_reload(void);

// 顶部栏 WiFi 状态变化时刷新显示
void ui_main_set_wifi_state(wifi_mgr_state_t state);
