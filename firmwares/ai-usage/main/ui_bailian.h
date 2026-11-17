// main/ui_bailian.h —— 阿里云百炼 Coding Plan 真实用量页
#pragma once

#include "lvgl.h"
#include "bailian_client.h"

void ui_bailian_build(lv_obj_t *parent);
void ui_bailian_render(const bailian_data_t *data, long elapsed_s);
