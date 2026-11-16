// main/ui_provision.h —— 配网页：扫码连热点 → 手机门户配置
#pragma once

#include "lvgl.h"
#include <stdbool.h>

void ui_provision_build(lv_obj_t *parent, const char *ap_name, const char *pass);
void ui_provision_hide(void);

// 0=等待配置 1=正在连接 2=已连接 3=连接失败
void ui_provision_set_state(int state);
