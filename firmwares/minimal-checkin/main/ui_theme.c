// main/ui_theme.c —— 主题实现
#include "ui_theme.h"
#include "ui_fonts.h"
#include "bsp_battery.h"

#include <stdio.h>
#include <string.h>

// ---------------- 页面/面板 ----------------

lv_obj_t *ui_page_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(UI_PAPER), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

lv_obj_t *ui_panel_create(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_STROKE), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_60, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0x6B6455), 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(panel, 14, 0);
    lv_obj_set_style_shadow_offset_y(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

// ---------------- 电池 ----------------
// 结构：容器内一个自绘电池对象（draw 回调）+ 百分比 label

typedef struct {
    lv_obj_t *body;      // 自绘电池
    lv_obj_t *label;     // 百分比文字
    bool dark;           // 深色底（休眠页）配色
} battery_widget_t;

static void battery_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    battery_widget_t *bw = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int w = (int)(a.x2 - a.x1 + 1);
    int h = (int)(a.y2 - a.y1 + 1);

    // 读电量
    int soc = bsp_battery_soc();

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);

    // 1) 电池外壳（圆角矩形描边；深色底时用柔灰描边+深底）
    dsc.bg_color = lv_color_hex(bw->dark ? 0x1B1E22 : UI_PANEL);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = 2;
    dsc.border_color = lv_color_hex(bw->dark ? 0x8A9098 : UI_INK);
    dsc.border_opa = LV_OPA_80;
    dsc.border_width = 2;
    lv_area_t shell = { .x1 = a.x1, .y1 = a.y1, .x2 = a.x1 + w - 5, .y2 = a.y2 };
    lv_draw_rect(layer, &dsc, &shell);

    // 2) 电池正极凸起
    dsc.bg_color = lv_color_hex(bw->dark ? 0x8A9098 : UI_INK);
    dsc.bg_opa = LV_OPA_80;
    dsc.border_width = 0;
    dsc.radius = 1;
    lv_area_t cap = { .x1 = a.x1 + w - 4, .y1 = a.y1 + h / 2 - 2,
                      .x2 = a.x1 + w - 1, .y2 = a.y1 + h / 2 + 2 };
    lv_draw_rect(layer, &dsc, &cap);

    // 3) 电量填充：宽度按 SOC 百分比；未知则不填
    if (soc >= 0) {
        if (soc > 100) soc = 100;
        lv_color_t fill = soc <= 20 ? lv_color_hex(UI_RED)
                        : soc <= 40 ? lv_color_hex(0xE0A63F)
                        : lv_color_hex(UI_GREEN);
        dsc.bg_color = fill;
        dsc.bg_opa = LV_OPA_COVER;
        dsc.radius = 1;
        int inner_w = ((w - 5) - 4) * soc / 100;
        lv_area_t fill_area = {
            .x1 = a.x1 + 3, .y1 = a.y1 + 3,
            .x2 = a.x1 + 3 + inner_w - 1, .y2 = a.y2 - 3,
        };
        if (inner_w > 0) lv_draw_rect(layer, &dsc, &fill_area);
    }
    (void)bw;
}

lv_obj_t *ui_battery_create(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_size(cont, 68, 16);

    battery_widget_t *bw = lv_malloc(sizeof(battery_widget_t));
    bw->body = lv_obj_create(cont);
    lv_obj_remove_style_all(bw->body);
    lv_obj_set_pos(bw->body, 0, 1);
    lv_obj_set_size(bw->body, 26, 14);
    lv_obj_add_event_cb(bw->body, battery_draw_cb, LV_EVENT_DRAW_MAIN, bw);

    bw->label = lv_label_create(cont);
    lv_obj_set_pos(bw->label, 30, 0);
    lv_obj_set_style_text_font(bw->label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bw->label, lv_color_hex(UI_INK), 0);
    lv_label_set_text(bw->label, "--%");

    lv_obj_set_user_data(cont, bw);
    ui_battery_refresh(cont);
    return cont;
}

void ui_battery_refresh(lv_obj_t *battery_widget)
{
    if (!battery_widget) return;
    // 子控件顺序：body, label
    battery_widget_t *bw = lv_obj_get_user_data(battery_widget);
    if (!bw) {
        // user_data 尚未挂上时跳过（构造完成后一定有）
        return;
    }
    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_label_set_text(bw->label, "--%");
    } else {
        if (soc > 100) soc = 100;
        lv_label_set_text_fmt(bw->label, "%d%%", soc);
    }
    lv_obj_invalidate(battery_widget);
}

