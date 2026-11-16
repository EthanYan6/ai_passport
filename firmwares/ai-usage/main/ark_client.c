// main/ark_client.c —— 火山方舟用量查询实现
//
// 与 pc-monitor/tools/pc_status_bridge.py 的 ArkUsageSampler 逐字节一致的
// 火山 V4（HMAC-SHA256）签名：
//   canonical = "POST\n/\nAction=..&Version=..\nhost:..\nx-content-sha256:..\nx-date:..\n"
//               "\nhost;x-content-sha256;x-date\n" + payload_hash
//   string_to_sign = "HMAC-SHA256\n{x-date}\n{date}/{region}/{service}/request\n" + sha256(canonical)
//   派生链 k_date -> k_region -> k_service -> k_signing("request")
// 先探 GetCodingPlanUsage（Coding Plan 个人版），无 QuotaUsage 再降级
// GetAFPUsage（Agent Plan 个人版）。X-Date 取 SNTP 对时后的 UTC 时间。
#include "ark_client.h"
#include "usage_store.h"
#include "app_config.h"
#include "time_sync.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

static const char *TAG = "ark_client";

#define RESP_MAX 8192
#define PAYLOAD "{}"

static const char *const AFP_KEYS[ARK_WINDOWS] = {
    "AFPFiveHour", "AFPWeekly", "AFPMonthly"};
static const char *const CODING_LEVELS[ARK_WINDOWS] = {
    "session", "weekly", "monthly"};

// ---------------- 摘要 / HMAC ----------------

static void sha256_hex(const char *data, char out[65])
{
    unsigned char digest[32];
    mbedtls_sha256((const unsigned char *)data, strlen(data), digest, 0);
    for (int i = 0; i < 32; i++) {
        sprintf(out + i * 2, "%02x", digest[i]);
    }
    out[64] = '\0';
}

static void hmac_sha256(const unsigned char *key, size_t keylen,
                        const char *msg, unsigned char out[32])
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(info, key, keylen, (const unsigned char *)msg, strlen(msg), out);
}

static void hmac_hex(const unsigned char *key, size_t keylen,
                     const char *msg, char out[65])
{
    unsigned char digest[32];
    hmac_sha256(key, keylen, msg, digest);
    for (int i = 0; i < 32; i++) {
        sprintf(out + i * 2, "%02x", digest[i]);
    }
    out[64] = '\0';
}

// ---------------- V4 签名 ----------------

// 生成 x-date 与 Authorization 头（ak/sk 来自调用方）
static void ark_sign(const char *ak, const char *sk, const char *host,
                     const char *region, const char *service,
                     const char *action, const char *version,
                     char *x_date, size_t x_date_len,
                     char *payload_hash, size_t ph_len,
                     char *auth, size_t auth_len)
{
    time_t now = time(NULL);
    struct tm gmt;
    gmtime_r(&now, &gmt);
    strftime(x_date, x_date_len, "%Y%m%dT%H%M%SZ", &gmt);
    const char *short_date = x_date;   // 前 8 位即 yyyyMMdd

    sha256_hex(PAYLOAD, payload_hash);

    // canonical request（注意：headers 块自带结尾换行，join 分隔符再补一个空行）
    char canon[640];
    snprintf(canon, sizeof(canon),
             "POST\n/\nAction=%s&Version=%s\n"
             "host:%s\nx-content-sha256:%s\nx-date:%s\n"
             "\nhost;x-content-sha256;x-date\n%s",
             action, version, host, payload_hash, x_date, payload_hash);

    char canon_hash[65];
    sha256_hex(canon, canon_hash);

    char sts[512];
    snprintf(sts, sizeof(sts), "HMAC-SHA256\n%s\n%.8s/%s/%s/request\n%s",
             x_date, short_date, region, service, canon_hash);

    unsigned char k_date[32], k_region[32], k_service[32], k_signing[32];
    hmac_sha256((const unsigned char *)sk, strlen(sk), short_date, k_date);
    hmac_sha256(k_date, sizeof(k_date), region, k_region);
    hmac_sha256(k_region, sizeof(k_region), service, k_service);
    hmac_sha256(k_service, sizeof(k_service), "request", k_signing);

    char sig_hex[65];
    hmac_hex(k_signing, sizeof(k_signing), sts, sig_hex);

    snprintf(auth, auth_len,
             "HMAC-SHA256 Credential=%s/%.8s/%s/%s/request, "
             "SignedHeaders=host;x-content-sha256;x-date, Signature=%s",
             ak, short_date, region, service, sig_hex);
}

