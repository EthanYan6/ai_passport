// main/ui_provision.c —— 配网页：扫码连热点 → 手机门户配置
//
// 布局（240x320，米纸底；顶部 0..28 让位 top layer 状态栏）：
//   标题"WiFi 配网"  y=36
//   QR 白卡 180x180 @(30,64)（含二维码）
//   状态行 y=252 / y=272 / y=292
// 二维码内容 "WIFI:T:WPA;S:Checkin-XXXX;P:12345678;;"（标准 WiFi QR）。
// 绘制：LV_EVENT_DRAW_MAIN 回调内逐模块 lv_draw_rect。
#include "ui_provision.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "qrcodegen.h"

#include "lvgl.h"
#include <stdio.h>
#include <string.h>

#define QR_CARD_X   30
#define QR_CARD_Y   64
#define QR_CARD_S   180

// 二维码缓冲（内容 40 字节级别 → v3 足够；放宽到 v4 上限）
static uint8_t s_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
static uint8_t s_qr_temp[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
static int s_qr_size;   // 模块数（如 v3 = 29）

static lv_obj_t *s_page;
static lv_obj_t *s_state_line;
static lv_obj_t *s_hint_line;
static char s_ap_name[33];   // 热点名：等待状态时显示在状态行

// QR 绘制回调：黑模块画 5x5 方块（含 2 模块静区）
static void qr_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (s_qr_size <= 0) return;

    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    const int scale = 5;                 // 每模块 5px
    const int quiet = 2;                 // 静区 2 模块
    const int total = (s_qr_size + quiet * 2) * scale;
    int ox = a.x1 + (QR_CARD_S - total) / 2;   // 画布左上原点
    int oy = a.y1 + (QR_CARD_S - total) / 2;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(0x1A1A1A);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = 0;

    lv_area_t cell;
    for (int y = 0; y < s_qr_size; y++) {
        for (int x = 0; x < s_qr_size; x++) {
            if (!qrcodegen_getModule(s_qr, x, y)) continue;
            cell.x1 = ox + (x + quiet) * scale;
            cell.y1 = oy + (y + quiet) * scale;
            cell.x2 = cell.x1 + scale - 1;
            cell.y2 = cell.y1 + scale - 1;
            lv_draw_rect(layer, &dsc, &cell);
        }
    }
}

void ui_provision_show(const char *ap_name, const char *pass)
{
    if (s_page) return;   // 已在显示
    snprintf(s_ap_name, sizeof(s_ap_name), "%s", ap_name);

    // ---- 生成二维码矩阵 ----
    char text[96];
    snprintf(text, sizeof(text), "WIFI:T:WPA;S:%s;P:%s;;", ap_name, pass);
    bool ok = qrcodegen_encodeText(text, s_qr_temp, s_qr,
                                   qrcodegen_Ecc_LOW,
                                   qrcodegen_VERSION_MIN, 4,
                                   qrcodegen_Mask_AUTO, true);
    s_qr_size = ok ? (int)qrcodegen_getSize(s_qr) : 0;

    // ---- 页面 ----
    s_page = ui_page_create(lv_screen_active());

    lv_obj_t *title = lv_label_create(s_page);
    lv_obj_set_style_text_font(title, &font_cn_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(title, 240);
    lv_obj_set_pos(title, 0, 36);
    lv_label_set_text(title, "WiFi 配网");

    // QR 白卡（拟物阴影）
    lv_obj_t *card = ui_panel_create(s_page, QR_CARD_X, QR_CARD_Y, QR_CARD_S, QR_CARD_S);
    lv_obj_t *canvas = lv_obj_create(card);
    lv_obj_remove_style_all(canvas);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_set_size(canvas, QR_CARD_S, QR_CARD_S);
    lv_obj_add_event_cb(canvas, qr_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    // 状态行
    lv_obj_t *step = lv_label_create(s_page);
    lv_obj_set_style_text_font(step, &font_cn_16, 0);
    lv_obj_set_style_text_color(step, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(step, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(step, 240);
    lv_obj_set_pos(step, 0, 252);
    lv_label_set_text(step, "1. 扫码连接热点，打开配网页");

    s_state_line = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_state_line, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_state_line, lv_color_hex(UI_INK_SOFT), 0);
    lv_obj_set_style_text_align(s_state_line, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_state_line, 240);
    lv_obj_set_pos(s_state_line, 0, 272);
    char line[48];
    snprintf(line, sizeof(line), "热点:%s", ap_name);
    lv_label_set_text(s_state_line, line);

    s_hint_line = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_hint_line, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_hint_line, lv_color_hex(UI_INK_SOFT), 0);
    lv_obj_set_style_text_align(s_hint_line, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_hint_line, 240);
    lv_obj_set_pos(s_hint_line, 0, 294);
    char hint[48];
    snprintf(hint, sizeof(hint), "密码:%s", pass);
    lv_label_set_text(s_hint_line, hint);
}

void ui_provision_hide(void)
{
    if (!s_page) return;
    lv_obj_delete(s_page);
    s_page = NULL;
    s_state_line = NULL;
    s_hint_line = NULL;
}

bool ui_provision_visible(void)
{
    return s_page != NULL;
}

void ui_provision_set_state(int state)
{
    if (!s_state_line) return;
    switch (state) {
    case 1:
        lv_obj_set_style_text_color(s_state_line, lv_color_hex(UI_INK), 0);
        lv_label_set_text(s_state_line, "正在连接 WiFi…");
        break;
    case 2:
        lv_obj_set_style_text_color(s_state_line, lv_color_hex(UI_GREEN), 0);
        lv_label_set_text(s_state_line, "已连接，即将返回主页");
        break;
    case 3:
        lv_obj_set_style_text_color(s_state_line, lv_color_hex(UI_RED), 0);
        lv_label_set_text(s_state_line, "连接失败，请重新提交");
        break;
    default:
        lv_obj_set_style_text_color(s_state_line, lv_color_hex(UI_INK_SOFT), 0);
        lv_label_set_text_fmt(s_state_line, "热点:%s", s_ap_name);
        break;
    }
}
