// main/usage_worker.c —— 后台刷新任务实现
//
// 两个 HTTPS 客户端串行执行（ESP32-C3 无 PSRAM，同时握手两路 TLS 内存吃紧）。
// 未配置密钥时客户端直接返回 err=1，不发起请求。结果结构体按值进队列，
// 由 app 任务在 LVGL 锁内消费渲染。
#include "usage_worker.h"
#include "ark_client.h"
#include "zcode_client.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "usage_worker";

static QueueHandle_t s_result_queue;   // 深度 4，usage_result_t
static QueueHandle_t s_req_queue;      // 深度 1：忙时新请求直接被拒
static volatile bool s_busy;

static void worker_task(void *arg)
{
    (void)arg;
    uint8_t req;
    for (;;) {
        if (xQueueReceive(s_req_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        ark_data_t ark;
        ark_client_fetch(&ark);
        usage_result_t res = { 0 };
        res.kind = USAGE_RES_ARK;
        res.ark = ark;
        xQueueSend(s_result_queue, &res, 0);
        ESP_LOGI(TAG, "ark err=%d plan=%s", ark.err,
                 ark.plan[0] ? ark.plan : "-");

        zcode_data_t zcode;
        zcode_client_fetch(&zcode);
        memset(&res, 0, sizeof(res));
        res.kind = USAGE_RES_ZCODE;
        res.zcode = zcode;
        xQueueSend(s_result_queue, &res, 0);
        ESP_LOGI(TAG, "zcode err=%d level=%s", zcode.err,
                 zcode.level[0] ? zcode.level : "-");

        // 逐台 SSH 采集（串行：SSH 握手内存开销大）
        usage_srv_t servers[USAGE_SRV_MAX];
        int n = usage_store_srv_list(servers, USAGE_SRV_MAX);
        for (int i = 0; i < n; i++) {
            memset(&res, 0, sizeof(res));
            res.kind = USAGE_RES_SRV;
            srv_client_fetch(&servers[i], &res.srv);
            xQueueSend(s_result_queue, &res, 0);
            ESP_LOGI(TAG, "srv[%d] %s err=%d cpu=%.1f mem=%d dsk=%d ct=%d",
                     servers[i].idx, servers[i].ip, res.srv.err,
                     (double)res.srv.cpu_pct, res.srv.mem_pct,
                     res.srv.disk_pct, res.srv.container_count);
        }

        memset(&res, 0, sizeof(res));
        res.kind = USAGE_RES_DONE;
        xQueueSend(s_result_queue, &res, 0);

        s_busy = false;
    }
}

bool usage_worker_start(void)
{
    s_result_queue = xQueueCreate(4, sizeof(usage_result_t));
    if (!s_result_queue) return false;
    s_req_queue = xQueueCreate(1, sizeof(uint8_t));
    if (!s_req_queue) return false;
    return xTaskCreate(worker_task, "usage_work", 12288, NULL, 4, NULL) == pdPASS;
}

QueueHandle_t usage_worker_result_queue(void)
{
    return s_result_queue;
}

bool usage_worker_request(void)
{
    if (s_busy) return false;
    const uint8_t req = 1;
    if (xQueueSend(s_req_queue, &req, 0) != pdTRUE) return false;
    s_busy = true;
    return true;
}
