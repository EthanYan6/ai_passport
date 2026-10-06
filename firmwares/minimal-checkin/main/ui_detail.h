// main/ui_detail.h —— 单日打卡详情页
#pragma once

// 显示某日的打卡详情（覆盖在月历上方）：
//   第一行日期，下面列出每次打卡的时间和"完成/未完成"
void ui_detail_show(int year, int month, int day);

// 关闭详情页
void ui_detail_hide(void);
