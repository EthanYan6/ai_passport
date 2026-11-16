// main/ui_server.h —— 服务器状态页（SSH 采集：CPU/内存/磁盘 + Docker 容器）
#pragma once

#include "lvgl.h"
#include "srv_client.h"

void ui_server_build(lv_obj_t *parent);
void ui_server_render(const srv_data_t *d, long elapsed_s);
