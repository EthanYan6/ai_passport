// main/ui_main.c —— 主页：年月/时间 + 大日数字(农历+休/班角标) + 右侧✗/√堆叠 + 底部记录
//
// 顶栏状态栏已移至 ui_theme.c（lv_layer_top()，所有页面共显）。
// 中部（内容带 70..222，垂直居中）：
//   左侧剩余空间(0..182)：农历行 + 大日数字 + 右上角休/班角标，水平居中于 x=91
//   右侧边缘：✗/√ 两条上下堆叠（上=未完成红，下=完成绿），形状保持半遮挡宽胶囊，
//             圆徽章在露出端半圆头中央；无记录时同样显示（按键图例）
// 底部（222..312）：
//   无记录：只显示按键提示（不再显示条）
//   有记录：三列显示今日打卡记录（事项/状态/时间，最新在最上，列对齐）
// 打卡流程：上/下键由 main.c 弹出事项选择窗（ui_picker），OK 确认后调
//           ui_main_mark_item() 落库并立即刷新底部。
#include "ui_main.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "checkin_store.h"
#include "checkin_logic.h"
#include "time_sync.h"
#include "holiday.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

// ---------------- 几何（屏幕 240x320） ----------------
#define BAR_H          56       // 半遮挡宽胶囊条
#define BAR_R          28
#define BAR_W          110
#define BAR_X          182      // 右侧堆叠：露出 182..240
#define BAR_UNDONE_Y   90       // ✗ 上：90..146（与日数字组在内容带 70..222 垂直居中）
#define BAR_DONE_Y     146      // √ 下：146..202
#define BADGE_D        32       // 条内圆徽章直径

#define GROUP_W        182      // 日数字组可用宽度（0..182，右侧留给条）
#define GROUP_CX       91       // 组水平中心 = 剩余空间中心
#define LUNAR_Y        87       // 农历行（内容带内垂直居中：87..103）
#define DAY_Y          109      // 大日数字（96px：109..205）

#define REC_ROWS       5        // 底部最多显示 5 行记录
#define REC_Y0         222      // 222..312
#define REC_ROW_H      18
#define REC_X_ITEM     26       // 三列固定 x：整块 26..214 在 240 宽上居中
#define REC_X_STATE    116
#define REC_X_TIME     174

#define HINT_Y         259      // 无记录提示：底部带 222..312 内垂直居中

// ---------------- 模块状态 ----------------
static lv_obj_t *s_page;

// 中部
static lv_obj_t *s_ym_label;    // "2026年10月 08:15"（年月+时间同一行）
static lv_obj_t *s_day_label;   // 大日数字
static lv_obj_t *s_lunar_label; // 日上方农历行（含节日名）
static lv_obj_t *s_day_badge;  // 日数字右上角"休/班"角标

// 右侧堆叠条 + 底部
static lv_obj_t *s_undone_bar;             // ✗ 条（未完成红，上）
static lv_obj_t *s_done_bar;               // √ 条（完成绿，下）
static lv_obj_t *s_undone_badge;           // ✗ 条内圆徽章
static lv_obj_t *s_done_badge;             // √ 条内圆徽章
static lv_obj_t *s_rec_item[REC_ROWS];     // 记录行：事项列
static lv_obj_t *s_rec_state[REC_ROWS];    // 记录行：状态列
static lv_obj_t *s_rec_time[REC_ROWS];     // 记录行：时间列
static lv_obj_t *s_hint;                   // 无记录时的按键提示

static int s_shown_day = -1;      // 当前显示的"日"，用于跨天/首次进入检测

// ---------------- 右侧堆叠条 ----------------

