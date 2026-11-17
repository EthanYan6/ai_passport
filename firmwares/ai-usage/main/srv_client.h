// main/srv_client.h —— 服务器状态采集（SSH 直连 exec）
#pragma once

#include <stdbool.h>
#include "usage_store.h"
#include "app_config.h"

typedef struct {
    char id[13];     // 容器短 ID（12 hex）
    char name[48];   // 超过 23 字符的 Compose 名称曾在此被硬截断，放大缓冲；
                     // 仍超长时由 UI 省略号截断
    char ports[128]; // 压缩后的端口映射；UI 对超长文本横向滚动
    uint8_t state;   // 0=running 1=其他
} srv_container_t;

typedef struct {
    bool valid;
    int err;         // 0=成功 1=未配置 2=连接失败 3=认证失败 4=采集/解析失败
    uint8_t slot;    // NVS 槽位号（结果回传用）
    char ip[USAGE_SRV_IP_MAX];
    uint16_t port;
    char user[USAGE_SRV_USER_MAX];
    float cpu_pct;                      // 0..100，支持小数
    int mem_pct;                        // 0..100
    int mem_used_mb, mem_total_mb;      // 明细
    int disk_pct;                       // 根分区 0..100
    int disk_used_gb, disk_total_gb;
    int container_count;                // 全部运行中容器数（含未显示的）
    srv_container_t containers[APP_SRV_CONTAINERS_MAX];
} srv_data_t;

// SSH 连上 srv 指定的服务器，exec 一条采集命令并解析（阻塞，总预算
// APP_SRV_TOTAL_TIMEOUT_MS）。失败时 err 置位，结构仍可渲染。
void srv_client_fetch(const usage_srv_t *srv, srv_data_t *result);
