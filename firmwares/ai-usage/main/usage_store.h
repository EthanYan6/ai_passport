// main/usage_store.h —— NVS 存取：WiFi 凭据 + 火山/智谱密钥 + 界面偏好
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define USAGE_WIFI_SSID_MAX 33
#define USAGE_WIFI_PASS_MAX 65
#define USAGE_ARK_AK_MAX 128
#define USAGE_ARK_SK_MAX 128
#define USAGE_ARK_HOST_MAX 96
#define USAGE_ZCODE_KEY_MAX 128

// NVS 初始化（nvs_flash_init + 满表擦除恢复）。启动时先调一次。
esp_err_t usage_store_init(void);

// ---------------- WiFi 凭据（命名空间 "wifi"） ----------------
esp_err_t usage_store_get_wifi(char *ssid, size_t ssid_len,
                               char *pass, size_t pass_len);
esp_err_t usage_store_set_wifi(const char *ssid, const char *pass);
bool usage_store_has_wifi(void);

// ---------------- 火山方舟密钥（命名空间 "ark"） ----------------
// host 可选：未配置时调用方用 APP_ARK_HOST_DEFAULT。
esp_err_t usage_store_get_ark(char *ak, size_t ak_len,
                              char *sk, size_t sk_len,
                              char *host, size_t host_len);
esp_err_t usage_store_set_ark(const char *ak, const char *sk, const char *host);
bool usage_store_has_ark(void);

// ---------------- 智谱 zcode API Key（命名空间 "zcode"） ----------------
esp_err_t usage_store_get_zcode_key(char *key, size_t key_len);
esp_err_t usage_store_set_zcode_key(const char *key);
bool usage_store_has_zcode(void);

// ---------------- 刷新间隔（命名空间 "ui"，秒） ----------------
uint32_t usage_store_get_refresh_s(void);
void usage_store_set_refresh_s(uint32_t seconds);

// ---------------- 服务器（SSH 监控，命名空间 "srv0".."srv3"，槽位即 idx） ----------------
// used = ip 非空；槽位固定 0..3，删除即清空槽位（页面数 = count）。
#define USAGE_SRV_MAX 4
#define USAGE_SRV_IP_MAX 64
#define USAGE_SRV_USER_MAX 32
#define USAGE_SRV_PASS_MAX 64

typedef struct {
    int idx;                        // 槽位号（get/list 时回填）
    char ip[USAGE_SRV_IP_MAX];
    char user[USAGE_SRV_USER_MAX];
    char pass[USAGE_SRV_PASS_MAX];
} usage_srv_t;

// 已配置台数（ip 非空的槽位数）
int usage_store_srv_count(void);
// 按槽位升序列出已配置的服务器，返回条数（<= max）
int usage_store_srv_list(usage_srv_t *out, int max);
// 在第一个空槽添加，成功返回槽位号；满返回 -1
int usage_store_srv_add(const char *ip, const char *user, const char *pass);
// 读指定槽位；未配置返回 false
bool usage_store_srv_get(int idx, usage_srv_t *out);
// 修改指定槽位（idx 由门户提交）
void usage_store_srv_set(int idx, const char *ip, const char *user,
                         const char *pass);
// 删除指定槽位（清空三键）
void usage_store_srv_del(int idx);