// ---------------- HTTPS ----------------

// POST body "{}" 到 ?Action=..&Version=..，返回 0 且填 resp（调用方 free）表示成功
static esp_err_t ark_post(const char *host, const char *action,
                          const char *x_date, const char *payload_hash,
                          const char *auth, char **resp)
{
    char url[160];
    snprintf(url, sizeof(url), "https://%s/?Action=%s&Version=%s",
             host, action, APP_ARK_API_VERSION);

    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = APP_ARK_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = ESP_OK;
    *resp = NULL;
    do {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "X-Date", x_date);
        esp_http_client_set_header(client, "X-Content-Sha256", payload_hash);
        esp_http_client_set_header(client, "Authorization", auth);

        if (esp_http_client_open(client, (int)strlen(PAYLOAD)) != ESP_OK) {
            err = ESP_FAIL;
            break;
        }
        if (esp_http_client_write(client, PAYLOAD, (int)strlen(PAYLOAD)) < 0) {
            err = ESP_FAIL;
            break;
        }
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGW(TAG, "%s HTTP %d", action, status);
            err = ESP_FAIL;
            break;
        }

        char *buf = malloc(RESP_MAX + 1);
        if (!buf) {
            err = ESP_ERR_NO_MEM;
            break;
        }
        int total = 0;
        int n;
        while (total < RESP_MAX &&
               (n = esp_http_client_read(client, buf + total, RESP_MAX - total)) > 0) {
            total += n;
        }
        buf[total] = '\0';
        *resp = buf;
    } while (0);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        free(*resp);
        *resp = NULL;
    }
    return err;
}

// ---------------- 响应解析 ----------------

static int level_idx(const char *level)
{
    if (!level) return -1;
    for (int i = 0; i < ARK_WINDOWS; i++) {
        if (strcasecmp(level, CODING_LEVELS[i]) == 0) return i;
    }
    return -1;
}

// GetCodingPlanUsage：Result.QuotaUsage[]。无可用数据返回 false（触发降级）。
static bool parse_coding(cJSON *root, ark_data_t *out)
{
    cJSON *result = cJSON_GetObjectItem(root, "Result");
    if (!cJSON_IsObject(result)) result = root;
    if (!cJSON_IsObject(result)) return false;

    const char *plan = cJSON_GetStringValue(cJSON_GetObjectItem(result, "PlanType"));
    if (plan && plan[0]) strlcpy(out->plan, plan, sizeof(out->plan));

    cJSON *arr = cJSON_GetObjectItem(result, "QuotaUsage");
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) return false;

    time_t now = time(NULL);
    bool any = false;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        int idx = level_idx(cJSON_GetStringValue(cJSON_GetObjectItem(item, "Level")));
        if (idx < 0) continue;
        cJSON *pct = cJSON_GetObjectItem(item, "Percent");
        if (!cJSON_IsNumber(pct)) continue;
        out->used[idx] = (float)pct->valuedouble;
        cJSON *cap = cJSON_GetObjectItem(item, "Cap");
        out->quota[idx] = cJSON_IsNumber(cap) ? (float)cap->valuedouble : 100.0f;
        cJSON *rst = cJSON_GetObjectItem(item, "ResetTimestamp");
        if (cJSON_IsNumber(rst) && rst->valuedouble > 0) {
            out->reset_s[idx] = (long)(rst->valuedouble - (double)now);
        }
        out->has_win[idx] = true;
        any = true;
    }
    return any;
}

