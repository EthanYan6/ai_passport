// main/main.c —— ai-usage：应用编排（事件队列 + 页面切换 + 自动刷新）
//
// 与 pc-monitor 不同，本固件自带网络栈：WiFi 连上后由 usage_worker 直接
// 拉取火山方舟与智谱 zcode 用量，结果经队列回传，app 任务持 LVGL 锁渲染。
// 模式：NORMAL（火山 / zcode / 设置 三页轮转）/ PROVISION（扫码配网覆盖页）。
// 页面按需构建（复用 pc-monitor 规避 80KB LVGL 池耗尽的手法）。
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "app_config.h"
#include "usage_store.h"
#include "wifi_manager.h"
#include "portal_http.h"
#include "time_sync.h"
#include "usage_worker.h"
#include "ui_common.h"
#include "ui_ark.h"
#include "ui_zcode.h"
#include "ui_server.h"
#include "ui_settings.h"
#include "ui_provision.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>

static const char *TAG = "ai_usage";

#define PAGE_ARK 0
#define PAGE_ZCODE 1
#define PAGE_SRV_BASE 2
// 动态页布局：火山 / zcode / 服务器×N / 设置
#define PAGE_SLOTS_MAX (PAGE_SRV_BASE + USAGE_SRV_MAX + 1)

typedef enum {
    MODE_NORMAL = 0,
    MODE_PROVISION,
} app_mode_t;

typedef struct {
    uint8_t kind;      // 0=按键 1=WiFi 状态
    bsp_btn_t btn;
    bsp_btn_ev_t ev;
    wifi_mgr_state_t wifi;
} app_event_t;

#define EVT_BUTTON 0
#define EVT_WIFI 1
#define QUEUE_DEPTH 8

#define BATT_SAMPLE_MS 5000
#define RENDER_TICK_MS 1000

static QueueHandle_t s_queue;
static volatile app_mode_t s_mode = MODE_NORMAL;
static int s_view = PAGE_ARK;
static bool s_paused;
static bool s_fetch_pending;
static int64_t s_last_fetch_ms;    // 最近一次结果到达时刻（自动刷新节拍）
static bool s_ark_ok;
static bool s_zcode_ok;
static bool s_ever_fetched;
static int64_t s_leave_at_ms;      // 配网成功后延迟返回 NORMAL 的时刻，0=无
static int s_batt_pct = -1;
static int64_t s_last_batt_ms;
static char s_ip_str[16];
static uint32_t s_refresh_s = APP_REFRESH_DEFAULT_S;

static lv_obj_t *s_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_dot;
static lv_obj_t *s_link_lbl;
static lv_obj_t *s_batt_lbl;
static lv_obj_t *s_hint;
static lv_obj_t *s_page[PAGE_SLOTS_MAX];
static lv_obj_t *s_prov_page;
static int s_built_view = -1;

static ark_data_t s_ark;
static zcode_data_t s_zcode;
static srv_data_t s_srv[USAGE_SRV_MAX];
static usage_srv_t s_srv_order[USAGE_SRV_MAX];   // 位置 -> 槽位映射（每 tick 刷新）
static int s_srv_count;

// ---------------- 事件入队（回调上下文只允许入队） ----------------

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_queue) return;
    const app_event_t e = { .kind = EVT_BUTTON, .btn = btn, .ev = ev };
    xQueueSendToFront(s_queue, &e, 0);
}

static void on_wifi_state(wifi_mgr_state_t state, void *user)
{
    (void)user;
    if (!s_queue) return;
    const app_event_t e = { .kind = EVT_WIFI, .wifi = state };
    xQueueSend(s_queue, &e, 0);
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;
    ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
    snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&evt->ip_info.ip));
}

// ---------------- 刷新触发 ----------------
// 动态页布局：0=火山 1=zcode 2..2+N-1=服务器 2+N=设置（见 page_count/page_settings）

static int page_count(void);
static int page_settings(void);

static void request_refresh(bool force)
{
    if (!force && s_fetch_pending) return;
    s_fetch_pending = usage_worker_request();
}

