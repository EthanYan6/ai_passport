// main/holiday.h —— 农历换算 + 节假日/补班查询（表范围 2025-2031）
#pragma once

#include <stdbool.h>

typedef struct {
    int lmonth;    // 农历月 1-12
    int lday;      // 农历日 1-30
    bool leap;     // 是否闰月
} lunar_date_t;

// 公历 → 农历。超出表范围（2025-01-29 前 / 2032 春节后）返回 false。
bool holiday_lunar(int y, int m, int d, lunar_date_t *out);

// 农历月名："正"/"二"/…/"十一"/"冬"/"腊"，闰月加"闰"前缀（如"闰五"）
void lunar_month_name(int m, bool leap, char *buf, int len);

// 农历日名："初一"…"初十"、"十一"…"十九"、"二十"、"廿一"…"廿九"、"三十"
void lunar_day_name(int d, char *buf, int len);

// 当天节日名（春节/除夕/中秋/元旦/国庆…），无节日返回 NULL
const char *holiday_festival_name(int y, int m, int d);

// 当天类型：0=工作日 1=周末休 2=法定节假日(休) 3=调休补班(班)
int holiday_day_type(int y, int m, int d);
