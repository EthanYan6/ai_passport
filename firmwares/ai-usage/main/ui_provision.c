// main/ui_provision.c —— 配网页实现（深色主题）
//
// 布局（240x320，页容器 240x262；顶部让位状态栏）：
//   标题"扫码配网" + QR 白卡 180x180 + 状态三行
// 二维码内容 "WIFI:T:WPA;S:Usage-XXXX;P:12345678;;"（标准 WiFi QR），
// 扫码后手机连上设备热点，门户自动弹出，在门户里填 WiFi 与两家密钥。
// 绘制：LV_EVENT_DRAW_MAIN 回调内逐模块 lv_draw_rect。
#include "ui_provision.h"
#include "ui_common.h"
#include "qrcodegen.h"

#include "lvgl.h"
#include <stdio.h>
#include <string.h>

#define QR_CARD_X 35
#define QR_CARD_Y 26
#define QR_CARD_S 170

static uint8_t s_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
static uint8_t s_qr_temp[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
static int s_qr_size;   // 模块数

static lv_obj_t *s_page;
static lv_obj_t *s_state_line;
static char s_ap_name[33];
static char s_ap_pass[65];

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
    int ox = a.x1 + (QR_CARD_S - total) / 2;
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

void ui_provision_build(lv_obj_t *parent, const char *ap_name, const char *pass)
{
    snprintf(s_ap_name, sizeof(s_ap_name), "%s", ap_name);
    snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", pass);

    // ---- 二维码矩阵 ----
    // 内容 ~40 字节（热点名 10 字符 + 固定骨架），v3 + LOW 足够，
    // 170px 白卡按每模块 5px 恰好容纳 v3 的 29 模块 + 静区。
    char text[96];
    snprintf(text, sizeof(text), "WIFI:T:WPA;S:%s;P:%s;;", ap_name, pass);
    bool ok = qrcodegen_encodeText(text, s_qr_temp, s_qr,
                                   qrcodegen_Ecc_LOW,
                                   qrcodegen_VERSION_MIN, 3,
                                   qrcodegen_Mask_AUTO, true);
    s_qr_size = ok ? (int)qrcodegen_getSize(s_qr) : 0;

    s_page = parent;

    lv_obj_t *title = ui_put_label(s_page, "扫码配网", 0, 0, &app_font_20, C_TEXT);
    lv_obj_set_width(title, 240);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // QR 白卡（深色底上留白保证对比度）。pad 必须清零：canvas 子对象
    // 按内容区定位，默认主题 ~10px 内边距会让二维码相对白框错位。
    lv_obj_t *card = lv_obj_create(s_page);
    lv_obj_set_size(card, QR_CARD_S, QR_CARD_S);
    lv_obj_set_pos(card, QR_CARD_X, QR_CARD_Y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF4F0E6), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *canvas = lv_obj_create(card);
    lv_obj_remove_style_all(canvas);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_set_size(canvas, QR_CARD_S, QR_CARD_S);
    lv_obj_add_event_cb(canvas, qr_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    lv_obj_t *step = ui_put_label(s_page, "扫码连接热点, 打开配网页", 0, 202,
                                  &app_font_14, C_TEXT);
    lv_obj_set_width(step, 240);
    lv_obj_set_style_text_align(step, LV_TEXT_ALIGN_CENTER, 0);

    s_state_line = ui_put_label(s_page, "热点:%s", 0, 222, &app_font_14, C_MUTED);
    lv_obj_set_width(s_state_line, 240);
    lv_obj_set_style_text_align(s_state_line, LV_TEXT_ALIGN_CENTER, 0);
    char line[48];
    snprintf(line, sizeof(line), "热点:%s 密码:%s", ap_name, pass);
    lv_label_set_text(s_state_line, line);

    lv_obj_t *hint = ui_put_label(s_page, "门户里填 WiFi 与密钥", 0, 242,
                                  &app_font_14, C_MUTED);
    lv_obj_set_width(hint, 240);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

void ui_provision_hide(void)
{
    s_page = NULL;
    s_state_line = NULL;
    s_qr_size = 0;
}

void ui_provision_set_state(int state)
{
    if (!s_state_line) return;
    switch (state) {
    case 1:
        lv_obj_set_style_text_color(s_state_line, lv_color_hex(C_TEXT), 0);
        lv_label_set_text(s_state_line, "正在连接 WiFi...");
        break;
    case 2:
        lv_obj_set_style_text_color(s_state_line, lv_color_hex(C_GREEN), 0);
        lv_label_set_text(s_state_line, "已连接, 即将返回");
        break;
    case 3:
        lv_obj_set_style_text_color(s_state_line, lv_color_hex(C_RED), 0);
        lv_label_set_text(s_state_line, "连接失败, 请重新提交");
        break;
    default:
        lv_obj_set_style_text_color(s_state_line, lv_color_hex(C_MUTED), 0);
        lv_label_set_text_fmt(s_state_line, "热点:%s 密码:%s", s_ap_name, s_ap_pass);
        break;
    }
}
