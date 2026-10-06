// main/ui_picker.c —— 主页打卡事项选择弹窗
//
// 主页按上/下键后弹出：半透明遮罩 + 白卡片
//   标题"选择打卡事项" + 状态行（本次要标记的状态着色）
//   事项行：选中行深底白字，未选中白底深字
//   卡片下方提示 "OK 确认 · 长按 OK 取消"
// 上/下键移动选择，OK 确认（由 main.c 落库），长按 OK 取消。
#include "ui_picker.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "checkin_store.h"

#include "lvgl.h"
#include <string.h>

// ---------------- 几何（屏幕 240x320） ----------------
#define PICK_CARD_X    20
#define PICK_CARD_Y    60
#define PICK_CARD_W    200
#define PICK_ROW_H     24
#define PICK_TITLE_Y   10
#define PICK_STATE_Y   40
#define PICK_ROWS_Y    64

static lv_obj_t *s_overlay;                              // 全屏半透明遮罩
static lv_obj_t *s_card;
static lv_obj_t *s_rows[CHECKIN_ITEMS_MAX];              // 事项行
static int s_sel = 0;
static int s_count = 0;

// 刷新全部行的选中样式
static void apply_row_styles(void)
{
    for (int i = 0; i < s_count && i < CHECKIN_ITEMS_MAX; i++) {
        if (!s_rows[i]) continue;
        if (i == s_sel) {
            lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(UI_INK), 0);
            lv_obj_set_style_bg_opa(s_rows[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(s_rows[i], 6, 0);
            lv_obj_set_style_text_color(s_rows[i], lv_color_hex(0xFFFFFF), 0);
        } else {
            lv_obj_set_style_bg_opa(s_rows[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(s_rows[i], lv_color_hex(UI_INK), 0);
        }
    }
}

void ui_picker_show(bool done)
{
    if (s_overlay) return;   // 已在显示

    s_sel = 0;
    s_count = checkin_store_get_item_count();
    if (s_count < 1) s_count = 1;
    if (s_count > CHECKIN_ITEMS_MAX) s_count = CHECKIN_ITEMS_MAX;

    // ---- 全屏遮罩（压暗主页） ----
    s_overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_size(s_overlay, 240, 320);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_60, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // ---- 中央卡片 ----
    int card_h = PICK_ROWS_Y + s_count * PICK_ROW_H + 10;
    s_card = lv_obj_create(s_overlay);
    lv_obj_set_pos(s_card, PICK_CARD_X, PICK_CARD_Y);
    lv_obj_set_size(s_card, PICK_CARD_W, card_h);
    lv_obj_set_style_radius(s_card, 12, 0);
    lv_obj_set_style_bg_color(s_card, lv_color_hex(UI_PANEL), 0);
    lv_obj_set_style_bg_opa(s_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(UI_STROKE), 0);
    lv_obj_set_style_border_width(s_card, 2, 0);
    lv_obj_set_style_shadow_color(s_card, lv_color_hex(0x6B6455), 0);
    lv_obj_set_style_shadow_opa(s_card, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(s_card, 12, 0);
    lv_obj_set_style_shadow_offset_y(s_card, 4, 0);
    lv_obj_set_style_pad_all(s_card, 0, 0);
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);

    // 标题
    lv_obj_t *title = lv_label_create(s_card);
    lv_obj_set_style_text_font(title, &font_cn_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(title, PICK_CARD_W);
    lv_obj_set_pos(title, 0, PICK_TITLE_Y);
    lv_label_set_text(title, "选择打卡事项");

    // 状态行
    lv_obj_t *st = lv_label_create(s_card);
    lv_obj_set_style_text_font(st, &font_cn_16, 0);
    lv_obj_set_style_text_color(st, lv_color_hex(done ? UI_GREEN : UI_RED), 0);
    lv_obj_set_style_text_align(st, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(st, PICK_CARD_W);
    lv_obj_set_pos(st, 0, PICK_STATE_Y);
    lv_label_set_text(st, done ? "状态：完成" : "状态：未完成");

    // 事项行
    char name[CHECKIN_ITEM_NAME_MAX];
    for (int i = 0; i < s_count; i++) {
        lv_obj_t *row = lv_label_create(s_card);
        lv_obj_set_style_text_font(row, &font_cn_16, 0);
        lv_obj_set_pos(row, 14, PICK_ROWS_Y + i * PICK_ROW_H);
        lv_obj_set_width(row, PICK_CARD_W - 28);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        if (!checkin_store_get_item_name(i, name, sizeof(name)))
            strlcpy(name, "事项", sizeof(name));
        lv_label_set_text(row, name);
        s_rows[i] = row;
    }

    // ---- 卡片下方提示 ----
    lv_obj_t *hint = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(hint, &font_cn_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xE8E4D8), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint, 240);
    lv_obj_set_pos(hint, 0, PICK_CARD_Y + card_h + 10);
    lv_label_set_text(hint, "OK 确认 · 长按 OK 取消");

    apply_row_styles();
}

void ui_picker_hide(void)
{
    if (!s_overlay) return;
    lv_obj_delete(s_overlay);   // 一并删除卡片/行/提示
    s_overlay = NULL;
    s_card = NULL;
    for (int i = 0; i < CHECKIN_ITEMS_MAX; i++) s_rows[i] = NULL;
}

bool ui_picker_move(int delta)
{
    if (!s_overlay) return false;
    int ns = s_sel + delta;
    if (ns < 0) ns = 0;
    if (ns > s_count - 1) ns = s_count - 1;
    if (ns == s_sel) return false;
    s_sel = ns;
    apply_row_styles();
    return true;
}

int ui_picker_selected(void)
{
    return s_sel;
}
