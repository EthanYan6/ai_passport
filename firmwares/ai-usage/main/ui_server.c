// main/ui_server.c —— 服务器状态页实现
//
// 布局（页容器 240x262，深色底）：
//   首行：IP（左，青色） + SSH 账号（右对齐，灰）
//   三行资源条：CPU / 内存 / 磁盘（根分区），百分比在条后，used/total 居中在条内
//   容器区：表头（容器 共N台 x/y页）+ 每页 5 行，每行两线：名称 / 端口映射
//   长按上/下翻容器页（翻页用最近一次渲染缓存的数据即时重绘）
#include "ui_server.h"
#include "ui_common.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_ip_lbl;
static lv_obj_t *s_user_lbl;
static lv_obj_t *s_bar[3];
static lv_obj_t *s_val[3];   // 条内居中的比值/状态文字
static lv_obj_t *s_pct[3];   // 条后百分比
static lv_obj_t *s_hdr;
static lv_obj_t *s_ct_name[APP_SRV_CONTAINERS_PAGE];
static lv_obj_t *s_ct_ports[APP_SRV_CONTAINERS_PAGE];

// 翻页状态：缓存最近一次渲染的数据，长按时不等刷新即可翻页
static srv_data_t s_cache;
static int s_ct_page;

static const char *const RES_NAMES[3] = {"CPU", "内存", "磁盘"};

// 容器总页数（按缓存数据算，至少 1 页；只翻已采集保留的前 MAX 台）
static int ct_pages(const srv_data_t *d)
{
    bool ok = d->valid && d->err == 0;
    int total = ok ? d->container_count : 0;
    if (total > APP_SRV_CONTAINERS_MAX) total = APP_SRV_CONTAINERS_MAX;
    int pages = (total + APP_SRV_CONTAINERS_PAGE - 1) / APP_SRV_CONTAINERS_PAGE;
    return pages < 1 ? 1 : pages;
}

