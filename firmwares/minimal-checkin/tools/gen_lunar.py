# tools/gen_lunar.py —— 生成 main/holiday.c 的农历年表（2025-2031 农历年）
# 用法：python tools/gen_lunar.py > main/lunar_table.inc
# 依赖：pip install lunardate
#
# 输出每个农历年：春节公历日、闰月号、月序天数位图（1=30天）。
# 运行时由 holiday.c 用该表做 公历→农历 换算。
from datetime import date, timedelta
from lunardate import LunarDate

LUNAR_YEARS = range(2025, 2032)   # 农历年（每年从春节开始，末年覆盖到 2032 年初）


def solar(y, m, d):
    return date(y, m, d)


def lunar_of(dt):
    ld = LunarDate.from_solar_date(dt.year, dt.month, dt.day)
    return (ld.year, ld.month, ld.day, bool(ld.isLeapMonth))


rows = []
for ly in LUNAR_YEARS:
    # 春节：该农历年正月初一。在公历 ly 年 1/1..3/15 内找
    spring = None
    dt = solar(ly, 1, 1)
    while dt <= solar(ly, 3, 15):
        yy, mm, dd, lp = lunar_of(dt)
        if yy == ly and mm == 1 and dd == 1 and not lp:
            spring = dt
            break
        dt += timedelta(days=1)
    assert spring, f"农历 {ly} 年春节未找到"

    # 从春节逐日推进到下一年春节，统计每个农历月的天数（按时间序）
    next_spring = None
    dt = solar(ly + 1, 1, 1)
    while dt <= solar(ly + 1, 3, 15):
        yy, mm, dd, lp = lunar_of(dt)
        if yy == ly + 1 and mm == 1 and dd == 1 and not lp:
            next_spring = dt
            break
        dt += timedelta(days=1)
    assert next_spring, f"农历 {ly + 1} 年春节未找到"

    months = []   # [(月号, 闰), ...] 按时间序
    days = []     # 每月天数
    cur = None
    d = spring
    while d < next_spring:
        yy, mm, dd, lp = lunar_of(d)
        key = (yy, mm, lp)
        if key != cur:
            cur = key
            months.append((mm, lp))
            days.append(0)
        days[-1] += 1
        d += timedelta(days=1)

    leap = 0
    for i, (mm, lp) in enumerate(months):
        if lp:
            leap = mm
    assert len(months) in (12, 13)
    bits30 = 0
    for i, nd in enumerate(days):
        if nd == 30:
            bits30 |= (1 << i)
    off = (spring - solar(ly, 1, 1)).days
    rows.append((ly, off, leap, len(months), bits30))
    # 校验：总天数
    assert sum(days) == (next_spring - spring).days

print("// 由 tools/gen_lunar.py 生成（pip install lunardate），勿手改。")
print("// 农历年 2025-2031：春节偏移(相对公历1/1)、闰月、月数、30天月位图(按时间序)")
print("static const lunar_year_row_t LUNAR_YEARS[] = {")
for ly, off, leap, n, bits in rows:
    print(f"    {{ {ly}, {off}, {leap}, {n}, 0x{bits:04X} }},")
print("};")
