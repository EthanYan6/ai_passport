// main/ui_server.c —— 服务器状态页实现
//
// 布局（页容器 240x262，深色底）：
//   首行：IP（左，青色） + SSH 账号（右对齐，灰）
//   三行资源条：CPU / 内存 / 磁盘（根分区），右端百分比，条下明细
//   容器区：表头（容器 共N台）+ 最多 5 行，每行两线：短ID+名称 / 端口映射
#include "ui_server.h"
#include "ui_common.h"

#include <stdio.h>

static lv_obj_t *s_ip_lbl;
static lv_obj_t *s_user_lbl;
static lv_obj_t *s_bar[3];
static lv_obj_t *s_pct[3];
static lv_obj_t *s_detail[3];
static lv_obj_t *s_hdr;
static lv_obj_t *s_ct_id[APP_SRV_CONTAINERS_MAX];
static lv_obj_t *s_ct_name[APP_SRV_CONTAINERS_MAX];
static lv_obj_t *s_ct_ports[APP_SRV_CONTAINERS_MAX];

static const char *const RES_NAMES[3] = {"CPU", "内存", "磁盘"};

void ui_server_build(lv_obj_t *p)
{
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
        s_bar[i] = ui_make_bar(p, 52, y + 3, 130, 10, C_CYAN);
        s_pct[i] = ui_put_label(p, "--", 190, y, &lv_font_montserrat_14, C_TEXT);
        s_detail[i] = ui_make_label(p, "", &app_font_14, C_MUTED);
        lv_obj_align(s_detail[i], LV_ALIGN_TOP_RIGHT, -10, y);
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

    for (int i = 0; i < APP_SRV_CONTAINERS_MAX; i++) {
        int y = 114 + i * 28;
        lv_obj_t *id = ui_put_label(p, "--", 10, y, &lv_font_montserrat_14, C_CYAN);
        lv_obj_t *name = ui_make_label(p, "", &app_font_14, C_TEXT);
        lv_obj_set_pos(name, 76, y);
        lv_obj_set_width(name, 152);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_t *ports = ui_put_label(p, "", 10, y + 14, &lv_font_montserrat_14,
                                       C_MUTED);
        lv_obj_set_width(ports, 218);
        lv_label_set_long_mode(ports, LV_LABEL_LONG_DOT);
        s_ct_id[i] = id;
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

void ui_server_render(const srv_data_t *d, long elapsed_s)
{
    bool ok = d->valid && d->err == 0;

    lv_label_set_text(s_ip_lbl, d->ip[0] ? d->ip : "--");
    lv_label_set_text(s_user_lbl, d->user[0] ? d->user : "--");

    int vals[3] = { ok ? d->cpu_pct : -1, ok ? d->mem_pct : -1,
                    ok ? d->disk_pct : -1 };
    for (int i = 0; i < 3; i++) {
        if (vals[i] >= 0) {
            int v = ui_clamp100((float)vals[i]);
            lv_bar_set_value(s_bar[i], v, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_bar[i],
                                      lv_color_hex(ui_level_color(v)),
                                      LV_PART_INDICATOR);
            ui_label_printf(s_pct[i], "%d", v);
            if (!ok) continue;
            if (i == 1) {
                ui_label_printf(s_detail[i], "%d / %dM", d->mem_used_mb,
                                d->mem_total_mb);
            } else if (i == 2) {
                ui_label_printf(s_detail[i], "%d / %dG", d->disk_used_gb,
                                d->disk_total_gb);
            }
        } else {
            lv_bar_set_value(s_bar[i], 0, LV_ANIM_OFF);
            lv_label_set_text(s_pct[i], "--");
            lv_label_set_text(s_detail[i],
                              d->valid ? err_message(d->err)
                                       : (elapsed_s < 3 ? "采集中" : "等待刷新"));
        }
    }

    if (ok) {
        ui_label_printf(s_hdr, "容器 共%d台", d->container_count);
        for (int i = 0; i < APP_SRV_CONTAINERS_MAX; i++) {
            bool has = i < d->container_count;
            if (has) {
                const srv_container_t *ct = &d->containers[i];
                lv_label_set_text(s_ct_id[i],
                                  ct->id[0] ? ct->id : "--");
                lv_label_set_text(s_ct_name[i],
                                  ct->name[0] ? ct->name : "--");
                lv_label_set_text(s_ct_ports[i],
                                  ct->ports[0] ? ct->ports : "-");
                uint32_t c = ct->state == 0 ? C_GREEN : C_AMBER;
                lv_obj_set_style_text_color(s_ct_id[i], lv_color_hex(c), 0);
            } else {
                lv_label_set_text(s_ct_id[i], "");
                lv_label_set_text(s_ct_name[i], "");
                lv_label_set_text(s_ct_ports[i], "");
            }
        }
    } else {
        lv_label_set_text(s_hdr, "容器");
        for (int i = 0; i < APP_SRV_CONTAINERS_MAX; i++) {
            lv_label_set_text(s_ct_id[i], "");
            lv_label_set_text(s_ct_name[i], "");
            lv_label_set_text(s_ct_ports[i], "");
        }
    }
}
