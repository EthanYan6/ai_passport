// main/main.c —— 极简打卡：应用编排（事件队列 + 页面切换 + 休眠）
//
// 页面模式（同一 screen 上的覆盖层，z 序 = 创建序）：
//   HOME 主页（常驻底层） / PICKER 事项选择 / CALENDAR 月历 / DETAIL 单日详情
//   PROVISION 配网 / STANDBY 休眠
// 按键回调与 WiFi 事件只入队；app 任务持 LVGL 锁消费。
// 1s lv_timer 在 LVGL 任务里刷新时间/电池并做 30s 休眠判定。
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "app_config.h"
#include "checkin_store.h"
#include "wifi_manager.h"
#include "portal_http.h"
#include "time_sync.h"
#include "ui_theme.h"
#include "ui_main.h"
#include "ui_picker.h"
#include "ui_calendar.h"
#include "ui_detail.h"
#include "ui_provision.h"
#include "ui_standby.h"

#include "lvgl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "app";

typedef enum {
    APP_MODE_HOME = 0,
    APP_MODE_PICKER,
    APP_MODE_CALENDAR,
    APP_MODE_DETAIL,
    APP_MODE_PROVISION,
    APP_MODE_STANDBY,
} app_mode_t;

typedef struct {
    uint8_t kind;      // 0=按键 1=WiFi 状态
    bsp_btn_t btn;
    bsp_btn_ev_t btn_ev;
    wifi_mgr_state_t wifi_state;
} app_event_t;

#define EVT_BUTTON 0
#define EVT_WIFI 1
#define QUEUE_DEPTH 8

static QueueHandle_t s_queue;
static volatile app_mode_t s_mode = APP_MODE_HOME;
static app_mode_t s_prev_mode = APP_MODE_HOME;   // 休眠前的页面
static bool s_picker_done;                        // PICKER 弹出时的目标状态
static volatile uint32_t s_last_activity_tick;    // ms（FreeRTOS tick）
static char s_ap_name[24];
static lv_obj_t *s_screen;

// ---------------- 事件入队（来自回调上下文） ----------------

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_queue) return;
    const app_event_t e = { .kind = EVT_BUTTON, .btn = btn, .btn_ev = ev };
    xQueueSendToFront(s_queue, &e, 0);
}

static void on_wifi_state(wifi_mgr_state_t state, void *user)
{
    (void)user;
    if (!s_queue) return;
    const app_event_t e = { .kind = EVT_WIFI, .wifi_state = state };
    xQueueSend(s_queue, &e, 0);
}

// ---------------- 配网启停 ----------------

static void enter_provision(void)
{
    if (s_mode == APP_MODE_PROVISION) return;
    wifi_manager_start_ap(s_ap_name, sizeof(s_ap_name));
    portal_http_start(s_ap_name);
    if (bsp_lvgl_lock(500)) {
        ui_provision_show(s_ap_name, APP_AP_PASS_8371);
        ui_provision_set_state(0);   // 统一显示"等待配置"；后续事件会更新
        bsp_lvgl_unlock();
    }
    s_mode = APP_MODE_PROVISION;
    s_last_activity_tick = xTaskGetTickCount();   // 配网页不休眠但仍重置计时
}

static void leave_provision(void)
{
    portal_http_stop();
    wifi_manager_stop_ap();
    if (bsp_lvgl_lock(500)) {
        ui_provision_hide();
        ui_main_reload();   // 配网页可能改过事项列表，回主页后重载记录
        bsp_lvgl_unlock();
    }
    s_mode = APP_MODE_HOME;
}

// ---------------- WiFi 状态处理 ----------------

static void handle_wifi_event(wifi_mgr_state_t state)
{
    if (!bsp_lvgl_lock(500)) return;
    ui_main_set_wifi_state(state);
    switch (state) {
    case WIFI_MGR_CONNECTED:
        time_sync_start();               // 首次连上后启动 SNTP
        if (s_mode == APP_MODE_PROVISION) {
            ui_provision_set_state(2);   // "已连接，即将返回"
            bsp_lvgl_unlock();
            // 停掉门户并返回主页（给用户 1.5s 看到状态）
            vTaskDelay(pdMS_TO_TICKS(1500));
            leave_provision();
            return;
        }
        break;
    case WIFI_MGR_CONNECTING:
        if (s_mode == APP_MODE_PROVISION) ui_provision_set_state(1);
        break;
    case WIFI_MGR_FAILED:
        if (s_mode == APP_MODE_PROVISION) ui_provision_set_state(3);
        break;
    default:
        break;
    }
    bsp_lvgl_unlock();
}

// ---------------- 按键处理 ----------------