// 条 + 内部圆徽章（徽章内用符号字画白色 ✗/√）。
// 条固定在右侧堆叠不移动，徽章在露出端（左）半圆头中央（badge_out 可为 NULL）。
static lv_obj_t *bar_create(lv_obj_t *parent, bool done, lv_obj_t **badge_out)
{
    uint32_t fill = done ? UI_GREEN : UI_RED;
    uint32_t soft = done ? UI_GREEN_BG : UI_RED_BG;

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_style_radius(bar, BAR_R, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(soft), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(fill), 0);
    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_60, 0);
    lv_obj_set_style_shadow_color(bar, lv_color_hex(0x6B6455), 0);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(bar, 10, 0);
    lv_obj_set_style_shadow_offset_y(bar, 3, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // 圆徽章：主色实底 + 阴影
    lv_obj_t *badge = lv_obj_create(bar);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_set_size(badge, BADGE_D, BADGE_D);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_shadow_color(badge, lv_color_hex(0x5A463C), 0);
    lv_obj_set_style_shadow_opa(badge, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(badge, 6, 0);
    lv_obj_set_style_shadow_offset_y(badge, 2, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    // 徽章内符号：白 ✗/✓（montserrat_48 缩放到 ~66% 适配 Ø32 徽章）
    lv_obj_t *sym = lv_label_create(badge);
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(sym, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_transform_pivot_x(sym, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(sym, lv_pct(50), 0);
    lv_obj_set_style_transform_scale(sym, 170, 0);   // 256*2/3
    lv_label_set_text(sym, done ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_center(sym);

    if (badge_out) *badge_out = badge;
    return bar;
}

// ---------------- 今日记录渲染 ----------------

// 大日数字装饰：农历行 + 右上角休/班角标（跨天/打卡后随 render_today 更新）
static void update_day_decor(int y, int m, int d)
{
    // 农历行："八月廿六" / 有节日时 "八月十五 · 中秋节"
    char lbuf[48] = "";
    lunar_date_t ld;
    if (holiday_lunar(y, m, d, &ld)) {
        char mn[12], dn[12];
        lunar_month_name(ld.lmonth, ld.leap, mn, sizeof(mn));
        lunar_day_name(ld.lday, dn, sizeof(dn));
        const char *fest = holiday_festival_name(y, m, d);
        if (fest) snprintf(lbuf, sizeof(lbuf), "%s月%s · %s", mn, dn, fest);
        else      snprintf(lbuf, sizeof(lbuf), "%s月%s", mn, dn);
    }
    static char s_lunar_prev[48];   // 内容没变就不重绘
    if (strcmp(s_lunar_prev, lbuf) != 0) {
        strlcpy(s_lunar_prev, lbuf, sizeof(s_lunar_prev));
        lv_label_set_text(s_lunar_label, lbuf);
    }

    // 角标：法定假=红底白字"休"，调休补班=墨底白字"班"，其余隐藏
    int type = holiday_day_type(y, m, d);
    if (type == 2 || type == 3) {
        lv_label_set_text(s_day_badge, type == 2 ? "休" : "班");
        lv_obj_set_style_bg_color(s_day_badge,
            lv_color_hex(type == 2 ? UI_RED : UI_INK), 0);
        // 贴着日数字右上角：量出数字实际宽度再定位（1 位/2 位数字宽不同）
        lv_point_t ts;
        lv_txt_get_size(&ts, lv_label_get_text(s_day_label), &font_day_96,
                        0, 0, LV_COORD_MAX, LV_TEXT_ALIGN_CENTER);
        int right = GROUP_CX + ts.x / 2;
        lv_obj_set_pos(s_day_badge, right + 2, DAY_Y);   // 数字右边缘外侧，不叠字
        lv_obj_clear_flag(s_day_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_day_badge, LV_OBJ_FLAG_HIDDEN);
    }
}

// 按今日记录切换底部区域（首次进入/跨天/打卡后都会调用）：
//   ✗/√ 条固定在右侧堆叠不动；无记录 → 只显示按键提示；
//   有记录 → 隐藏提示，三列显示今日记录
static void render_today(void)
{
    int y, m, d;
    if (!time_sync_today(&y, &m, &d)) return;

    update_day_decor(y, m, d);

    checkin_record_t recs[CHECKIN_RECORDS_MAX];
    int n = checkin_store_get_records(y, m, d, recs, CHECKIN_RECORDS_MAX);

    if (n == 0) {
        for (int i = 0; i < REC_ROWS; i++) {
            lv_obj_add_flag(s_rec_item[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_rec_state[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_rec_time[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);

    // 记录行（store 已按时间倒序返回：最新在前）
    char name[CHECKIN_ITEM_NAME_MAX];
    char tbuf[16];
    for (int i = 0; i < REC_ROWS; i++) {
        if (i < n) {
            bool done = recs[i].state == 2;
            if (!checkin_store_get_item_name(recs[i].item, name, sizeof(name)))
                strlcpy(name, "事项", sizeof(name));
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d",
                     recs[i].minutes / 60, recs[i].minutes % 60);
            lv_label_set_text(s_rec_item[i], name);
            lv_label_set_text(s_rec_state[i], done ? "完成" : "未完成");
            lv_obj_set_style_text_color(s_rec_state[i],
                lv_color_hex(done ? UI_GREEN : UI_RED), 0);
            lv_label_set_text(s_rec_time[i], tbuf);
            lv_obj_clear_flag(s_rec_item[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_rec_state[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_rec_time[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_rec_item[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_rec_state[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_rec_time[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ---------------- 对外接口 ----------------

void ui_main_build(lv_obj_t *screen)
{
    if (s_page) return;   // 幂等

    s_page = ui_page_create(screen);

    // ---- 年月 + 时间（同一行，24px） ----
    s_ym_label = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_ym_label, &font_cn_24, 0);
    lv_obj_set_style_text_color(s_ym_label, lv_color_hex(UI_INK_SOFT), 0);
    lv_obj_set_style_text_align(s_ym_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_ym_label, 240);
    lv_obj_set_pos(s_ym_label, 0, 42);
    lv_label_set_text(s_ym_label, "----年--月 --:--");

    // ---- 大日数字（左侧剩余空间 0..182 内水平居中，农历行在上方） ----
    s_day_label = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_day_label, &font_day_96, 0);
    lv_obj_set_style_text_color(s_day_label, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(s_day_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_day_label, GROUP_W);
    lv_obj_set_pos(s_day_label, 0, DAY_Y);
    lv_label_set_text(s_day_label, "--");

    // ---- 农历行（日上方，"八月廿六 · 中秋节"） ----
    s_lunar_label = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_lunar_label, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lunar_label, lv_color_hex(UI_INK_SOFT), 0);
    lv_obj_set_style_text_align(s_lunar_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lunar_label, GROUP_W);
    lv_obj_set_pos(s_lunar_label, 0, LUNAR_Y);
    lv_label_set_text(s_lunar_label, "");

    // ---- 日数字右上角"休/班"角标（日之后创建，盖在数字上层） ----
    s_day_badge = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_day_badge, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_day_badge, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_day_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_day_badge, 7, 0);
    lv_obj_set_style_pad_hor(s_day_badge, 4, 0);
    lv_obj_set_style_pad_ver(s_day_badge, 2, 0);
    lv_label_set_text(s_day_badge, "");
    lv_obj_add_flag(s_day_badge, LV_OBJ_FLAG_HIDDEN);

    // ---- 右侧堆叠条：上=✗未完成（红），下=√完成（绿）；固定不随记录变化 ----
    s_undone_bar = bar_create(s_page, false, &s_undone_badge);
    lv_obj_set_size(s_undone_bar, BAR_W, BAR_H);
    lv_obj_set_pos(s_undone_bar, BAR_X, BAR_UNDONE_Y);
    lv_obj_align(s_undone_badge, LV_ALIGN_LEFT_MID,
                 BAR_R - BADGE_D / 2 - 2, 0);
    s_done_bar = bar_create(s_page, true, &s_done_badge);
    lv_obj_set_size(s_done_bar, BAR_W, BAR_H);
    lv_obj_set_pos(s_done_bar, BAR_X, BAR_DONE_Y);
    lv_obj_align(s_done_badge, LV_ALIGN_LEFT_MID,
                 BAR_R - BADGE_D / 2 - 2, 0);

    // ---- 底部记录行：三列 × 5 行（初始隐藏） ----
    for (int i = 0; i < REC_ROWS; i++) {
        int ry = REC_Y0 + i * REC_ROW_H;

        s_rec_item[i] = lv_label_create(s_page);
        lv_obj_set_style_text_font(s_rec_item[i], &font_cn_16, 0);
        lv_obj_set_style_text_color(s_rec_item[i], lv_color_hex(UI_INK), 0);
        lv_obj_set_pos(s_rec_item[i], REC_X_ITEM, ry);
        lv_label_set_long_mode(s_rec_item[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_rec_item[i], REC_X_STATE - REC_X_ITEM - 6);
        lv_obj_add_flag(s_rec_item[i], LV_OBJ_FLAG_HIDDEN);

        s_rec_state[i] = lv_label_create(s_page);
        lv_obj_set_style_text_font(s_rec_state[i], &font_cn_16, 0);
        lv_obj_set_pos(s_rec_state[i], REC_X_STATE, ry);
        lv_obj_add_flag(s_rec_state[i], LV_OBJ_FLAG_HIDDEN);

        s_rec_time[i] = lv_label_create(s_page);
        lv_obj_set_style_text_font(s_rec_time[i], &font_cn_16, 0);
        lv_obj_set_style_text_color(s_rec_time[i], lv_color_hex(UI_INK_SOFT), 0);
        lv_obj_set_pos(s_rec_time[i], REC_X_TIME, ry);
        lv_obj_add_flag(s_rec_time[i], LV_OBJ_FLAG_HIDDEN);
    }

    // ---- 无记录时的按键提示（底部带 222..312 内垂直居中） ----
    s_hint = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_hint, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(UI_INK_SOFT), 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_hint, 240);
    lv_obj_set_pos(s_hint, 0, HINT_Y);
    lv_label_set_text(s_hint, "上键 未完成 · 下键 完成");

    s_shown_day = -1;   // 触发首次 tick 全量刷新（首次进入即按记录摆放）
}

void ui_main_tick(void)
{
    if (!s_page) return;

    bool valid = time_sync_valid();
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    int y = lt.tm_year + 1900, m = lt.tm_mon + 1, d = lt.tm_mday;

    if (valid) {
        static char buf[64];
        snprintf(buf, sizeof(buf), "%d年%02d月 %02d:%02d",
                 y, m, lt.tm_hour, lt.tm_min);
        lv_label_set_text(s_ym_label, buf);
        snprintf(buf, sizeof(buf), "%d", d);
        lv_label_set_text(s_day_label, buf);
    }

    // 跨天或首次进入：重新渲染今日底部（记录/条位置）
    if (valid && d != s_shown_day) {
        s_shown_day = d;
        render_today();
    } else if (!valid) {
        lv_label_set_text(s_ym_label, "----年--月 --:--");
        lv_label_set_text(s_day_label, "--");
    }
}

void ui_main_reload(void)
{
    // 强制下一 tick 重载今日记录（配网页改过事项列表后调用）
    s_shown_day = -1;
}

bool ui_main_mark_item(int item, bool done)
{
    if (!s_page) return false;
    if (!time_sync_valid()) return false;   // 时间没对上，无法归属到"某一天"

    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    int y = lt.tm_year + 1900, m = lt.tm_mon + 1, d = lt.tm_mday;

    // upsert 落库（同事项只更新状态与时间）并同步月位图，然后立即重渲染
    checkin_store_mark_item(y, m, d, item, done ? 2 : 1,
                             lt.tm_hour * 60 + lt.tm_min);
    s_shown_day = d;
    render_today();
    return true;
}

void ui_main_set_wifi_state(wifi_mgr_state_t state)
{
    // 顶栏已移至 top layer 状态栏（ui_theme.c），这里只做状态→文案/配色映射
    switch (state) {
    case WIFI_MGR_CONNECTED: {
        char ssid[33] = { 0 };
        if (wifi_manager_get_ssid(ssid, sizeof(ssid)) && ssid[0]) {
            ui_statusbar_set_wifi(ssid, UI_INK, UI_GREEN);
        } else {
            ui_statusbar_set_wifi("已连接", UI_INK, UI_GREEN);
        }
        break;
    }
    case WIFI_MGR_CONNECTING:
        ui_statusbar_set_wifi("连接中", UI_INK_SOFT, UI_INK_SOFT);
        break;
    case WIFI_MGR_FAILED:
        ui_statusbar_set_wifi("连接失败", UI_RED, UI_RED);
        break;
    default:
        ui_statusbar_set_wifi("未连接", UI_INK_SOFT, UI_INK_SOFT);
        break;
    }
}