void ui_battery_set_dark(lv_obj_t *battery_widget, bool dark)
{
    if (!battery_widget) return;
    battery_widget_t *bw = lv_obj_get_user_data(battery_widget);
    if (!bw || bw->dark == dark) return;
    bw->dark = dark;
    lv_obj_set_style_text_color(bw->label,
        lv_color_hex(dark ? 0xC2C7CC : UI_INK), 0);
    lv_obj_invalidate(battery_widget);
}

// ---------------- WiFi 图标 ----------------

lv_obj_t *ui_wifi_icon_create(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *icon = lv_label_create(parent);
    lv_obj_set_pos(icon, x, y);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(UI_INK), 0);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    return icon;
}

// ---------------- 顶栏状态栏 ----------------
// 建在 lv_layer_top() 上：悬浮于所有页面（含弹窗遮罩/休眠页）之上，
// 一处构建、全页共显；配色随休眠页深色底切换。

static lv_obj_t *s_sb_wifi;     // WiFi 图标
static lv_obj_t *s_sb_ssid;     // SSID/状态文字
static lv_obj_t *s_sb_battery;  // 电池

static bool s_sb_dark;                            // 深色底模式
static char s_sb_text[40];                        // 最近一次 WiFi 文字
static uint32_t s_sb_text_color, s_sb_icon_color; // 最近一次配色

// 深色底下的等价色：墨色系换柔灰，绿/红本就清晰则保留
static uint32_t sb_adapt_color(uint32_t color, bool dark)
{
    if (!dark) return color;
    if (color == UI_INK) return 0xC2C7CC;
    if (color == UI_INK_SOFT) return 0x9AA0A6;
    return color;
}

void ui_statusbar_build(void)
{
    if (s_sb_battery) return;   // 幂等

    lv_obj_t *layer = lv_layer_top();
    s_sb_wifi = ui_wifi_icon_create(layer, 10, 8);

    s_sb_ssid = lv_label_create(layer);
    lv_obj_set_pos(s_sb_ssid, 28, 9);
    lv_obj_set_width(s_sb_ssid, 130);
    lv_obj_set_style_text_font(s_sb_ssid, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_sb_ssid, lv_color_hex(UI_INK_SOFT), 0);
    lv_label_set_long_mode(s_sb_ssid, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_sb_ssid, "未连接");

    s_sb_battery = ui_battery_create(layer, 168, 8);

    strlcpy(s_sb_text, "未连接", sizeof(s_sb_text));
    s_sb_text_color = UI_INK_SOFT;
    s_sb_icon_color = UI_INK_SOFT;
}

void ui_statusbar_tick(void)
{
    // 电池（I2C 读取，5 秒一次足够）
    static int s_bat_div;
    if (++s_bat_div >= 5) {
        s_bat_div = 0;
        ui_battery_refresh(s_sb_battery);
    }
}

void ui_statusbar_set_wifi(const char *text, uint32_t text_color, uint32_t icon_color)
{
    if (!s_sb_ssid || !text) return;
    snprintf(s_sb_text, sizeof(s_sb_text), "%s", text);
    s_sb_text_color = text_color;
    s_sb_icon_color = icon_color;

    lv_label_set_text(s_sb_ssid, s_sb_text);
    lv_obj_set_style_text_color(s_sb_ssid,
        lv_color_hex(sb_adapt_color(s_sb_text_color, s_sb_dark)), 0);
    lv_obj_set_style_text_color(s_sb_wifi,
        lv_color_hex(sb_adapt_color(s_sb_icon_color, s_sb_dark)), 0);
}

void ui_statusbar_set_dark(bool dark)
{
    if (s_sb_dark == dark) return;
    s_sb_dark = dark;

    lv_obj_set_style_text_color(s_sb_wifi,
        lv_color_hex(sb_adapt_color(s_sb_icon_color, dark)), 0);
    lv_obj_set_style_text_color(s_sb_ssid,
        lv_color_hex(sb_adapt_color(s_sb_text_color, dark)), 0);
    ui_battery_set_dark(s_sb_battery, dark);
}
