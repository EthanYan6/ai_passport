// main/time_sync.h —— SNTP 对时与时间有效性
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// WiFi 连上后调用：启动 SNTP 请求（异步，成功后 time() 返回真实时间）
void time_sync_start(void);

// 时间是否已对上（>= 2026-01-01）
bool time_sync_valid(void);

// 便捷取值：返回当天 day（1-31）；时间无效返回 0
int time_sync_today(int *year, int *month, int *day);