static void cycle_refresh_preset(void)
{
    int cur = 0;
    for (int i = 0; i < (int)APP_REFRESH_PRESETS_NUM; i++) {
        if (APP_REFRESH_PRESETS_S[i] == s_refresh_s) cur = i;
    }
    s_refresh_s = APP_REFRESH_PRESETS_S[(cur + 1) % APP_REFRESH_PRESETS_NUM];
    usage_store_set_refresh_s(s_refresh_s);
    ESP_LOGI(TAG, "刷新间隔 -> %lus", (unsigned long)s_refresh_s);
}

// ---------------- 配网启停 ----------------

static void enter_provision(void)
{
    if (s_mode == MODE_PROVISION) return;
    char ap_name[24];
    wifi_manager_start_ap(ap_name, sizeof(ap_name));
    portal_http_start(ap_name);
    if (bsp_lvgl_lock(500)) {
        if (s_built_view >= 0 && s_page[s_built_view]) {
            lv_obj_delete(s_page[s_built_view]);
            s_page[s_built_view] = NULL;
            s_built_view = -1;
        }
        s_prov_page = ui_make_container(s_scr, 0, 34, 240, 262);
        ui_provision_build(s_prov_page, ap_name, APP_AP_PASS_8371);
        ui_provision_set_state(0);
        bsp_lvgl_unlock();
    }
    s_mode = MODE_PROVISION;
    s_leave_at_ms = 0;
}

static void leave_provision(void)
{
    portal_http_stop();
    wifi_manager_stop_ap();
    if (bsp_lvgl_lock(500)) {
        ui_provision_hide();
        if (s_prov_page) {
            lv_obj_delete(s_prov_page);
            s_prov_page = NULL;
        }
        bsp_lvgl_unlock();
    }
    s_mode = MODE_NORMAL;
    s_built_view = -1;
    s_leave_at_ms = 0;
    s_last_fetch_ms = 0;   // 立即触发一次刷新
}

// ---------------- WiFi 事件 ----------------

static void handle_wifi_event(wifi_mgr_state_t state)
{
    if (state == WIFI_MGR_CONNECTED) {
        time_sync_start();
        if (s_mode == MODE_NORMAL) {
            s_last_fetch_ms = 0;   // 连上立即拉一轮
        }
    }

    if (s_mode != MODE_PROVISION) return;

    if (!bsp_lvgl_lock(500)) return;
    switch (state) {
    case WIFI_MGR_CONNECTED:
        ui_provision_set_state(2);
        s_leave_at_ms = esp_timer_get_time() / 1000 + 1500;
        break;
    case WIFI_MGR_STA_ASSOC:
        // 未拿到 IP 时保持门户，避免填写过程中 AP 自动关闭；可长按 OK 手动退出
        ui_provision_set_state(1);
        break;
    case WIFI_MGR_CONNECTING:
        ui_provision_set_state(1);
        break;
    case WIFI_MGR_FAILED:
        ui_provision_set_state(3);
        break;
    default:
        break;
    }
    bsp_lvgl_unlock();
}

// ---------------- 结果事件 ----------------

static void handle_result(const usage_result_t *res)
{
    int64_t now = esp_timer_get_time() / 1000;
    if (res->kind == USAGE_RES_ARK) {
        s_ark = res->ark;
        s_ark_ok = s_ark.err == 1 || (s_ark.valid && s_ark.err == 0);
        s_ever_fetched = true;
        s_last_fetch_ms = now;
        ESP_LOGI(TAG, "火山 err=%d", s_ark.err);
    } else if (res->kind == USAGE_RES_ZCODE) {
        s_zcode = res->zcode;
        s_zcode_ok = s_zcode.err == 1 || (s_zcode.valid && s_zcode.err == 0);
        s_ever_fetched = true;
        s_last_fetch_ms = now;
        ESP_LOGI(TAG, "zcode err=%d", s_zcode.err);
    } else if (res->kind == USAGE_RES_SRV) {
        if (res->srv.slot < USAGE_SRV_MAX) {
            s_srv[res->srv.slot] = res->srv;
        }
        s_ever_fetched = true;
        s_last_fetch_ms = now;
    } else {   // USAGE_RES_DONE：一轮结束
        s_fetch_pending = false;
    }
}

