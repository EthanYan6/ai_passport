// main/ui_standby.h —— 休眠页（黑底年月日 + 个人名片）
#pragma once

#include <stdbool.h>

// 显示休眠页：黑底白字，年月日 + 名片三行（NVS "card"）
void ui_standby_show(void);

// 关闭休眠页（露出下面的页面）
void ui_standby_hide(void);

bool ui_standby_visible(void);
