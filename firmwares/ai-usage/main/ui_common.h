// main/ui_common.h —— 深色科技风主题与组件工厂（取自 pc-monitor app_pcmon.c）
#pragma once

#include "lvgl.h"
#include <stdint.h>

// 中文子集字库（tools/gen_app_fonts.py 按 main/*.c 字符串生成）
LV_FONT_DECLARE(app_font_14);
LV_FONT_DECLARE(app_font_20);
LV_FONT_DECLARE(app_font_40);

#define C_BG 0x060A12
#define C_FRAME 0x16283A
#define C_DIM 0x2A4356
#define C_TEXT 0xE6F7FF
#define C_MUTED 0x6E8CA0
#define C_CYAN 0x2FE6FF
#define C_GREEN 0x39FF88
#define C_AMBER 0xFFC24D
#define C_RED 0xFF4D6D

// 用量等级配色：>=90 红 / >=70 黄 / 其余绿
uint32_t ui_level_color(int level);

int ui_clamp100(float v);

lv_obj_t *ui_make_label(lv_obj_t *parent, const char *text,
                        const lv_font_t *font, uint32_t color);
lv_obj_t *ui_put_label(lv_obj_t *parent, const char *text, int x, int y,
                       const lv_font_t *font, uint32_t color);
lv_obj_t *ui_make_container(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *ui_make_bar(lv_obj_t *parent, int x, int y, int w, int h,
                      uint32_t color);
// 1px 圆角描边小标签，返回内部 label 便于改字
lv_obj_t *ui_make_chip(lv_obj_t *parent, const char *text, int x, int y, int w,
                       uint32_t color);

// 经 snprintf 的 label 格式化（LVGL 内置 %f 不可用）
void ui_label_printf(lv_obj_t *lbl, const char *fmt, ...);

void ui_set_hidden(lv_obj_t *obj, bool hidden);
