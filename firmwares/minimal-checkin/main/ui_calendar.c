// main/ui_calendar.c —— 月历页：打卡日历 + 底部统计（支持跨月导航）
//
// 布局（240x320）：
//   标题"2026年10月"  y=36
//   星期头"日一二三四五六" y=78
//   日期格 30x28 间距 2，x 起 9，y 起 98，7 列（最多 6 行）
//   统计行 y=288
// 格子着色：完成=绿底白字，未完成=红底白字，无记录=灰字，今天=白色粗边框
// （白框只在当月画），选中格=墨色外圈（默认选今天）。
// 上下键移动选中日；1 号再往前 → 上月最后一天，月末再往后 → 下月 1 号，
// 跨月时整块重建（s_content 容器）。
#include "ui_calendar.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "checkin_store.h"
#include "checkin_logic.h"
#include "time_sync.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define CELL_W      30
#define CELL_H      28
#define CELL_GAP    2
#define GRID_X      9
#define GRID_Y      98
#define ROWS        6

static lv_obj_t *s_page;
static lv_obj_t *s_title;     // 年月标题（跨月时只改文本）
static lv_obj_t *s_content;   // 星期头 + 格子 + 统计，跨月时整块重建
static lv_obj_t *s_cells[ROWS * 7];

// 选中态（默认今天；上/下键前后移动，越界跨月）
static int s_sel_year, s_sel_month, s_sel_day;
static int s_first_wd, s_days;

// 今天（show 时缓存；"白框"只在显示当月时画）
static bool s_today_valid;
static int s_today_y, s_today_m, s_today_d;

