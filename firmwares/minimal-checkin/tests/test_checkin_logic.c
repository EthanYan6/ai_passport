// tests/test_checkin_logic.c —— 纯逻辑主机测试（不依赖 ESP-IDF）
// 编译运行：见 tools/run_host_tests.mjs 或手动用任意 C 编译器。
#include <stdio.h>
#include <string.h>
#include "../main/checkin_logic.h"

static int g_failed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failed++; \
    } \
} while (0)

static void test_days_in_month(void)
{
    CHECK(checkin_days_in_month(2026, 1) == 31);
    CHECK(checkin_days_in_month(2026, 2) == 28);   // 平年
    CHECK(checkin_days_in_month(2028, 2) == 29);   // 闰年
    CHECK(checkin_days_in_month(2000, 2) == 29);   // 400 倍数
    CHECK(checkin_days_in_month(1900, 2) == 28);   // 100 倍数非 400
    CHECK(checkin_days_in_month(2026, 4) == 30);
    CHECK(checkin_days_in_month(2026, 12) == 31);
    CHECK(checkin_days_in_month(2026, 13) == 0);   // 非法
    CHECK(checkin_days_in_month(2026, 0) == 0);
}

static void test_weekday(void)
{
    // 已知锚点：2026-10-05 是周一
    CHECK(checkin_weekday(2026, 10, 5) == 1);
    // 2026-10-04 周日
    CHECK(checkin_weekday(2026, 10, 4) == 0);
    // 2026-01-01 周四
    CHECK(checkin_weekday(2026, 1, 1) == 4);
    // 2024-02-29 周四
    CHECK(checkin_weekday(2024, 2, 29) == 4);
    // 2000-01-01 周六
    CHECK(checkin_weekday(2000, 1, 1) == 6);
    // 1970-01-01 周四
    CHECK(checkin_weekday(1970, 1, 1) == 4);
}

static void test_bits(void)
{
    CHECK(checkin_get(0, 1) == CHECKIN_NONE);
    uint64_t b = checkin_set(0, 1, CHECKIN_UNDONE);
    CHECK(checkin_get(b, 1) == CHECKIN_UNDONE);
    b = checkin_set(b, 1, CHECKIN_DONE);
    CHECK(checkin_get(b, 1) == CHECKIN_DONE);
    CHECK(checkin_get(b, 2) == CHECKIN_NONE);

    b = checkin_set(0, 31, CHECKIN_DONE);
    CHECK(checkin_get(b, 31) == CHECKIN_DONE);
    CHECK(checkin_get(b, 30) == CHECKIN_NONE);

    // 覆写
    b = checkin_set(0, 5, CHECKIN_UNDONE);
    b = checkin_set(b, 6, CHECKIN_DONE);
    CHECK(checkin_get(b, 5) == CHECKIN_UNDONE);
    CHECK(checkin_get(b, 6) == CHECKIN_DONE);
    b = checkin_set(b, 5, CHECKIN_DONE);
    CHECK(checkin_get(b, 5) == CHECKIN_DONE);
    CHECK(checkin_get(b, 6) == CHECKIN_DONE);   // 邻位不受影响

    // 非法入参
    CHECK(checkin_get(0xFFFFFFFFFFFFFFFFull, 0) == CHECKIN_NONE);
    CHECK(checkin_get(0xFFFFFFFFFFFFFFFFull, 32) == CHECKIN_NONE);
    uint64_t same = checkin_set(0x1234, 0, CHECKIN_DONE);
    CHECK(same == 0x1234);
    same = checkin_set(0x1234, 32, CHECKIN_DONE);
    CHECK(same == 0x1234);
}

static void test_count(void)
{
    uint64_t b = 0;
    for (int d = 1; d <= 10; d += 2) b = checkin_set(b, d, CHECKIN_DONE);
    for (int d = 2; d <= 10; d += 2) b = checkin_set(b, d, CHECKIN_UNDONE);

    int done = 0, undone = 0;
    checkin_count_month(b, 31, &done, &undone);
    CHECK(done == 5);
    CHECK(undone == 5);

    checkin_count_month(b, 5, &done, &undone);   // 只数前 5 天
    CHECK(done == 3);    // 1,3,5 完成
    CHECK(undone == 2);  // 2,4 未完成

    checkin_count_month(0, 31, &done, &undone);
    CHECK(done == 0 && undone == 0);
}

static void test_streak(void)
{
    // 10 月 1-5 全完成，今天 5 号
    uint64_t oct = 0;
    for (int d = 1; d <= 5; d++) oct = checkin_set(oct, d, CHECKIN_DONE);
    CHECK(checkin_streak(oct, 0, 2026, 10, 5) == 5);

    // 4 号缺卡 → streak 到 5（只有今天）
    oct = checkin_set(oct, 4, CHECKIN_NONE);
    CHECK(checkin_streak(oct, 0, 2026, 10, 5) == 1);

    // 恢复 4 号，跨月：9 月 29/30 完成
    oct = checkin_set(oct, 4, CHECKIN_DONE);
    uint64_t sep = 0;
    sep = checkin_set(sep, 29, CHECKIN_DONE);
    sep = checkin_set(sep, 30, CHECKIN_DONE);
    CHECK(checkin_streak(oct, sep, 2026, 10, 5) == 7);   // 5 + 2

    // 9 月 28 无 → 只算到 9/29
    CHECK(checkin_streak(oct, 0, 2026, 10, 5) == 5);    // 无上月记录
    uint64_t sep2 = checkin_set(sep, 28, CHECKIN_UNDONE);
    CHECK(checkin_streak(oct, sep2, 2026, 10, 5) == 7); // UNDONE 也是断

    // 今天未完成 → streak=0
    oct = checkin_set(oct, 5, CHECKIN_UNDONE);
    CHECK(checkin_streak(oct, sep, 2026, 10, 5) == 0);

    // 今天无记录 → 0
    oct = checkin_set(oct, 5, CHECKIN_NONE);
    CHECK(checkin_streak(oct, sep, 2026, 10, 5) == 0);

    // 今天是 1 号且完成，上月最后一天未完成
    uint64_t nov = checkin_set(0, 1, CHECKIN_DONE);
    CHECK(checkin_streak(nov, checkin_set(0, 31, CHECKIN_UNDONE), 2026, 11, 1) == 1);
    // 上月最后一天完成 → 2
    CHECK(checkin_streak(nov, checkin_set(0, 31, CHECKIN_DONE), 2026, 11, 1) == 2);

    // 跨年：1 月 1 日，上月是去年 12 月（31 天）
    uint64_t jan = checkin_set(0, 1, CHECKIN_DONE);
    CHECK(checkin_streak(jan, checkin_set(0, 31, CHECKIN_DONE), 2027, 1, 1) == 2);
}

int main(void)
{
    test_days_in_month();
    test_weekday();
    test_bits();
    test_count();
    test_streak();

    if (g_failed) {
        printf("== %d 个用例失败 ==\n", g_failed);
        return 1;
    }
    printf("== 全部通过 ==\n");
    return 0;
}