// GetAFPUsage：Result.AFPFiveHour/AFPWeekly/AFPMonthly，ResetTime 为毫秒时间戳。
// 至少一个窗口 Quota>0 视为有效。
static bool parse_afp(cJSON *root, ark_data_t *out)
{
    cJSON *result = cJSON_GetObjectItem(root, "Result");
    if (!cJSON_IsObject(result)) result = root;
    if (!cJSON_IsObject(result)) return false;

    time_t now = time(NULL);
    bool any = false;
    for (int i = 0; i < ARK_WINDOWS; i++) {
        cJSON *w = cJSON_GetObjectItem(result, AFP_KEYS[i]);
        if (!cJSON_IsObject(w)) continue;
        cJSON *quota = cJSON_GetObjectItem(w, "Quota");
        if (!cJSON_IsNumber(quota)) continue;
        double q = quota->valuedouble;
        cJSON *used = cJSON_GetObjectItem(w, "Used");
        out->quota[i] = (float)q;
        out->used[i] = cJSON_IsNumber(used) ? (float)used->valuedouble : 0.0f;
        cJSON *rst = cJSON_GetObjectItem(w, "ResetTime");
        if (cJSON_IsNumber(rst) && rst->valuedouble > 0) {
            out->reset_s[i] = (long)(rst->valuedouble / 1000.0 - (double)now);
        }
        out->has_win[i] = q > 0;
        any |= q > 0;
    }
    return any;
}

// ---------------- 入口 ----------------

// 单个 Action 的完整流程（签名 + 请求 + 解析）。返回 true 表示拿到了可用数据。
static bool fetch_action(const char *host, const char *ak, const char *sk,
                         const char *action, ark_data_t *out)
{
    char x_date[24], payload_hash[65], auth[512];
    ark_sign(ak, sk, host, APP_ARK_REGION_DEFAULT, APP_ARK_SERVICE_DEFAULT,
             action, APP_ARK_API_VERSION, x_date, sizeof(x_date),
             payload_hash, sizeof(payload_hash), auth, sizeof(auth));

    char *resp = NULL;
    if (ark_post(host, action, x_date, payload_hash, auth, &resp) != ESP_OK) {
        return false;
    }

    ark_data_t parsed = { 0 };
    for (int i = 0; i < ARK_WINDOWS; i++) parsed.reset_s[i] = -1;
    strlcpy(parsed.plan, out->plan, sizeof(parsed.plan));
    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) return false;

    bool ok = strcmp(action, APP_ARK_ACTION_PRIMARY) == 0
                  ? parse_coding(root, &parsed)
                  : parse_afp(root, &parsed);
    cJSON_Delete(root);
    if (!ok) return false;

    memcpy(out, &parsed, sizeof(*out));
    out->valid = true;
    out->err = 0;
    return true;
}

void ark_client_fetch(ark_data_t *result)
{
    memset(result, 0, sizeof(*result));
    for (int i = 0; i < ARK_WINDOWS; i++) result->reset_s[i] = -1;

    char ak[USAGE_ARK_AK_MAX], sk[USAGE_ARK_SK_MAX], host_cfg[USAGE_ARK_HOST_MAX];
    if (usage_store_get_ark(ak, sizeof(ak), sk, sizeof(sk),
                            host_cfg, sizeof(host_cfg)) != ESP_OK || !ak[0] || !sk[0]) {
        result->err = 1;
        return;
    }
    if (!time_sync_valid()) {
        result->err = 3;
        return;
    }
    const char *host = host_cfg[0] ? host_cfg : APP_ARK_HOST_DEFAULT;

    if (fetch_action(host, ak, sk, APP_ARK_ACTION_PRIMARY, result)) return;
    if (fetch_action(host, ak, sk, APP_ARK_ACTION_FALLBACK, result)) return;
    result->err = 2;
}
