// main/ui_zcode.c —— zcode 用量页实现
//
// 布局与火山页同款：大标题 + 智谱/套餐等级 chips + 3 行（近5小时 Token、
// 每周 Token、MCP 月度）。Token 行只有服务端算好的百分比，明细显示重置
// 倒计时；MCP 行明细显示 currentValue/usage 次数。
#include "ui_zcode.h"
#include "ui_common.h"

#define ZCODE_TITLE_W_MAX 180

static lv_obj_t *s_title;
static lv_obj_t *s_level_chip;
static lv_obj_t *s_reset[ZCODE_ROWS];
static lv_obj_t *s_bar[ZCODE_ROWS];
static lv_obj_t *s_pct[ZCODE_ROWS];
static lv_obj_t *s_detail[ZCODE_ROWS];

void ui_zcode_build(lv_obj_t *p)
{
    static const char *const row_titles[ZCODE_ROWS] = {
        "近5小时 Token", "每周 Token", "MCP 月度"};

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
        s_detail[i] = ui_put_label(p, "--", 10, y + 40, &app_font_14, C_MUTED);
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

static const char *err_message(int err)
{
    switch (err) {
    case 1:
        return "未配置";
    case 2:
        return "获取失败";
    default:
        return "--";
    }
}

void ui_zcode_render(const zcode_data_t *d, long elapsed_s)
{
    bool ok = d->valid && d->err == 0;

    set_title_font("GLM Coding");
    lv_label_set_text(s_level_chip, d->level[0] ? d->level : "--");

    for (int i = 0; i < ZCODE_ROWS; i++) {
        if (ok && d->has_row[i]) {
            int v = ui_clamp100((float)d->pct[i]);
            lv_bar_set_value(s_bar[i], v, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_bar[i],
                                      lv_color_hex(ui_level_color(v)),
                                      LV_PART_INDICATOR);
            ui_label_printf(s_pct[i], "%d", v);
            long remain = d->reset_s[i] >= 0 ? d->reset_s[i] - elapsed_s : -1;
            if (remain >= 0) {
                format_reset(s_reset[i], remain);
            } else {
                lv_label_set_text(s_reset[i], "--");
            }
            if (i == ZCODE_ROWS - 1 && d->cur[i] >= 0 && d->total[i] > 0) {
                ui_label_printf(s_detail[i], "%ld / %ld 次", d->cur[i], d->total[i]);
            } else {
                lv_label_set_text(s_detail[i], "已用百分比");
            }
        } else {
            lv_bar_set_value(s_bar[i], 0, LV_ANIM_OFF);
            lv_label_set_text(s_pct[i], "--");
            lv_label_set_text(s_reset[i], "--");
            lv_label_set_text(s_detail[i],
                              d->valid ? err_message(d->err) : "等待刷新");
        }
    }
}
