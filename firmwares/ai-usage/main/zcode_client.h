// main/zcode_client.h —— 智谱 GLM Coding Plan（zcode）用量查询
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define ZCODE_LEVEL_MAX 16
#define ZCODE_ROWS 3   // 0=近5小时 Token 1=每周 Token 2=MCP 月度

typedef struct {
    bool valid;
    int err;                     // 0=成功 1=未配置 API Key 2=获取失败
    char level[ZCODE_LEVEL_MAX]; // 套餐等级（如 "pro"），可为空
    bool has_row[ZCODE_ROWS];
    int pct[ZCODE_ROWS];         // 0..100（服务端算好的百分比）
    long cur[ZCODE_ROWS];        // MCP 行：currentValue
    long total[ZCODE_ROWS];      // MCP 行：usage 总额
    long reset_s[ZCODE_ROWS];    // 距重置秒数（尽力解析 nextResetTime）；<0 未知
} zcode_data_t;

// 拉取用量（阻塞，一次 HTTPS，~10s 超时）。结果总是可渲染。
void zcode_client_fetch(zcode_data_t *result);
