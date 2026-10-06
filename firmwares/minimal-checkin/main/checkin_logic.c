// main/checkin_logic.c —— 纯逻辑实现（无 ESP-IDF 依赖，主机可测）
#include "checkin_logic.h"

// 平年每月天数（下标 0 = 1 月）
static const int DAYS_PLAIN[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static bool is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int checkin_days_in_month(int year, int month)
{
    if (month < 1 || month > 12) return 0;
    if (month == 2 && is_leap(year)) return 29;
    return DAYS_PLAIN[month - 1];
}

// 坂内算法：0=周日。省内存、无查表，适合 MCU。
int checkin_weekday(int year, int month, int day)
{
    if (month < 3) {
        year -= 1;
        month += 12;
    }
    int y = year % 100;
    int c = year / 100;
    int w = day + (13 * (month + 1)) / 5 + y + y / 4 + c / 4 + 5 * c;
    w = w % 7;
    return (w + 6) % 7;   // 变换为 0=周日
}

checkin_state_t checkin_get(uint64_t bits, int day)
{
    if (day < 1 || day > 31) return CHECKIN_NONE;
    int shift = (day - 1) * 2;
    if (shift >= 64) return CHECKIN_NONE;
    return (checkin_state_t)((bits >> shift) & 0x3);
}

uint64_t checkin_set(uint64_t bits, int day, checkin_state_t state)
{
    if (day < 1 || day > 31) return bits;
    int shift = (day - 1) * 2;
    if (shift >= 64) return bits;
    uint64_t mask = (uint64_t)0x3 << shift;
    return (bits & ~mask) | (((uint64_t)state & 0x3) << shift);
}

void checkin_count_month(uint64_t bits, int days, int *done, int *undone)
{
    int d = 0, u = 0;
    if (days < 0) days = 0;
    if (days > 31) days = 31;
    for (int i = 1; i <= days; i++) {
        checkin_state_t s = checkin_get(bits, i);
        if (s == CHECKIN_DONE) d++;
        else if (s == CHECKIN_UNDONE) u++;
    }
    if (done) *done = d;
    if (undone) *undone = u;
}

int checkin_streak(uint64_t month_bits, uint64_t prev_bits,
                   int year, int month, int today)
{
    // 输入校验：today 至少为 1；月份非法直接算 0
    if (today < 1 || month < 1 || month > 12) return 0;

    int streak = 0;
    // 从今天往前数本月
    for (int d = today; d >= 1; d--) {
        if (checkin_get(month_bits, d) != CHECKIN_DONE) break;
        streak++;
    }
    // 若本月 1 号也连续，则继续查上月
    if (streak == today) {
        int py = year, pm = month - 1;
        if (pm < 1) {
            pm = 12;
            py--;
        }
        int prev_days = checkin_days_in_month(py, pm);
        for (int d = prev_days; d >= 1; d--) {
            if (checkin_get(prev_bits, d) != CHECKIN_DONE) break;
            streak++;
        }
    }
    return streak;
}