// 只给选中格加墨色外圈（其余格清零外圈）；与"今天"的白边框可叠加
static void selection_apply(int day)
{
    for (int i = 0; i < ROWS * 7; i++) {
        if (s_cells[i]) lv_obj_set_style_outline_width(s_cells[i], 0, 0);
    }
    if (!s_page || day < 1 || day > s_days) return;
    int idx = day - 1 + s_first_wd;
    if (idx < 0 || idx >= ROWS * 7) return;
    lv_obj_t *c = s_cells[idx];
    if (!c) return;
    lv_obj_set_style_outline_color(c, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_outline_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_pad(c, 0, 0);
    lv_obj_set_style_outline_width(c, 2, 0);
}

// 重建某年某月的日历内容（星期头 + 格子 + 统计），并选中 sel_day
static void rebuild(int y, int m, int sel_day)
{
    if (s_content) {
        lv_obj_delete(s_content);
        s_content = NULL;
        memset(s_cells, 0, sizeof(s_cells));
    }
    s_content = lv_obj_create(s_page);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_size(s_content, 240, 320);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    // 标题
    static char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%d年%02d月", y, m);
    lv_label_set_text(s_title, tbuf);

    // 星期头（7 个独立标签，与日期格逐列对齐）
    static const char *WEEKS[7] = { "日", "一", "二", "三", "四", "五", "六" };
    for (int col = 0; col < 7; col++) {
        lv_obj_t *w = lv_label_create(s_content);
        lv_obj_set_style_text_font(w, &font_cn_16, 0);
        lv_obj_set_style_text_color(w, lv_color_hex(UI_INK_SOFT), 0);
        lv_obj_set_style_text_align(w, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(w, GRID_X + col * (CELL_W + CELL_GAP), 78);
        lv_obj_set_width(w, CELL_W);
        lv_label_set_text(w, WEEKS[col]);
    }

    // 月份参数
    uint64_t bits = checkin_store_get_month(y, m);
    int days = checkin_days_in_month(y, m);
    int first_wd = checkin_weekday(y, m, 1);   // 1 号是周几
    bool is_cur = s_today_valid && y == s_today_y && m == s_today_m;
    int today = is_cur ? s_today_d : 0;        // 白框只在当月画

    s_sel_year = y; s_sel_month = m; s_sel_day = sel_day;
    s_first_wd = first_wd; s_days = days;

    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < 7; col++) {
            int idx = row * 7 + col;
            int day = row * 7 + col - first_wd + 1;   // 该格对应的日期
            int x = GRID_X + col * (CELL_W + CELL_GAP);
            int yy = GRID_Y + row * (CELL_H + CELL_GAP);

            lv_obj_t *cell = lv_obj_create(s_content);
            lv_obj_set_pos(cell, x, yy);
            lv_obj_set_size(cell, CELL_W, CELL_H);
            lv_obj_set_style_radius(cell, 8, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_shadow_width(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            s_cells[idx] = cell;

            if (day < 1 || day > days) {
                lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            checkin_state_t st = checkin_get(bits, day);
            uint32_t bg = UI_PANEL, fg = UI_INK_SOFT;
            switch (st) {
            case CHECKIN_DONE:   bg = UI_GREEN; fg = 0xFFFFFF; break;
            case CHECKIN_UNDONE: bg = UI_RED;   fg = 0xFFFFFF; break;
            default:             bg = 0xEDE8DC; fg = UI_INK_SOFT; break;
            }
            lv_obj_set_style_bg_color(cell, lv_color_hex(bg), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);

            // 今天：白色粗边框
            if (day == today) {
                lv_obj_set_style_border_color(cell, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_border_width(cell, 3, 0);
                lv_obj_set_style_border_opa(cell, LV_OPA_COVER, 0);
            }

            char dbuf[12];
            snprintf(dbuf, sizeof(dbuf), "%d", day);
            lv_obj_t *lbl = lv_label_create(cell);
            lv_obj_set_style_text_font(lbl, &font_cn_16, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(fg), 0);
            lv_label_set_text(lbl, dbuf);
            lv_obj_center(lbl);
        }
    }

    selection_apply(sel_day);

    // 统计行（跨月浏览时不显示"连续N天"——只在当月有完整上下文）
    int done = 0, undone = 0;
    checkin_count_month(bits, days, &done, &undone);
    int streak = -1;
    if (is_cur) {
        int py = y, pm = m - 1;
        if (pm < 1) { pm = 12; py--; }
        uint64_t prev = checkin_store_get_month(py, pm);
        streak = checkin_streak(bits, prev, y, m, today ? today : days + 1);
        if (!today) streak = 0;
    }
    lv_obj_t *stats = lv_label_create(s_content);
    lv_obj_set_style_text_font(stats, &font_cn_16, 0);
    lv_obj_set_style_text_color(stats, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(stats, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(stats, 240);
    lv_obj_set_pos(stats, 0, 288);
    static char sbuf[64];
    if (streak >= 0)
        snprintf(sbuf, sizeof(sbuf), "完成%d天 · 未完成%d天 · 连续%d天", done, undone, streak);
    else
        snprintf(sbuf, sizeof(sbuf), "完成%d天 · 未完成%d天", done, undone);
    lv_label_set_text(stats, sbuf);
}

void ui_calendar_show(void)
{
    if (s_page) return;   // 已在显示

    int y = 0, m = 0, d = 0;
    s_today_valid = time_sync_today(&y, &m, &d) != 0;
    if (!s_today_valid) {
        // 时间无效：仍可显示框架，格子全灰
        y = 2026; m = 1; d = 0;
    }
    s_today_y = y; s_today_m = m; s_today_d = d;

    s_page = ui_page_create(lv_screen_active());
    // 覆盖在主页上方（z 序 = 创建序）

    s_title = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_title, &font_cn_24, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_title, 240);
    lv_obj_set_pos(s_title, 0, 36);
    lv_label_set_text(s_title, "");

    int days = checkin_days_in_month(y, m);
    rebuild(y, m, (d >= 1 && d <= days) ? d : 1);   // 默认选中今天
}

bool ui_calendar_move_selection(int delta)
{
    if (!s_page || s_sel_day < 1) return false;
    int nd = s_sel_day + delta;
    if (nd < 1) {   // 1 号再往前：上月最后一天
        int py = s_sel_year, pm = s_sel_month - 1;
        if (pm < 1) { pm = 12; py--; }
        rebuild(py, pm, checkin_days_in_month(py, pm));
        return true;
    }
    if (nd > s_days) {   // 月末再往后：下月 1 号
        int ny = s_sel_year, nm = s_sel_month + 1;
        if (nm > 12) { nm = 1; ny++; }
        rebuild(ny, nm, 1);
        return true;
    }
    s_sel_day = nd;
    selection_apply(nd);
    return true;
}

bool ui_calendar_selected_date(int *year, int *month, int *day)
{
    if (!s_page || s_sel_day < 1) return false;
    if (year) *year = s_sel_year;
    if (month) *month = s_sel_month;
    if (day) *day = s_sel_day;
    return true;
}

void ui_calendar_hide(void)
{
    if (!s_page) return;
    lv_obj_delete(s_page);   // content 是其子对象，一并删除
    s_page = NULL;
    s_content = NULL;
    memset(s_cells, 0, sizeof(s_cells));
    s_sel_day = 0;
}

bool ui_calendar_visible(void)
{
    return s_page != NULL;
}
