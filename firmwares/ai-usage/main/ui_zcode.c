// main/ui_zcode.c —— zcode 用量页实现
//
// 布局与火山页同款：大标题 + 智谱/套餐等级 chips + 3 行（近5小时 Token、
// 每周 Token、MCP 月度）。每行显示重置倒计时、进度条、三角位置标记和
// 右侧两位小数百分比。
#include "ui_zcode.h"
#include "ui_common.h"

#define ZCODE_TITLE_W_MAX 180

static lv_obj_t *s_title;
static lv_obj_t *s_level_chip;
static lv_obj_t *s_reset[ZCODE_ROWS];
static lv_obj_t *s_bar[ZCODE_ROWS];
static lv_obj_t *s_pct[ZCODE_ROWS];
static lv_obj_t *s_marker[ZCODE_ROWS];

void ui_zcode_build(lv_obj_t *p)
{
    static const char *const row_titles[ZCODE_ROWS] = {
        "近5小时 Token", "每周 Token", "MCP 月度"};
    static const lv_point_precise_t marker_points[] = {
        { 0, 6 }, { 4, 0 }, { 8, 6 }, { 0, 6 }};

    s_title = ui_put_label(p, "GLM Coding", 10, 0, &lv_font_montserrat_40, C_TEXT);
    lv_obj_set_width(s_title, ZCODE_TITLE_W_MAX);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);

    ui_make_chip(p, "智谱", 10, 50, 56, C_CYAN);
    s_level_chip = ui_make_chip(p, "--", 74, 50, 72, C_AMBER);

    for (int i = 0; i < ZCODE_ROWS; i++) {
        int y = 86 + i * 58;
        ui_put_label(p, row_titles[i], 10, y, &app_font_14, C_MUTED);
        s_reset[i] = ui_make_label(p, "--", &app_font_14, C_CYAN);
        lv_obj_align(s_reset[i], LV_ALIGN_TOP_RIGHT, -10, y);
        s_bar[i] = ui_make_bar(p, 10, y + 22, 148, 12, C_CYAN);
        s_pct[i] = ui_put_label(p, "--", 166, y + 21, &lv_font_montserrat_14, C_TEXT);
        s_marker[i] = lv_line_create(p);
        lv_line_set_points(s_marker[i], marker_points, 4);
        lv_obj_set_size(s_marker[i], 9, 7);
        lv_obj_set_style_line_width(s_marker[i], 2, 0);
        lv_obj_set_pos(s_marker[i], 6, y + 36);
    }
}

// 与火山页相同的标题自适应字号
static void set_title_font(const char *title)
{
    const lv_font_t *cands[3];
    int n = 0;
    cands[n++] = &lv_font_montserrat_40;
    cands[n++] = &lv_font_montserrat_28;
    cands[n++] = &lv_font_montserrat_20;
    const lv_font_t *font = cands[n - 1];
    for (int i = 0; i < n; i++) {
        lv_point_t size;
        lv_text_get_size(&size, title, cands[i], 0, 0, LV_COORD_MAX,
                         LV_TEXT_FLAG_EXPAND);
        if (size.x <= ZCODE_TITLE_W_MAX) {
            font = cands[i];
            break;
        }
    }
    int title_y = (46 - (int)font->line_height) / 2;
    if (title_y < 0) {
        title_y = 0;
    }
    lv_obj_set_style_text_font(s_title, font, 0);
    lv_obj_set_y(s_title, title_y);
    lv_label_set_text(s_title, title);
}

static void format_reset(lv_obj_t *lbl, long remain)
{
    if (remain >= 86400) {
        ui_label_printf(lbl, "%ldd%ldh后重置", remain / 86400,
                        (remain % 86400) / 3600);
    } else if (remain >= 3600) {
        ui_label_printf(lbl, "%ldh%ldm后重置", remain / 3600, (remain % 3600) / 60);
    } else {
        ui_label_printf(lbl, "%ldm后重置", remain / 60);
    }
}

void ui_zcode_render(const zcode_data_t *d, long elapsed_s)
{
    bool ok = d->valid && d->err == 0;

    set_title_font("GLM Coding");
    lv_label_set_text(s_level_chip, d->level[0] ? d->level : "--");

    for (int i = 0; i < ZCODE_ROWS; i++) {
        if (ok && d->has_row[i]) {
            float pct = d->pct[i];
            int v = ui_clamp100(pct);
            int y = 86 + i * 58;
            lv_bar_set_value(s_bar[i], v, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_bar[i],
                                      lv_color_hex(ui_level_color(pct)),
                                      LV_PART_INDICATOR);
            ui_label_printf(s_pct[i], "%.2f%%", (double)pct);
            lv_obj_set_style_line_color(s_marker[i],
                                        lv_color_hex(ui_level_color(pct)), 0);
            lv_obj_set_pos(s_marker[i], 10 + (v * 148) / 100 - 4, y + 36);
            lv_obj_set_hidden(s_marker[i], false);
            long remain = d->reset_s[i] >= 0 ? d->reset_s[i] - elapsed_s : -1;
            if (remain >= 0) {
                format_reset(s_reset[i], remain);
            } else {
                lv_label_set_text(s_reset[i], "--");
            }
        } else {
            lv_bar_set_value(s_bar[i], 0, LV_ANIM_OFF);
            lv_label_set_text(s_pct[i], "--");
            lv_label_set_text(s_reset[i], "--");
            lv_obj_set_hidden(s_marker[i], true);
        }
    }
}