// ---------------- 按键 ----------------

static void handle_button(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (s_mode == MODE_PROVISION) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            if (usage_store_has_wifi()) {
                leave_provision();
            }
        }
        return;
    }

    if (!bsp_lvgl_lock(500)) return;

    if (ev == BSP_BTN_CLICK) {
        if (btn == BSP_BTN_UP) {
            s_view = (s_view + page_count() - 1) % page_count();
        } else if (btn == BSP_BTN_DOWN) {
            s_view = (s_view + 1) % page_count();
        } else if (btn == BSP_BTN_OK) {
            if (s_view >= page_settings()) {
                cycle_refresh_preset();
            } else {
                request_refresh(true);
            }
        }
    } else if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            // 服务器页：长按上/下翻容器列表（页面已构建才碰 LVGL 对象）
            if (s_view >= PAGE_SRV_BASE && s_view < page_settings() &&
                s_built_view == s_view) {
                ui_server_page_step(btn == BSP_BTN_UP ? -1 : 1);
            }
        } else if (btn == BSP_BTN_OK) {
            if (s_view >= page_settings()) {
                bsp_lvgl_unlock();
                enter_provision();
                return;
            }
            s_paused = !s_paused;
            ESP_LOGI(TAG, "auto refresh %s", s_paused ? "paused" : "resumed");
        }
    }
    bsp_lvgl_unlock();
}

// ---------------- 页面按需构建 ----------------
// 视图布局：0=火山 1=zcode 2..2+N-1=服务器 2+N=设置

static int page_count(void)
{
    return PAGE_SRV_BASE + s_srv_count + 1;
}

static int page_settings(void)
{
    return PAGE_SRV_BASE + s_srv_count;
}

static void page_show(int view)
{
    if (s_built_view == view) return;
    if (s_built_view >= 0 && s_page[s_built_view]) {
        lv_obj_delete(s_page[s_built_view]);
        s_page[s_built_view] = NULL;
    }
    s_page[view] = ui_make_container(s_scr, 0, 34, 240, 262);
    switch (view) {
    case PAGE_ARK:
        ui_ark_build(s_page[view]);
        break;
    case PAGE_ZCODE:
        ui_zcode_build(s_page[view]);
        break;
    default:
        if (view < page_settings()) {
            ui_server_build(s_page[view]);
        } else {
            ui_settings_build(s_page[view]);
        }
        break;
    }
    s_built_view = view;
}

// ---------------- 渲染 ----------------