static void handle_button(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    // 休眠中：任意键唤醒，恢复休眠前的页面
    if (s_mode == APP_MODE_STANDBY) {
        if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG) return;
        if (bsp_lvgl_lock(500)) {
            ui_standby_hide();
            bsp_lvgl_unlock();
        }
        bsp_display_backlight(100);   // 唤醒：恢复背光
        s_mode = s_prev_mode;
        return;
    }

    if (!bsp_lvgl_lock(500)) return;

    switch (s_mode) {
    case APP_MODE_HOME:
        if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG) break;
        if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK) {
            ui_picker_show(false);        // 上键：弹窗选事项，标记未完成
            s_picker_done = false;
            s_mode = APP_MODE_PICKER;
        } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
            ui_picker_show(true);         // 下键：弹窗选事项，标记完成
            s_picker_done = true;
            s_mode = APP_MODE_PICKER;
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            ui_calendar_show();           // OK：月历
            s_mode = APP_MODE_CALENDAR;
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            bsp_lvgl_unlock();
            enter_provision();            // 长按 OK：重新配网
            return;
        }
        break;

    case APP_MODE_PICKER:
        if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG) break;
        if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK) {
            ui_picker_move(-1);           // 上键：选择上移
        } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
            ui_picker_move(1);            // 下键：选择下移
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            ui_main_mark_item(ui_picker_selected(), s_picker_done);   // OK：确认打卡
            ui_picker_hide();
            s_mode = APP_MODE_HOME;
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            ui_picker_hide();             // 长按 OK：取消
            s_mode = APP_MODE_HOME;
        }
        break;

    case APP_MODE_CALENDAR:
        if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG && ev != BSP_BTN_LONG_HOLD) break;
        if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK) {
            ui_calendar_move_selection(-1);     // 上键：前一天
        } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
            ui_calendar_move_selection(1);      // 下键：后一天
        } else if (btn == BSP_BTN_UP && ev == BSP_BTN_LONG_HOLD) {
            ui_calendar_move_selection(-1);     // 长按上键不放：快速向前逐日移动
        } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_LONG_HOLD) {
            ui_calendar_move_selection(1);      // 长按下键不放：快速向后逐日移动
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            int y, m, d;                         // OK：查看选中日详情
            if (ui_calendar_selected_date(&y, &m, &d)) {
                ui_detail_show(y, m, d);
                s_mode = APP_MODE_DETAIL;
            }
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            ui_calendar_hide();                  // 长按 OK：返回主页
            s_mode = APP_MODE_HOME;
        }
        break;

    case APP_MODE_DETAIL:
        if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG) break;
        if (btn == BSP_BTN_OK) {
            ui_detail_hide();                   // OK：返回月历
            s_mode = APP_MODE_CALENDAR;
        }
        break;

    case APP_MODE_PROVISION:
        // 仅长按 OK 可退出（且必须已有可用凭据）
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            if (checkin_store_has_wifi()) {
                bsp_lvgl_unlock();
                leave_provision();
                return;
            }
        }
        break;

    default:
        break;
    }
    bsp_lvgl_unlock();
}

// ---------------- app 任务 ----------------

static void app_task(void *arg)
{
    (void)arg;
    app_event_t e;
    for (;;) {
        if (xQueueReceive(s_queue, &e, portMAX_DELAY) != pdTRUE) continue;
        s_last_activity_tick = xTaskGetTickCount();
        if (e.kind == EVT_BUTTON) handle_button(e.btn, e.btn_ev);
        else handle_wifi_event(e.wifi_state);
    }
}

// ---------------- 1s 刷新（LVGL 任务内，免锁） ----------------

static void tick_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    // 时间首次对上后：净化当月/上月位图（旧固件可能留下"有状态无记录"的
    // 日子，日历只认真实打卡记录——有记录且全部完成才算完成）
    static bool s_cleaned = false;
    if (!s_cleaned && time_sync_valid()) {
        s_cleaned = true;
        int y = 0, m = 0, d = 0;
        if (time_sync_today(&y, &m, &d)) {
            int py = y, pm = m - 1;
            if (pm < 1) { pm = 12; py--; }
            checkin_store_cleanup_month(y, m);
            checkin_store_cleanup_month(py, pm);
        }
    }

    if (s_mode != APP_MODE_STANDBY) ui_main_tick();
    ui_statusbar_tick();   // 状态栏电池（休眠时也刷新）

    // 30s 无操作 → 休眠（配网页除外）
    if (s_mode != APP_MODE_PROVISION && s_mode != APP_MODE_STANDBY) {
        uint32_t idle_ms = xTaskGetTickCount() - s_last_activity_tick;
        if (idle_ms >= pdMS_TO_TICKS(APP_IDLE_STANDBY_SECONDS * 1000)) {
            s_prev_mode = s_mode;
            ui_standby_show();
            bsp_display_backlight(15);   // 墨水屏感：休眠时背光调暗
            s_mode = APP_MODE_STANDBY;
        }
    }
}

// ---------------- 启动 ----------------

void app_main(void)
{
    ESP_LOGI(TAG, "极简打卡启动");

    ESP_ERROR_CHECK(checkin_store_init());
    ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init()) {
        ESP_LOGE(TAG, "LVGL 初始化失败");
        return;
    }
    bsp_display_backlight(100);
    bsp_battery_init();   // 失败也不阻塞：电池显示 "--"

    // WiFi 栈先起（事件要流入队列）
    ESP_ERROR_CHECK(wifi_manager_init(on_wifi_state, NULL));

    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(app_event_t));
    assert(s_queue);
    ESP_ERROR_CHECK(bsp_button_init(on_key, NULL));

    // UI 构建
    if (!bsp_lvgl_lock(1000)) return;
    s_screen = lv_screen_active();
    ui_main_build(s_screen);
    ui_statusbar_build();   // top layer 状态栏：悬浮于所有页面之上
    bsp_lvgl_unlock();

    bool has_creds = checkin_store_has_wifi();
    if (has_creds) {
        wifi_manager_connect_saved();
    } else {
        // 首次使用：直接进入配网
        enter_provision();
    }

    // 初始活动时间（配网模式本来就不休眠）
    s_last_activity_tick = xTaskGetTickCount();

    lv_timer_create(tick_timer_cb, 1000, NULL);

    if (xTaskCreate(app_task, "app", 6144, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "app 任务创建失败");
        return;
    }

    ESP_LOGI(TAG, "就绪：%s", has_creds ? "自动连接 WiFi" : "等待配网");
}
