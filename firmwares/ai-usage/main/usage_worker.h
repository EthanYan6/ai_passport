// main/usage_worker.h —— 后台刷新任务：串行 HTTPS，结果经队列回传 UI
#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "ark_client.h"
#include "zcode_client.h"
#include "srv_client.h"

typedef enum {
    USAGE_RES_ARK = 0,    // 火山结果（先回）
    USAGE_RES_ZCODE,      // zcode 结果
    USAGE_RES_SRV,        // 每台服务器一条（res.srv.slot 为槽位号）
    USAGE_RES_DONE,       // 一轮全部结束（app 据此清 pending）
} usage_res_kind_t;

typedef struct {
    usage_res_kind_t kind;
    union {               // 按类型只用其一；联合体让结果队列省 ~2/3 内存
        ark_data_t ark;
        zcode_data_t zcode;
        srv_data_t srv;
    };
} usage_result_t;

// 启动 worker 任务（内部自建结果队列，深度 4）。
bool usage_worker_start(void);

// 结果队列句柄（usage_result_t）。app 任务以 0 超时轮询消费。
QueueHandle_t usage_worker_result_queue(void);

// 请求一次完整刷新（火山 + zcode）。忙时返回 false，本次调用被忽略。
bool usage_worker_request(void);