static void render(void)
{
    if (!s_scr) return;

    bool provision = s_mode == MODE_PROVISION;
    if (!provision) {
        page_show(s_view);
    }

    // 位置->槽位映射每 tick 刷新（门户可能增删服务器，页面数随之变化）
    s_srv_count = usage_store_srv_list(s_srv_order, USAGE_SRV_MAX);
    if (!provision && s_view >= page_count()) {
        s_view = page_count() - 1;
    }

    // 页名：火山 / zcode / 服务器N / 设置 / 配网
    static char srv_title[24];
    if (provision) {
        lv_label_set_text(s_title, "配网");
    } else if (s_view == PAGE_ARK) {
        lv_label_set_text(s_title, "火山");
    } else if (s_view == PAGE_ZCODE) {
        lv_label_set_text(s_title, "zcode");
    } else if (s_view < page_settings()) {
        snprintf(srv_title, sizeof(srv_title), "服务器%d",
                 s_view - PAGE_SRV_BASE + 1);
        lv_label_set_text(s_title, srv_title);
    } else {
        lv_label_set_text(s_title, "设置");
    }

    // 状态点：AP=配网中；NO WIFI=未连接；NO KEY=两家均未配置；
    // WIFI=已配置接口全部成功；FAIL=已配置接口失败；WAIT=等待首轮结果
    bool connected = wifi_manager_state() == WIFI_MGR_CONNECTED;
    uint32_t dot_color;
    const char *link;
    if (provision) {
        dot_color = C_AMBER;
        link = "AP";
    } else if (!connected) {
        dot_color = C_RED;
        link = "NO WIFI";
    } else if (s_ever_fetched && s_ark.err == 1 && s_zcode.err == 1) {
        dot_color = C_AMBER;
        link = "NO KEY";
    } else if (s_ever_fetched && s_ark_ok && s_zcode_ok) {
        dot_color = C_GREEN;
        link = "WIFI";
    } else if (s_ever_fetched) {
        dot_color = C_AMBER;
        link = "FAIL";
    } else {
        dot_color = C_AMBER;
        link = "WAIT";
    }
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(dot_color), 0);
    lv_label_set_text(s_link_lbl, link);
    lv_obj_set_style_text_color(s_link_lbl, lv_color_hex(dot_color), 0);

    if (s_batt_pct < 0) {
        lv_label_set_text(s_batt_lbl, "--");
    } else {
        uint32_t c = s_batt_pct > 50 ? C_GREEN : (s_batt_pct > 20 ? C_AMBER : C_RED);
        ui_label_printf(s_batt_lbl, "%d%%", s_batt_pct);
        lv_obj_set_style_text_color(s_batt_lbl, lv_color_hex(c), 0);
    }

    if (provision) {
        lv_label_set_text(s_hint, "HOLD OK:exit (after saved)");
    } else if (s_view >= page_settings()) {
        lv_label_set_text(s_hint, "OK:time  HOLD:provision");
    } else if (s_paused) {
        lv_label_set_text(s_hint, "PAUSED  OK:refr  HOLD:resume");
    } else if (s_view >= PAGE_SRV_BASE && s_view < page_settings()) {
        lv_label_set_text(s_hint, "HOLD UP/DN:ct page  OK:refr");
    } else {
        lv_label_set_text(s_hint, "UP/DN page  OK:refr  HOLD:pause");
    }

    if (provision) return;

    long elapsed_s = s_ever_fetched
                         ? (long)((esp_timer_get_time() / 1000 - s_last_fetch_ms) / 1000)
                         : 0;
    if (s_view == PAGE_ARK) {
        ui_ark_render(&s_ark, elapsed_s);
    } else if (s_view == PAGE_ZCODE) {
        ui_zcode_render(&s_zcode, elapsed_s);
    } else if (s_view < page_settings()) {
        int slot = s_srv_order[s_view - PAGE_SRV_BASE].idx;
        ui_server_render(&s_srv[slot], elapsed_s);
    } else {
        ui_settings_info_t info = { 0 };
        info.refresh_s = s_refresh_s;
        info.wifi_ok = connected;
        wifi_manager_get_ssid(info.ssid, sizeof(info.ssid));
        if (connected) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                info.rssi = ap.rssi;
            }
        }
        info.ip = s_ip_str;
        info.time_ok = time_sync_valid();
        if (info.time_ok) {
            static char clock_str[9];
            time_t now = time(NULL);
            struct tm lt;
            localtime_r(&now, &lt);
            strftime(clock_str, sizeof(clock_str), "%H:%M:%S", &lt);
            info.clock = clock_str;
        } else {
            info.clock = "--:--:--";
        }
        info.batt_pct = s_batt_pct;
        info.srv_count = s_srv_count;
        ui_settings_render(&info);
    }
}

// ---------------- app 任务 ----------------

