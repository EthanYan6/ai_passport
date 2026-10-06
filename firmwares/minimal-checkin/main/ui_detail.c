// main/ui_detail.c —— 单日打卡详情页
//
// 布局（240x320，米纸底；顶部 0..28 让位 top layer 状态栏）：
//   标题 "2026年10月6日 周二"  y=36（cn_24）
//   当日最终状态行（来自位图）  y=64
//   打卡流水三列（事项/状态/时间，x 与主页一致 26/116/174），每行 20px，y 起 94
//   底部提示 "按 OK 返回月历"    y=298
#include "ui_detail.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "checkin_store.h"
#include "checkin_logic.h"

#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_page;

void ui_detail_show(int year, int month, int day)
{
    if (s_page) return;   // 已在显示

    s_page = ui_page_create(lv_screen_active());
    // 创建在月历之后 → 覆盖其上方（z 序 = 创建序）

    // ---- 标题：日期 + 星期 ----
    static const char *WEEKS[7] = { "日", "一", "二", "三", "四", "五", "六" };
    char buf[40];
    snprintf(buf, sizeof(buf), "%d年%d月%d日 周%s",
             year, month, day, WEEKS[checkin_weekday(year, month, day)]);
    lv_obj_t *title = lv_label_create(s_page);
    lv_obj_set_style_text_font(title, &font_cn_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(title, 240);
    lv_obj_set_pos(title, 0, 36);
    lv_label_set_text(title, buf);

    // ---- 当日最终状态（来自位图；旧固件标记的日子可能有状态而无流水） ----
    uint64_t bits = checkin_store_get_month(year, month);
    checkin_state_t st = checkin_get(bits, day);
    const char *st_text = "状态：未打卡";
    uint32_t st_color = UI_INK_SOFT;
    if (st == CHECKIN_DONE)         { st_text = "状态：完成";   st_color = UI_GREEN; }
    else if (st == CHECKIN_UNDONE)  { st_text = "状态：未完成"; st_color = UI_RED; }
    lv_obj_t *state = lv_label_create(s_page);
    lv_obj_set_style_text_font(state, &font_cn_16, 0);
    lv_obj_set_style_text_color(state, lv_color_hex(st_color), 0);
    lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(state, 240);
    lv_obj_set_pos(state, 0, 64);
    lv_label_set_text(state, st_text);

    // ---- 打卡流水（每次标记的时间 + 结果） ----
    checkin_record_t recs[CHECKIN_RECORDS_MAX];
    int n = checkin_store_get_records(year, month, day, recs, CHECKIN_RECORDS_MAX);

    if (n == 0) {
        lv_obj_t *empty = lv_label_create(s_page);
        lv_obj_set_style_text_font(empty, &font_cn_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(UI_INK_SOFT), 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(empty, 240);
        lv_obj_set_pos(empty, 0, 150);
        lv_label_set_text(empty, "当日无打卡时间记录");
    }
    for (int i = 0; i < n; i++) {
        int ry = 94 + i * 20;
        bool done = recs[i].state == 2;

        // 三列固定 x（与主页一致）：整块 26..214 水平居中，各列上下对齐
        char name[CHECKIN_ITEM_NAME_MAX];
        if (!checkin_store_get_item_name(recs[i].item, name, sizeof(name)))
            strlcpy(name, "事项", sizeof(name));

        lv_obj_t *it = lv_label_create(s_page);
        lv_obj_set_style_text_font(it, &font_cn_16, 0);
        lv_obj_set_style_text_color(it, lv_color_hex(UI_INK), 0);
        lv_obj_set_pos(it, 26, ry);
        lv_label_set_long_mode(it, LV_LABEL_LONG_DOT);
        lv_obj_set_width(it, 116 - 26 - 6);
        lv_label_set_text(it, name);

        lv_obj_t *rs = lv_label_create(s_page);
        lv_obj_set_style_text_font(rs, &font_cn_16, 0);
        lv_obj_set_style_text_color(rs, lv_color_hex(done ? UI_GREEN : UI_RED), 0);
        lv_obj_set_pos(rs, 116, ry);
        lv_label_set_text(rs, done ? "完成" : "未完成");

        lv_obj_t *tm = lv_label_create(s_page);
        lv_obj_set_style_text_font(tm, &font_cn_16, 0);
        lv_obj_set_style_text_color(tm, lv_color_hex(UI_INK_SOFT), 0);
        lv_obj_set_pos(tm, 174, ry);
        snprintf(buf, sizeof(buf), "%02d:%02d",
                 recs[i].minutes / 60, recs[i].minutes % 60);
        lv_label_set_text(tm, buf);
    }

    // ---- 底部提示 ----
    lv_obj_t *hint = lv_label_create(s_page);
    lv_obj_set_style_text_font(hint, &font_cn_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_INK_SOFT), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint, 240);
    lv_obj_set_pos(hint, 0, 298);
    lv_label_set_text(hint, "按 OK 返回月历");
}

void ui_detail_hide(void)
{
    if (!s_page) return;
    lv_obj_delete(s_page);
    s_page = NULL;
}
