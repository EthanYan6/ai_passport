// main/ui_calendar.h —— 月历页
#pragma once

#include <stdbool.h>
#include "lvgl.h"

// 构建并显示月历页（覆盖在主页上方）。读取当前月份打卡记录。
void ui_calendar_show(void);

// 关闭月历页
void ui_calendar_hide(void);

bool ui_calendar_visible(void);

// 上下键移动选中日期（delta=±1 天，月内夹取）。返回是否有变化
bool ui_calendar_move_selection(int delta);

// 当前选中的年/月/日（日历未显示时返回 false）
bool ui_calendar_selected_date(int *year, int *month, int *day);