void ui_server_build(lv_obj_t *p)
{
    s_ct_page = 0;

    s_ip_lbl = ui_put_label(p, "--", 10, 0, &lv_font_montserrat_14, C_CYAN);
    lv_obj_set_width(s_ip_lbl, 150);
    lv_label_set_long_mode(s_ip_lbl, LV_LABEL_LONG_DOT);

    s_user_lbl = ui_make_label(p, "--", &app_font_14, C_MUTED);
    lv_obj_align(s_user_lbl, LV_ALIGN_TOP_RIGHT, -10, 0);
    lv_obj_set_width(s_user_lbl, 60);
    lv_label_set_long_mode(s_user_lbl, LV_LABEL_LONG_DOT);

    for (int i = 0; i < 3; i++) {
        int y = 20 + i * 22;
        ui_put_label(p, RES_NAMES[i], 10, y, &app_font_14, C_MUTED);
        s_bar[i] = ui_make_bar(p, 52, y + 1, 130, 14, C_CYAN);
        s_val[i] = ui_make_label(p, "", &app_font_14, C_TEXT);
        lv_obj_set_width(s_val[i], 130);
        lv_obj_set_style_text_align(s_val[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(s_val[i], LV_LABEL_LONG_CLIP);
        lv_obj_align_to(s_val[i], s_bar[i], LV_ALIGN_CENTER, 0, 0);
        s_pct[i] = ui_make_label(p, "--", &lv_font_montserrat_14, C_TEXT);
        lv_obj_set_pos(s_pct[i], 188, y + 1);
        lv_obj_set_width(s_pct[i], 42);
        lv_obj_set_style_text_align(s_pct[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(s_pct[i], LV_LABEL_LONG_CLIP);
    }

    lv_obj_t *sep = lv_obj_create(p);
    lv_obj_set_size(sep, 216, 1);
    lv_obj_set_pos(sep, 12, 88);
    lv_obj_set_style_bg_color(sep, lv_color_hex(C_FRAME), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    s_hdr = ui_put_label(p, "容器", 10, 94, &app_font_14, C_MUTED);

    for (int i = 0; i < APP_SRV_CONTAINERS_PAGE; i++) {
        int y = 114 + i * 28;
        lv_obj_t *name = ui_put_label(p, "--", 10, y, &app_font_14, C_TEXT);
        lv_obj_set_width(name, 218);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_t *ports = ui_put_label(p, "", 10, y + 14,
                                       &lv_font_montserrat_14, C_MUTED);
        lv_obj_set_width(ports, 218);
        // 长端口映射在固定宽度内左右往复滚动，短文本保持静止。
        lv_label_set_long_mode(ports, LV_LABEL_LONG_SCROLL);
        s_ct_name[i] = name;
        s_ct_ports[i] = ports;
    }
}

static const char *err_message(int err)
{
    switch (err) {
    case 2:
        return "连接失败";
    case 3:
        return "认证失败";
    case 4:
        return "采集失败";
    default:
        return "--";
    }
}

// 容器区按当前页重绘：表头 + 每页 5 行（名称颜色表示状态）
static void render_ct(void)
{
    const srv_data_t *d = &s_cache;

    if (!(d->valid && d->err == 0)) {
        lv_label_set_text(s_hdr, "容器");
        for (int i = 0; i < APP_SRV_CONTAINERS_PAGE; i++) {
            lv_label_set_text(s_ct_name[i], "");
            lv_label_set_text(s_ct_ports[i], "");
        }
        return;
    }

    int pages = ct_pages(d);
    if (pages > 1) {
        ui_label_printf(s_hdr, "容器 共%d台 %d/%d页", d->container_count,
                        s_ct_page + 1, pages);
    } else {
        ui_label_printf(s_hdr, "容器 共%d台", d->container_count);
    }
    int first = s_ct_page * APP_SRV_CONTAINERS_PAGE;
    for (int i = 0; i < APP_SRV_CONTAINERS_PAGE; i++) {
        int idx = first + i;
        if (idx < d->container_count && idx < APP_SRV_CONTAINERS_MAX) {
            const srv_container_t *ct = &d->containers[idx];
            const char *ports = ct->ports[0] ? ct->ports : "-";
            lv_label_set_text(s_ct_name[i], ct->name[0] ? ct->name : "--");
            // 相同文本不重置滚动动画；翻页或端口变化时才更新标签。
            if (strcmp(lv_label_get_text(s_ct_ports[i]), ports) != 0) {
                lv_label_set_text(s_ct_ports[i], ports);
            }
            uint32_t c = ct->state == 0 ? C_GREEN : C_AMBER;
            lv_obj_set_style_text_color(s_ct_name[i], lv_color_hex(c), 0);
        } else {
            lv_label_set_text(s_ct_name[i], "");
            lv_label_set_text(s_ct_ports[i], "");
        }
    }
}

void ui_server_render(const srv_data_t *d, long elapsed_s)
{
    bool ok = d->valid && d->err == 0;

    s_cache = *d;

    lv_label_set_text(s_ip_lbl, d->ip[0] ? d->ip : "--");
    lv_label_set_text(s_user_lbl, d->user[0] ? d->user : "--");

    float vals[3] = { ok ? d->cpu_pct : -1.0f, ok ? (float)d->mem_pct : -1.0f,
                      ok ? (float)d->disk_pct : -1.0f };
    for (int i = 0; i < 3; i++) {
        if (vals[i] >= 0) {
            int v = ui_clamp100(vals[i]);
            lv_bar_set_value(s_bar[i], v, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_bar[i],
                                      lv_color_hex(ui_level_color(v)),
                                      LV_PART_INDICATOR);
            if (i == 0) {
                if (vals[i] < 10.0f) {
                    ui_label_printf(s_pct[i], "%.1f%%", (double)vals[i]);
                } else {
                    ui_label_printf(s_pct[i], "%.0f%%", (double)vals[i]);
                }
                lv_label_set_text(s_val[i], "");
            } else {
                ui_label_printf(s_pct[i], "%d%%", v);
                if (i == 1) {
                    ui_label_printf(s_val[i], "%d/%dM", d->mem_used_mb,
                                    d->mem_total_mb);
                } else {
                    ui_label_printf(s_val[i], "%d/%dG", d->disk_used_gb,
                                    d->disk_total_gb);
                }
            }
        } else {
            lv_bar_set_value(s_bar[i], 0, LV_ANIM_OFF);
            lv_label_set_text(s_pct[i], "--");
            lv_label_set_text(s_val[i],
                              d->valid ? err_message(d->err)
                                       : (elapsed_s < 3 ? "采集中" : "等待刷新"));
        }
        // 文本更新会改变标签尺寸，每次都以进度条为基准重新居中。
        lv_obj_align_to(s_val[i], s_bar[i], LV_ALIGN_CENTER, 0, 0);
    }

    // 数据更新后容器数可能变少，当前页越界则回拉
    int pages = ct_pages(d);
    if (s_ct_page >= pages) s_ct_page = pages - 1;
    render_ct();
}

// 长按上/下：容器列表翻页（-1=上一页 +1=下一页，循环），即时重绘
void ui_server_page_step(int dir)
{
    if (!s_hdr) return;
    int pages = ct_pages(&s_cache);
    if (pages <= 1) return;
    s_ct_page = (s_ct_page + dir + pages) % pages;
    render_ct();
}
