// main/holiday.c —— 农历换算 + 节假日/补班查询
//
// 农历数据由 tools/gen_lunar.py（lunardate 库）生成的紧凑年表提供：
// 每农历年一条 {春节偏移, 闰月号, 月数, 30天月位图}，运行时逐月推进换算。
// 节假日/补班表按国务院每年安排人工维护，集中在下方 HOLIDAY_REST/HOLIDAY_WORK。
#include "holiday.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

// 农历年表行（数组本体在 lunar_table.inc，由工具生成）
typedef struct {
    int year;         // 农历年
    int off;          // 春节公历日 = 公历1/1 + off
    int leap;         // 闰月号（0 = 无闰月）
    int n;            // 月数 12 或 13
    uint32_t bits30;  // 30 天月位图（按时间序，bit s = 第 s 月 30 天）
} lunar_year_row_t;

#include "lunar_table.inc"

// ---------------- 公历 ↔ 天数序号（Howard Hinnant 算法） ----------------
// 返回自 1970-01-01 的天数（该日 = 0，周四）
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);            // [0, 399]
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + (long)doe - 719468L;
}

// ---------------- 农历年表查找 ----------------
// 表序：农历年 2025..2031（每年从春节开始）
#define LUNAR_YEAR_MIN 2025
#define LUNAR_YEAR_CNT ((int)(sizeof(LUNAR_YEARS) / sizeof(LUNAR_YEARS[0])))

// 农历年 ly 的春节公历天数序号；ly 出表返回 LONG_MIN
static long lunar_spring_day(int ly)
{
    int idx = ly - LUNAR_YEAR_MIN;
    if (idx < 0 || idx >= LUNAR_YEAR_CNT) return -1000000000L;
    return days_from_civil(ly, 1, 1) + LUNAR_YEARS[idx].off;
}

bool holiday_lunar(int y, int m, int d, lunar_date_t *out)
{
    long day = days_from_civil(y, m, d);
    if (day < lunar_spring_day(LUNAR_YEAR_MIN)) return false;

    // 找 day 所在农历年：春节(下一年) 之前属于当年；超出末年春节后由
    // 月循环自然判 false（表覆盖到 2032 年初）
    int ly = LUNAR_YEAR_MIN + LUNAR_YEAR_CNT - 1;
    for (int i = LUNAR_YEAR_MIN; i < LUNAR_YEAR_MIN + LUNAR_YEAR_CNT - 1; i++) {
        if (day < lunar_spring_day(i + 1)) { ly = i; break; }
    }
    long spring = lunar_spring_day(ly);
    long diff = day - spring;   // 年内第几天（0 起）
    const lunar_year_row_t *row = &LUNAR_YEARS[ly - LUNAR_YEAR_MIN];

    int leap = row->leap;
    long rem = diff;
    for (int s = 0; s < row->n; s++) {
        long len = ((row->bits30 >> s) & 1) ? 30 : 29;   // 该农历月天数
        if (rem < len) {
            if (out) {
                out->lday = (int)rem + 1;
                out->leap = (leap != 0 && s == leap);
                // 时间序 → 月号：闰月占一个序号
                if (leap == 0)        out->lmonth = s + 1;
                else if (s < leap)    out->lmonth = s + 1;
                else if (s == leap)   out->lmonth = leap;   // 闰 leap 月
                else                  out->lmonth = s;
            }
            return true;
        }
        rem -= len;
    }
    return false;   // 表尾越界
}

// ---------------- 名称 ----------------

void lunar_month_name(int m, bool leap, char *buf, int len)
{
    static const char *NAMES[13] = { "", "正", "二", "三", "四", "五", "六",
                                     "七", "八", "九", "十", "十一", "腊" };
    if (m < 1 || m > 12 || len <= 0) { if (len > 0) buf[0] = 0; return; }
    snprintf(buf, (size_t)len, "%s%s", leap ? "闰" : "", NAMES[m]);
}

