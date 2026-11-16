// main/portal_http.h —— 配网门户（SoftAP + DNS 劫持 + HTTP 门户）
#pragma once

#include <stdbool.h>
#include "esp_err.h"

// 启动门户（DNS + HTTP 任务）。ap_name 用于页面底部提示。
esp_err_t portal_http_start(const char *ap_name);
esp_err_t portal_http_stop(void);
bool portal_http_running(void);
