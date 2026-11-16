// main/usage_store.c —— NVS 存取实现
//
// 四个命名空间：
//   wifi  ssid/pass      配网门户写入，wifi_manager 读取
//   ark   ak/sk/host     火山方舟 V4 签名密钥（host 可选）
//   zcode apikey         智谱 GLM Coding Plan 用量查询密钥
//   ui    refresh        自动刷新间隔（秒，uint32）
#include "usage_store.h"
#include "app_config.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

esp_err_t usage_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

// ---------------- 通用小工具 ----------------

static esp_err_t get_str(const char *ns, const char *key, char *out, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t sz = len;
    err = nvs_get_str(h, key, out, &sz);
    nvs_close(h);
    if (err != ESP_OK) out[0] = '\0';
    return err;
}

static esp_err_t set_str(const char *ns, const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, value ? value : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static bool has_str(const char *ns, const char *key)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    bool found = nvs_find_key(h, key, NULL) == ESP_OK;
    nvs_close(h);
    return found;
}

// ---------------- WiFi ----------------

esp_err_t usage_store_get_wifi(char *ssid, size_t ssid_len,
                               char *pass, size_t pass_len)
{
    esp_err_t err = get_str("wifi", "ssid", ssid, ssid_len);
    if (err != ESP_OK) return err;
    return get_str("wifi", "pass", pass, pass_len);
}

esp_err_t usage_store_set_wifi(const char *ssid, const char *pass)
{
    esp_err_t err = set_str("wifi", "ssid", ssid);
    if (err == ESP_OK) err = set_str("wifi", "pass", pass ? pass : "");
    return err;
}

bool usage_store_has_wifi(void)
{
    return has_str("wifi", "ssid");
}

// ---------------- 火山方舟 ----------------

esp_err_t usage_store_get_ark(char *ak, size_t ak_len,
                              char *sk, size_t sk_len,
                              char *host, size_t host_len)
{
    esp_err_t err = get_str("ark", "ak", ak, ak_len);
    if (err != ESP_OK) return err;
    err = get_str("ark", "sk", sk, sk_len);
    if (err != ESP_OK) return err;
    if (host && host_len) {
        if (get_str("ark", "host", host, host_len) != ESP_OK) host[0] = '\0';
    }
    return ESP_OK;
}

esp_err_t usage_store_set_ark(const char *ak, const char *sk, const char *host)
{
    esp_err_t err = set_str("ark", "ak", ak);
    if (err == ESP_OK) err = set_str("ark", "sk", sk ? sk : "");
    if (err == ESP_OK && host) err = set_str("ark", "host", host);
    return err;
}

bool usage_store_has_ark(void)
{
    return has_str("ark", "ak");
}

// ---------------- 智谱 zcode ----------------

esp_err_t usage_store_get_zcode_key(char *key, size_t key_len)
{
    return get_str("zcode", "apikey", key, key_len);
}

esp_err_t usage_store_set_zcode_key(const char *key)
{
    return set_str("zcode", "apikey", key ? key : "");
}

bool usage_store_has_zcode(void)
{
    return has_str("zcode", "apikey");
}

// ---------------- 刷新间隔 ----------------

uint32_t usage_store_get_refresh_s(void)
{
    nvs_handle_t h;
    uint32_t v = 0;
    if (nvs_open("ui", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "refresh", &v);
        nvs_close(h);
    }
    if (v == 0) v = APP_REFRESH_DEFAULT_S;
    return v;
}

void usage_store_set_refresh_s(uint32_t seconds)
{
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_u32(h, "refresh", seconds) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

// ---------------- 服务器（SSH 监控） ----------------
//
// 每台服务器一个命名空间 "srvN"（N=槽位 0..3），键 ip/user/pass。
// 槽位固定：删除只清键不复位编号，页面映射 = 有内容的槽位。

static esp_err_t srv_get(int idx, usage_srv_t *out)
{
    char ns[8];
    snprintf(ns, sizeof(ns), "srv%d", idx);
    if (get_str(ns, "ip", out->ip, sizeof(out->ip)) != ESP_OK) {
        out->ip[0] = '\0';
    }
    get_str(ns, "user", out->user, sizeof(out->user));
    get_str(ns, "pass", out->pass, sizeof(out->pass));
    out->idx = idx;
    return out->ip[0] ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void srv_set(int idx, const char *ip, const char *user, const char *pass)
{
    char ns[8];
    snprintf(ns, sizeof(ns), "srv%d", idx);
    set_str(ns, "ip", ip ? ip : "");
    set_str(ns, "user", user ? user : "");
    set_str(ns, "pass", pass ? pass : "");
}

int usage_store_srv_count(void)
{
    usage_srv_t tmp;
    int n = 0;
    for (int i = 0; i < USAGE_SRV_MAX; i++) {
        if (srv_get(i, &tmp) == ESP_OK) n++;
    }
    return n;
}

int usage_store_srv_list(usage_srv_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < USAGE_SRV_MAX && n < max; i++) {
        if (srv_get(i, &out[n]) == ESP_OK) n++;
    }
    return n;
}

int usage_store_srv_add(const char *ip, const char *user, const char *pass)
{
    if (!ip || !ip[0]) return -1;
    for (int i = 0; i < USAGE_SRV_MAX; i++) {
        usage_srv_t tmp;
        if (srv_get(i, &tmp) != ESP_OK) {
            srv_set(i, ip, user, pass);
            return i;
        }
    }
    return -1;
}

bool usage_store_srv_get(int idx, usage_srv_t *out)
{
    if (idx < 0 || idx >= USAGE_SRV_MAX) return false;
    return srv_get(idx, out) == ESP_OK;
}

void usage_store_srv_set(int idx, const char *ip, const char *user,
                         const char *pass)
{
    if (idx < 0 || idx >= USAGE_SRV_MAX) return;
    srv_set(idx, ip, user, pass);
}

void usage_store_srv_del(int idx)
{
    if (idx < 0 || idx >= USAGE_SRV_MAX) return;
    srv_set(idx, "", "", "");
}
