// main/ui_bailian.c —— 阿里云百炼 Coding Plan 真实用量页
#include "ui_bailian.h"
#include "ui_common.h"

#include <stdio.h>

static lv_obj_t *s_state_chip;
static lv_obj_t *s_used[BAILIAN_QUOTA_MAX];
static lv_obj_t *s_percent[BAILIAN_QUOTA_MAX];
static lv_obj_t *s_bar[BAILIAN_QUOTA_MAX];
static lv_obj_t *s_state;
static lv_obj_t *s_elapsed;

void ui_bailian_build(lv_obj_t *p)
{
    static const char *names[BAILIAN_QUOTA_MAX] = { "5小时", "每周", "每月" };

    ui_put_label(p, "阿里云百炼", 10, 0, &app_font_40, C_TEXT);
    ui_make_chip(p, "Coding Plan", 10, 52, 130, C_CYAN);
    s_state_chip = ui_make_chip(p, "WAIT", 148, 52, 80, C_AMBER);

    lv_obj_t *panel = ui_make_container(p, 10, 94, 220, 132);
    lv_obj_set_style_bg_color(panel, lv_color_hex(C_FRAME), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 6, 0);

    for (int i = 0; i < BAILIAN_QUOTA_MAX; i++) {
        int y = 6 + i * 42;
        ui_put_label(panel, names[i], 12, y, &app_font_20, C_MUTED);
        s_percent[i] = ui_put_label(panel, "--", 72, y, &app_font_20, C_TEXT);
        s_used[i] = ui_put_label(panel, "--/--", 126, y + 4,
                                 &lv_font_montserrat_14, C_MUTED);
        s_bar[i] = ui_make_bar(panel, 12, y + 27, 196, 6, C_GREEN);
    }

    s_state = ui_put_label(p, "等待配置", 10, 234, &app_font_20, C_AMBER);
    s_elapsed = ui_put_label(p, "", 184, 238, &lv_font_montserrat_14, C_MUTED);
}

void ui_bailian_render(const bailian_data_t *data, long elapsed_s)
{
    if (!s_state) return;

    bool ok = data->err == 0 && data->quota_count > 0;
    for (int i = 0; i < BAILIAN_QUOTA_MAX; i++) {
        bool valid = ok && i < data->quota_count && data->quotas[i].valid;
        if (!valid) {
            lv_label_set_text(s_used[i], "--/--");
            lv_label_set_text(s_percent[i], "--");
            lv_bar_set_value(s_bar[i], 0, LV_ANIM_OFF);
            continue;
        }
        const bailian_quota_t *quota = &data->quotas[i];
        uint32_t color = ui_level_color(quota->percent >= 90 ? 2 :
                                        quota->percent >= 70 ? 1 : 0);
        ui_label_printf(s_used[i], "%d/%d", quota->used, quota->total);
        ui_label_printf(s_percent[i], "%d%%", quota->percent);
        lv_obj_set_style_text_color(s_percent[i], lv_color_hex(color), 0);
        lv_obj_set_style_bg_color(s_bar[i], lv_color_hex(color), LV_PART_INDICATOR);
        lv_bar_set_value(s_bar[i], quota->percent, LV_ANIM_OFF);
    }

    const char *state = "真实用量";
    const char *chip = "LIVE";
    uint32_t color = C_GREEN;
    if (data->err == 0 && data->quota_count == 0) {
        state = "等待首次查询";
        chip = "WAIT";
        color = C_AMBER;
    } else if (data->err == 1) {
        state = "配置百炼 AK/SK";
        chip = "NO KEY";
        color = C_AMBER;
    } else if (data->err == 2) {
        state = "查询或鉴权失败";
        chip = "ERROR";
        color = C_RED;
    } else if (data->err == 3) {
        state = "等待网络对时";
        chip = "WAIT";
        color = C_AMBER;
    } else if (data->err == 4) {
        state = "响应格式异常";
        chip = "ERROR";
        color = C_RED;
    }
    lv_label_set_text(s_state, state);
    lv_obj_set_style_text_color(s_state, lv_color_hex(color), 0);
    lv_label_set_text(s_state_chip, chip);
    lv_obj_set_style_text_color(s_state_chip, lv_color_hex(color), 0);

    if (elapsed_s > 0) {
        ui_label_printf(s_elapsed, "%lds", elapsed_s);
    } else {
        lv_label_set_text(s_elapsed, "");
    }
}
