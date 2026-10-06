// main/checkin_logic.h —— 打卡记录与日历计算（纯 C，不依赖 ESP-IDF/LVGL，可在主机测试）
//
// 数据模型：一个月一个 u64，键 yYYYYmMM。每天占 2 bit，位偏移 (day-1)*2：
//   0 = 无记录  1 = 未完成(✗)  2 = 完成(✓)
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CHECKIN_NONE = 0,     // 无记录
    CHECKIN_UNDONE = 1,   // 未完成 ✗
    CHECKIN_DONE = 2,     // 完成 ✓
} checkin_state_t;

// 某年某月有多少天（month: 1-12）
int checkin_days_in_month(int year, int month);

// 星期几，0=周日…6=周六（坂内算法）
int checkin_weekday(int year, int month, int day);

// ---------- 位操作（day: 1-31） ----------
// 读某天状态。day 非法返回 CHECKIN_NONE。
checkin_state_t checkin_get(uint64_t bits, int day);

// 返回写入 day 后的新 u64。day 非法原样返回。
uint64_t checkin_set(uint64_t bits, int day, checkin_state_t state);

// ---------- 月统计 ----------
// 统计月内 done/undone 天数，可为 NULL。
void checkin_count_month(uint64_t bits, int days,
                         int *done, int *undone);

// ---------- 连续打卡 ----------
// 从 today（含）往前数连续"完成"天数；跨月时用前一个月的 bits 查更早的日子。
// today 对应 month_bits 中的 day。prev_bits 传上个月的记录，可为 0。
// 例：today=10月5日，10 月 bits 前 4 天全 done 且第 5 天 done，9 月最后 3 天 done
//     → 返回 4+3=7（若 10 月 5 日当天也是 done）。
int checkin_streak(uint64_t month_bits, uint64_t prev_bits,
                  int year, int month, int today);
