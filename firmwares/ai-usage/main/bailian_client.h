// main/bailian_client.h —— 阿里云百炼 Coding Plan 用量查询
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define BAILIAN_QUOTA_MAX 3

typedef struct {
    bool valid;
    int used;
    int total;
    int percent;
} bailian_quota_t;

typedef struct {
    bailian_quota_t quotas[BAILIAN_QUOTA_MAX];   // API 顺序：5 小时 / 每周 / 每月
    int quota_count;
    int err;                // 0=正常 1=未配置 2=HTTP/鉴权失败 3=未对时 4=响应异常
} bailian_data_t;

// 阻塞拉取一次用量。AK/SK 仅用于生成控制台访问令牌，不写入日志。
void bailian_client_fetch(bailian_data_t *out);
