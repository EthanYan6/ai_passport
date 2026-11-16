// main/ui_ark.h —— 火山方舟用量页（样式取自 pc-monitor 火山页）
#pragma once

#include "lvgl.h"
#include "ark_client.h"

void ui_ark_build(lv_obj_t *parent);
// elapsed_s：数据获取至今的秒数（用于重置倒计时递减）
void ui_ark_render(const ark_data_t *d, long elapsed_s);
