// main/ark_client.h —— 火山方舟个人版套餐用量查询（设备直连，V4 签名）
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define ARK_PLAN_MAX 20
#define ARK_WINDOWS 3   // 0=近5小时(session) 1=近一周(weekly) 2=近一月(monthly)

typedef struct {
    bool valid;
    int err;                    // 0=成功 1=未配置密钥 2=获取失败 3=时间未同步
    char plan[ARK_PLAN_MAX];    // PlanType（如 "pro"），可为空
    bool has_win[ARK_WINDOWS];
    float quota[ARK_WINDOWS];   // Coding: Cap / AFP: Quota
    float used[ARK_WINDOWS];    // Coding: Percent / AFP: Used
    long reset_s[ARK_WINDOWS];  // 距重置秒数；<0 未知
} ark_data_t;

// 拉取用量（阻塞，含两次 HTTPS，最多 ~20s）。结果总是可渲染：
// 未配置/失败/未对时分别落到 err=1/2/3。
void ark_client_fetch(ark_data_t *result);