static void app_task(void *arg)
{
    (void)arg;
    int64_t last_tick = 0;
    QueueHandle_t result_queue = usage_worker_result_queue();

    for (;;) {
        int64_t now = esp_timer_get_time() / 1000;

        app_event_t e;
        if (xQueueReceive(s_queue, &e, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (e.kind == EVT_BUTTON) {
                handle_button(e.btn, e.ev);
            } else {
                handle_wifi_event(e.wifi);
            }
        }
        usage_result_t res;
        while (xQueueReceive(result_queue, &res, 0) == pdTRUE) {
            handle_result(&res);
        }

        // 配网成功提示后延迟返回 NORMAL
        if (s_mode == MODE_PROVISION && s_leave_at_ms != 0 &&
            now >= s_leave_at_ms) {
            leave_provision();
        }

        // 自动刷新：WiFi 连上、未暂停、到达间隔
        if (s_mode == MODE_NORMAL &&
            wifi_manager_state() == WIFI_MGR_CONNECTED && !s_paused &&
            !s_fetch_pending &&
            now - s_last_fetch_ms >= (int64_t)s_refresh_s * 1000) {
            s_refresh_s = usage_store_get_refresh_s();   // 门户可能改过
            request_refresh(false);
        }

        if (now - s_last_batt_ms >= BATT_SAMPLE_MS) {
            s_batt_pct = bsp_battery_soc();
            s_last_batt_ms = now;
        }

        if (now - last_tick >= RENDER_TICK_MS) {
            last_tick = now;
            if (bsp_lvgl_lock(200)) {
                render();
                bsp_lvgl_unlock();
            }
        }
    }
}

// ---------------- 启动 ----------------

void app_main(void)
{
    ESP_LOGI(TAG, "ai-usage 启动");

    ESP_ERROR_CHECK(usage_store_init());
    s_refresh_s = usage_store_get_refresh_s();

    ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init()) {
        ESP_LOGE(TAG, "LVGL 初始化失败");
        return;
    }
    bsp_display_backlight(100);
    bsp_battery_init();

    // WiFi 栈先起（事件要流入队列）
    ESP_ERROR_CHECK(wifi_manager_init(on_wifi_state, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               on_got_ip, NULL));

    memset(&s_ark, 0, sizeof(s_ark));
    memset(&s_zcode, 0, sizeof(s_zcode));
    memset(s_srv, 0, sizeof(s_srv));
    for (int i = 0; i < ARK_WINDOWS; i++) s_ark.reset_s[i] = -1;
    for (int i = 0; i < ZCODE_ROWS; i++) s_zcode.reset_s[i] = -1;
    s_srv_count = usage_store_srv_list(s_srv_order, USAGE_SRV_MAX);

    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(app_event_t));
    assert(s_queue);
    ESP_ERROR_CHECK(bsp_button_init(on_key, NULL));

    if (!bsp_lvgl_lock(1000)) return;
    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);

    lv_obj_t *frame = lv_obj_create(s_scr);
    lv_obj_set_size(frame, 232, 312);
    lv_obj_set_pos(frame, 4, 4);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(C_FRAME), 0);
    lv_obj_set_style_radius(frame, 6, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_CLICKABLE);

    s_title = ui_put_label(s_scr, "火山", 12, 10, &app_font_14, C_CYAN);

    s_dot = lv_obj_create(s_scr);
    lv_obj_set_size(s_dot, 8, 8);
    lv_obj_set_pos(s_dot, 100, 14);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(C_RED), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);

    s_link_lbl = ui_put_label(s_scr, "NO WIFI", 112, 10, &lv_font_montserrat_14,
                              C_RED);

    s_batt_lbl = ui_make_label(s_scr, "--", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(s_batt_lbl, LV_ALIGN_TOP_RIGHT, -10, 10);

    lv_obj_t *sep = lv_obj_create(s_scr);
    lv_obj_set_size(sep, 216, 1);
    lv_obj_set_pos(sep, 12, 30);
    lv_obj_set_style_bg_color(sep, lv_color_hex(C_FRAME), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    s_hint = ui_put_label(s_scr, "UP/DN page  OK:refr  HOLD:pause", 12, 298,
                          &lv_font_montserrat_14, C_MUTED);

    lv_screen_load(s_scr);
    bsp_lvgl_unlock();

    if (!usage_worker_start()) {
        ESP_LOGE(TAG, "usage worker 启动失败");
    }

    if (usage_store_has_wifi()) {
        wifi_manager_connect_saved();
    } else {
        enter_provision();
    }

    if (xTaskCreate(app_task, "ai_usage", 6144, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "app 任务创建失败");
        return;
    }

    ESP_LOGI(TAG, "就绪");
}
