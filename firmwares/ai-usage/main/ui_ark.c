// main/ui_ark.c —— 火山方舟用量页实现
//
// 布局（页容器 240x262，深色底）：
//   大标题 "Coding Plan"（宽度受限自动降字号）+ 火山方舟/套餐 chips
//   3 个窗口行：标题 + 右侧重置倒计时 + 进度条 + 百分比 + 明细
#include "ui_ark.h"
#include "ui_common.h"

#include <stdio.h>

// 大标题止步于顶栏电池标签（右对齐 ~40px）之外
#define ARK_TITLE_W_MAX 180

static lv_obj_t *s_title;
static lv_obj_t *s_plan_chip;
static lv_obj_t *s_reset[ARK_WINDOWS];
static lv_obj_t *s_bar[ARK_WINDOWS];
static lv_obj_t *s_pct[ARK_WINDOWS];
static lv_obj_t *s_detail[ARK_WINDOWS];

void ui_ark_build(lv_obj_t *p)
{
    static const char *const win_titles[ARK_WINDOWS] = {
        "近5小时用量", "近一周用量", "近一月用量"};

    s_title = ui_put_label(p, "Coding Plan", 10, 0, &lv_font_montserrat_40, C_TEXT);
    lv_obj_set_width(s_title, ARK_TITLE_W_MAX);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);

    ui_make_chip(p, "火山方舟", 10, 50, 68, C_CYAN);
    s_plan_chip = ui_make_chip(p, "--", 86, 50, 60, C_AMBER);

    for (int i = 0; i < ARK_WINDOWS; i++) {
        int y = 86 + i * 58;
        ui_put_label(p, win_titles[i], 10, y, &app_font_14, C_MUTED);
        s_reset[i] = ui_make_label(p, "--", &app_font_14, C_CYAN);
        lv_obj_align(s_reset[i], LV_ALIGN_TOP_RIGHT, -10, y);
        s_bar[i] = ui_make_bar(p, 10, y + 22, 148, 12, C_CYAN);
        s_pct[i] = ui_put_label(p, "--", 166, y + 21, &lv_font_montserrat_14, C_TEXT);
        s_detail[i] = ui_put_label(p, "--", 10, y + 40, &app_font_14, C_MUTED);
    }
}

static void set_title_font(const char *title)
{
    // 选最大能放进宽度限制的字号；纯 ASCII 用 montserrat，中文用子集字库
    bool ascii = true;
    for (const char *q = title; *q; q++) {
        if ((unsigned char)*q >= 0x80) {
            ascii = false;
            break;
        }
    }
    const lv_font_t *cands[3];
    int n = 0;
    if (ascii) {
        cands[n++] = &lv_font_montserrat_40;
        cands[n++] = &lv_font_montserrat_28;
        cands[n++] = &lv_font_montserrat_20;
    } else {
        cands[n++] = &app_font_40;
        cands[n++] = &app_font_20;
    }
    const lv_font_t *font = cands[n - 1];
    for (int i = 0; i < n; i++) {
        lv_point_t size;
        lv_text_get_size(&size, title, cands[i], 0, 0, LV_COORD_MAX,
                         LV_TEXT_FLAG_EXPAND);
        if (size.x <= ARK_TITLE_W_MAX) {
            font = cands[i];
            break;
        }
    }
    // 在 chips 行上方的 ~46px 带内垂直居中
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
    case 3:
        return "时间未同步";
    default:
        return "--";
    }
}

void ui_ark_render(const ark_data_t *d, long elapsed_s)
{
    bool ok = d->valid && d->err == 0;

    set_title_font("Coding Plan");

    lv_label_set_text(s_plan_chip, d->plan[0] ? d->plan : "--");

    for (int i = 0; i < ARK_WINDOWS; i++) {
        bool has = ok && d->has_win[i] && d->quota[i] > 0.0f;
        if (has) {
            float pct = d->used[i] * 100.0f / d->quota[i];
            int v = ui_clamp100(pct);
            lv_bar_set_value(s_bar[i], v, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_bar[i],
                                      lv_color_hex(ui_level_color(pct)),
                                      LV_PART_INDICATOR);
            ui_label_printf(s_pct[i], "%d", v);
            long remain = d->reset_s[i] >= 0 ? d->reset_s[i] - elapsed_s : -1;
            if (remain >= 0) {
                format_reset(s_reset[i], remain);
            } else {
                lv_label_set_text(s_reset[i], "--");
            }
            ui_label_printf(s_detail[i], "%.0f / %.0f", (double)d->used[i],
                            (double)d->quota[i]);
        } else {
            lv_bar_set_value(s_bar[i], 0, LV_ANIM_OFF);
            lv_label_set_text(s_pct[i], "--");
            lv_label_set_text(s_reset[i], "--");
            lv_label_set_text(s_detail[i],
                              d->valid ? err_message(d->err) : "等待刷新");
        }
    }
}