void lunar_day_name(int d, char *buf, int len)
{
    static const char *TENS[10]  = { "", "初", "十", "廿", "三" };
    static const char *DIGS[10] = { "", "一", "二", "三", "四", "五",
                                    "六", "七", "八", "九", "十" };
    if (d < 1 || d > 30 || len <= 0) { if (len > 0) buf[0] = 0; return; }
    if (d == 30)      snprintf(buf, (size_t)len, "三十");
    else if (d == 20) snprintf(buf, (size_t)len, "二十");
    else if (d == 10) snprintf(buf, (size_t)len, "初十");
    else {
        int t = d / 10, u = d % 10;
        snprintf(buf, (size_t)len, "%s%s", TENS[t], DIGS[u]);
    }
}

// ---------------- 节日 ----------------

const char *holiday_festival_name(int y, int m, int d)
{
    // 公历固定节日
    if (m == 1  && d == 1)  return "元旦";
    if (m == 5  && d == 1)  return "劳动节";
    if (m == 10 && d == 1)  return "国庆节";

    lunar_date_t ld;
    if (holiday_lunar(y, m, d, &ld) && !ld.leap) {
        if (ld.lmonth == 1 && ld.lday == 1)  return "春节";
        if (ld.lmonth == 1 && ld.lday == 15)  return "元宵节";
        if (ld.lmonth == 5 && ld.lday == 5)   return "端午节";
        if (ld.lmonth == 7 && ld.lday == 7)   return "七夕";
        if (ld.lmonth == 8 && ld.lday == 15)  return "中秋节";
        if (ld.lmonth == 9 && ld.lday == 9)   return "重阳节";
    }
    // 除夕：明天是某农历年的春节（含腊月为小月廿九除夕的情况）
    long tomorrow = days_from_civil(y, m, d) + 1;
    for (int i = LUNAR_YEAR_MIN; i < LUNAR_YEAR_MIN + LUNAR_YEAR_CNT; i++) {
        if (lunar_spring_day(i) == tomorrow) return "除夕";
    }
    return NULL;
}

// ---------------- 节假日 / 补班表（按国务院每年安排，可自行修改） ----------------

typedef struct { int y, m, d1, d2; } range_t;

// 法定休（含调休连休）
static const range_t HOLIDAY_REST[] = {
    // 2025
    { 2025,  1,  1,  1 },      // 元旦
    { 2025,  1, 28,  2,  4 },  // 春节
    { 2025,  4,  4,  6 },      // 清明
    { 2025,  5,  1,  5 },      // 劳动节
    { 2025,  5, 31,  6,  2 },  // 端午
    { 2025, 10,  1,  8 },      // 国庆中秋
    // 2026
    { 2026,  1,  1,  3 },      // 元旦
    { 2026,  2, 15, 22 },      // 春节（2/17 初一）
    { 2026,  4,  4,  6 },      // 清明
    { 2026,  5,  1,  5 },      // 劳动节
    { 2026,  6, 19, 21 },      // 端午（6/19）
    { 2026,  9, 25, 27 },      // 中秋（9/25）
    { 2026, 10,  1,  8 },      // 国庆
    // 2027（暂按常规安排，待国务院发布后自行修改）
    { 2027,  1,  1,  3 },
    { 2027,  5,  1,  5 },
    { 2027, 10,  1,  7 },
};

// 调休补班（这些周末要上班）
static const range_t HOLIDAY_WORK[] = {
    { 2025,  1, 26, 26 }, { 2025,  2,  8,  8 },
    { 2026,  9, 20, 20 }, { 2026, 10, 10, 10 },
};

static bool in_ranges(const range_t *tbl, int cnt, int y, int m, int d)
{
    for (int i = 0; i < cnt; i++) {
        if (tbl[i].y == y && tbl[i].m == m && d >= tbl[i].d1 && d <= tbl[i].d2)
            return true;
    }
    return false;
}

int holiday_day_type(int y, int m, int d)
{
    if (in_ranges(HOLIDAY_WORK, (int)(sizeof(HOLIDAY_WORK) / sizeof(HOLIDAY_WORK[0])),
                  y, m, d)) return 3;
    if (in_ranges(HOLIDAY_REST, (int)(sizeof(HOLIDAY_REST) / sizeof(HOLIDAY_REST[0])),
                  y, m, d)) return 2;
    int wd = (int)((days_from_civil(y, m, d) + 4) % 7);   // 0=周日…6=周六
    if (wd == 0 || wd == 6) return 1;
    return 0;
}
