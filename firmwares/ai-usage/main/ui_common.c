// main/ui_common.c —— 主题与组件工厂实现
#include "ui_common.h"

#include <stdarg.h>
#include <stdio.h>

uint32_t ui_level_color(int level)
{
    if (level >= 2) {
        return C_RED;
    }
    if (level == 1) {
        return C_AMBER;
    }
    return C_GREEN;
}

int ui_clamp100(float v)
{
    if (v < 0.0f) {
        return 0;
    }
    if (v > 100.0f) {
        return 100;
    }
    return (int)(v + 0.5f);
}

lv_obj_t *ui_make_label(lv_obj_t *parent, const char *text,
                        const lv_font_t *font, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    return lbl;
}

lv_obj_t *ui_put_label(lv_obj_t *parent, const char *text, int x, int y,
                       const lv_font_t *font, uint32_t color)
{
    lv_obj_t *lbl = ui_make_label(parent, text, font, color);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

lv_obj_t *ui_make_container(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

lv_obj_t *ui_make_bar(lv_obj_t *parent, int x, int y, int w, int h,
                      uint32_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, h);
    lv_obj_set_pos(bar, x, y);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_DIM), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_NONE, LV_PART_INDICATOR);
    return bar;
}

lv_obj_t *ui_make_chip(lv_obj_t *parent, const char *text, int x, int y, int w,
                       uint32_t color)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_set_size(chip, w, 22);
    lv_obj_set_pos(chip, x, y);
    lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(color), 0);
    lv_obj_set_style_radius(chip, 4, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *lbl = ui_make_label(chip, text, &app_font_14, color);
    // 持久居中：运行时换字后仍保持居中（一次性 align 会锚在旧宽度边缘）
    lv_obj_set_align(lbl, LV_ALIGN_CENTER);
    return lbl;
}

void ui_label_printf(lv_obj_t *lbl, const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lv_label_set_text(lbl, buf);
}

void ui_set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}
