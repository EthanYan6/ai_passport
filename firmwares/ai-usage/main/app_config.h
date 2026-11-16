// main/app_config.h —— ai-usage 全局配置与默认值
#pragma once

#include <stdint.h>

// ---------------- 配网热点 ----------------
#define APP_AP_PASS_8371 "12345678"   // 热点密码：8 位（WPA2 要求 >=8）
#define APP_AP_CHANNEL 6

// ---------------- 对时（火山 V4 签名的 X-Date 依赖真实时间） ----------------
#define APP_SNTP_HOST_1 "ntp.aliyun.com"
#define APP_SNTP_HOST_2 "pool.ntp.org"
// 有效时间下限：2026-01-01 UTC+8 之前的时间视为无效（还没对上）
#define APP_MIN_VALID_TIME 1767196800
#define APP_TZ "CST-8"

// ---------------- 火山方舟（Coding Plan / Agent Plan 用量查询） ----------------
// 与 pc-monitor/tools/pc_status_bridge.py 的 ArkUsageSampler 相同的 V4 签名接口。
// host/region/service/version 可在配网门户覆盖（留空用默认）。
#define APP_ARK_HOST_DEFAULT "ark.cn-beijing.volcengineapi.com"
#define APP_ARK_REGION_DEFAULT "cn-beijing"
#define APP_ARK_SERVICE_DEFAULT "ark"
#define APP_ARK_ACTION_PRIMARY "GetCodingPlanUsage"   // Coding Plan 个人版
#define APP_ARK_ACTION_FALLBACK "GetAFPUsage"         // Agent Plan 个人版
#define APP_ARK_API_VERSION "2024-01-01"
#define APP_ARK_HTTP_TIMEOUT_MS 10000

// ---------------- zcode 用量（智谱 GLM Coding Plan，国内版） ----------------
#define APP_ZCODE_URL \
    "https://open.bigmodel.cn/api/monitor/usage/quota/limit"
// 认证头直接放 API Key 本体（无 Bearer 前缀），与官方/cc-switch 脚本一致。
#define APP_ZCODE_HTTP_TIMEOUT_MS 10000

// ---------------- 服务器 SSH 监控 ----------------
// 一条 exec 命令采集全部数据（两次 /proc/stat 采样算 CPU，procfs 内存，
// df 根分区，docker ps 容器列表）。S1/S2/MEM/DSK 为段落前缀，C| 为容器行。
#define APP_SRV_CMD \
    "awk 'NR==1{printf \"S1 %d %d %d\\n\",$2+$4,$5,$2+$4+$5}' /proc/stat; " \
    "sleep 1; " \
    "awk 'NR==1{printf \"S2 %d %d %d\\n\",$2+$4,$5,$2+$4+$5}' /proc/stat; " \
    "awk '/MemTotal/{t=$2}/MemAvailable/{a=$2}END{printf \"MEM %d %d %d\\n\",(t-a)*100/t,t-a,t}' /proc/meminfo; " \
    "df -kP / | awk 'NR==2{printf \"DSK %d %d %d\\n\",$3*100/$2,$3,$2}'; " \
    "docker ps --format 'C|{{.ID}}|{{.Names}}|{{.State}}|{{.Ports}}' 2>/dev/null || echo NODOCKER"
#define APP_SRV_SSH_PORT 22
#define APP_SRV_CONNECT_TIMEOUT_MS 5000   // TCP 连接超时
#define APP_SRV_TOTAL_TIMEOUT_MS 15000    // 握手+采集总预算
#define APP_SRV_RESP_MAX 16384            // 输出累积上限
#define APP_SRV_CONTAINERS_MAX 5          // 屏上最多显示的容器行

// ---------------- 刷新间隔（秒） ----------------
// 设置页 OK 循环切换，门户下拉选择；存 NVS。
#define APP_REFRESH_DEFAULT_S 60
static const uint32_t APP_REFRESH_PRESETS_S[] = { 30, 60, 120, 300, 600, 1800 };
#define APP_REFRESH_PRESETS_NUM \
    (sizeof(APP_REFRESH_PRESETS_S) / sizeof(APP_REFRESH_PRESETS_S[0]))
