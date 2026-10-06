// main/ui_standby.c —— 休眠页实现
//
// 墨水屏质感：深炭灰底（不纯黑）+ 柔灰字（不纯白），低对比不刺眼；
// 配合 main.c 在休眠时把背光调暗到 15%，进一步消除"发光感"。
//   年月日 y=64（cn_24 柔白，居中）
//   名片：名称 y=150（cn_24 柔白）、职位 y=192（cn_16 绿）、介绍 y=218（cn_16 中灰）
//   底部提示 y=292（暗灰小字）
#include "ui_standby.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "checkin_store.h"
#include "time_sync.h"

#include "lvgl.h"
#include <stdio.h>

static lv_obj_t *s_page;

void ui_standby_show(void)
{
    if (s_page) return;

    s_page = ui_page_create(lv_screen_active());
    lv_obj_set_style_bg_color(s_page, lv_color_hex(0x1B1E22), 0);   // 深炭灰，不纯黑

    // 年月日
    char buf[48];
    int y = 0, m = 0, d = 0;
    if (time_sync_today(&y, &m, &d)) {
        snprintf(buf, sizeof(buf), "%d年%02d月%d日", y, m, d);
    } else {
        snprintf(buf, sizeof(buf), "----年--月--日");
    }

    lv_obj_t *date = lv_label_create(s_page);
    lv_obj_set_style_text_font(date, &font_cn_24, 0);
    lv_obj_set_style_text_color(date, lv_color_hex(0xC2C7CC), 0);   // 柔白
    lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(date, 240);
    lv_obj_set_pos(date, 0, 64);
    lv_label_set_text(date, buf);

    // 名片
    card_info_t card;
    checkin_store_get_card(&card);

    lv_obj_t *name = lv_label_create(s_page);
    lv_obj_set_style_text_font(name, &font_cn_24, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0xD6DADE), 0);   // 最亮的一行（柔白）
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(name, 240);
    lv_obj_set_pos(name, 0, 150);
    lv_label_set_text(name, card.name);

    lv_obj_t *title = lv_label_create(s_page);
    lv_obj_set_style_text_font(title, &font_cn_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x6FA873), 0);   // 职位：柔绿
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(title, 240);
    lv_obj_set_pos(title, 0, 192);
    lv_label_set_text(title, card.title);

    lv_obj_t *bio = lv_label_create(s_page);
    lv_obj_set_style_text_font(bio, &font_cn_16, 0);
    lv_obj_set_style_text_color(bio, lv_color_hex(0x9AA0A6), 0);    // 中灰
    lv_obj_set_style_text_align(bio, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(bio, 240);
    lv_obj_set_pos(bio, 0, 218);
    lv_label_set_text(bio, card.bio);

    // 唤醒提示
    lv_obj_t *hint = lv_label_create(s_page);
    lv_obj_set_style_text_font(hint, &font_cn_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x5C636B), 0);   // 更暗的提示
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint, 240);
    lv_obj_set_pos(hint, 0, 292);
    lv_label_set_text(hint, "按任意键唤醒");
}

void ui_standby_hide(void)
{
    if (!s_page) return;
    lv_obj_delete(s_page);
    s_page = NULL;
}

bool ui_standby_visible(void)
{
    return s_page != NULL;
}
