// main/ui_zcode.h —— zcode（智谱 GLM Coding Plan）用量页
#pragma once

#include "lvgl.h"
#include "zcode_client.h"

void ui_zcode_build(lv_obj_t *parent);
// elapsed_s：数据获取至今的秒数（用于重置倒计时递减）
void ui_zcode_render(const zcode_data_t *d, long elapsed_s);
