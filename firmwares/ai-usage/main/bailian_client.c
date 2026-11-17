// main/bailian_client.c —— 阿里云百炼 Coding Plan 真实用量查询
// 流程与官方 bailian-cli 2.10.1 一致：
// 1. AK/SK 通过 ACS3-HMAC-SHA256 调用 generateAccessToken。
// 2. 携带 Bearer 令牌调用控制台 Coding Plan 查询接口。
// 凭据仅用于签名与请求，不写入日志。
#include "bailian_client.h"
#include "app_config.h"
#include "usage_store.h"
#include "time_sync.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

static const char *TAG = "bailian";

#define RESPONSE_MAX (16 * 1024)
#define BAILIAN_SIGNED_HEADERS \
    "content-type;host;x-acs-action;x-acs-content-sha256;x-acs-date;" \
    "x-acs-signature-nonce;x-acs-version"

static void hex_encode(char *out, const uint8_t *data, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex[data[i] >> 4];
        out[i * 2 + 1] = hex[data[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

static void sha256_hex(char out[65], const char *data)
{
    uint8_t digest[32];
    mbedtls_sha256((const unsigned char *)data, strlen(data), digest, 0);
    hex_encode(out, digest, sizeof(digest));
    memset(digest, 0, sizeof(digest));
}

static void hmac_sha256_hex(char out[65], const char *key, const char *data)
{
    uint8_t digest[32];
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(info, (const unsigned char *)key, strlen(key),
                    (const unsigned char *)data, strlen(data), digest);
    hex_encode(out, digest, sizeof(digest));
    memset(digest, 0, sizeof(digest));
}

static esp_err_t http_post(const char *url, const char *body,
                           const char *content_type, const char *authorization,
                           char **out, size_t *out_len)
{
    *out = NULL;
    *out_len = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = APP_BAILIAN_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;
    esp_http_client_set_header(client, "Content-Type", content_type);
    if (authorization) {
        esp_http_client_set_header(client, "Authorization", authorization);
    }
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS 请求失败: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
    int status = esp_http_client_get_status_code(client);
    int64_t len = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "HTTPS POST 状态 %d", status);
    if (status != 200 || len > RESPONSE_MAX) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    size_t cap = len > 0 ? (size_t)len + 1 : 4096;
    char *buf = malloc(cap);
    if (!buf) {
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    size_t used = 0;
    while (used + 1 < cap) {
        int n = esp_http_client_read(client, buf + used, (int)(cap - 1 - used));
        if (n < 0) {
            free(buf);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (n == 0) break;
        used += (size_t)n;
        if (len < 0 && used + 1 == cap) {
            size_t new_cap = cap * 2;
            if (new_cap > RESPONSE_MAX + 1) break;
            char *next = realloc(buf, new_cap);
            if (!next) {
                free(buf);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            buf = next;
            cap = new_cap;
        }
    }
    esp_http_client_cleanup(client);
    if (used == 0 || (len >= 0 && used != (size_t)len)) {
        free(buf);
        return ESP_FAIL;
    }
    buf[used] = '\0';
    *out = buf;
    *out_len = used;
    return ESP_OK;
}

static esp_err_t generate_console_token(const char *ak, const char *sk,
                                        char *token_out, size_t token_out_len)
{
    char date[24];
    time_t now = time(NULL);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

    char nonce[37];
    uint8_t uuid[16];
    esp_fill_random(uuid, sizeof(uuid));
    uuid[6] = (uuid[6] & 0x0f) | 0x40;
    uuid[8] = (uuid[8] & 0x3f) | 0x80;
    snprintf(nonce, sizeof(nonce),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
             "%02x%02x%02x%02x%02x%02x",
             uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5],
             uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11],
             uuid[12], uuid[13], uuid[14], uuid[15]);

    static const char body[] = "{}";
    char body_sha[65];
    char canonical[1536];
    char canonical_sha[65];
    char string_to_sign[128];
    char signature[65];
    char authorization[512];
    sha256_hex(body_sha, body);
    int n = snprintf(canonical, sizeof(canonical),
                     "POST\n" APP_BAILIAN_OPENAPI_PATH "\n\n"
                     "content-type:application/json\n"
                     "host:" APP_BAILIAN_OPENAPI_HOST "\n"
                     "x-acs-action:GenerateCLIAccessToken\n"
                     "x-acs-content-sha256:%s\n"
                     "x-acs-date:%s\n"
                     "x-acs-signature-nonce:%s\n"
                     "x-acs-version:" APP_BAILIAN_OPENAPI_VERSION "\n"
                     "\n" BAILIAN_SIGNED_HEADERS "\n%s",
                     body_sha, date, nonce, body_sha);
    if (n < 0 || n >= (int)sizeof(canonical)) return ESP_FAIL;
    sha256_hex(canonical_sha, canonical);
    snprintf(string_to_sign, sizeof(string_to_sign),
             "ACS3-HMAC-SHA256\n%s", canonical_sha);
    hmac_sha256_hex(signature, sk, string_to_sign);
    n = snprintf(authorization, sizeof(authorization),
                 "ACS3-HMAC-SHA256 Credential=%s,SignedHeaders=%s,Signature=%s",
                 ak, BAILIAN_SIGNED_HEADERS, signature);
    if (n < 0 || n >= (int)sizeof(authorization)) return ESP_FAIL;

    char *resp = NULL;
    size_t resp_len = 0;
    esp_err_t err = http_post(APP_BAILIAN_OPENAPI_URL, body, "application/json",
                              authorization, &resp, &resp_len);
    memset(canonical, 0, sizeof(canonical));
    memset(string_to_sign, 0, sizeof(string_to_sign));
    memset(signature, 0, sizeof(signature));
    memset(authorization, 0, sizeof(authorization));
    if (err != ESP_OK) return err;

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "cliAccessToken");
    if (!cJSON_IsString(token) || !token->valuestring || !token->valuestring[0]) {
        ESP_LOGE(TAG, "令牌响应缺少 cliAccessToken");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    if (strlen(token->valuestring) >= token_out_len) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    strcpy(token_out, token->valuestring);
    cJSON_Delete(root);
    return ESP_OK;
}

// URLSearchParams 的 application/x-www-form-urlencoded 编码。
// 本请求 JSON 不含空格，其他非保留字符统一百分号编码。
static bool form_encode(const char *src, char *out, size_t out_len)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        unsigned char c = *p;
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                     c == '.' || c == '~';
        size_t need = plain ? 1 : 3;
        if (used + need >= out_len) return false;
        if (plain) {
            out[used++] = (char)c;
        } else {
            out[used++] = '%';
            out[used++] = hex[c >> 4];
            out[used++] = hex[c & 0x0f];
        }
    }
    out[used] = '\0';
    return true;
}

static bool json_number(const cJSON *item, double *out)
{
    if (cJSON_IsNumber(item)) {
        *out = item->valuedouble;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring && item->valuestring[0]) {
        char *end = NULL;
        *out = strtod(item->valuestring, &end);
        return end && end != item->valuestring && *end == '\0';
    }
    return false;
}

static bool parse_quota(const cJSON *quota, const char *used_key,
                        const char *total_key, bailian_quota_t *out)
{
    double used = 0;
    double total = 0;
    if (!json_number(cJSON_GetObjectItemCaseSensitive(quota, used_key), &used) ||
        !json_number(cJSON_GetObjectItemCaseSensitive(quota, total_key), &total) ||
        used < 0 || total <= 0 || used > INT_MAX || total > INT_MAX) {
        return false;
    }
    out->used = (int)used;
    out->total = (int)total;
    int percent = (int)(used * 100.0 / total + 0.5);
    out->percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    out->valid = true;
    return true;
}

static cJSON *unwrap_response(cJSON *root)
{
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!data) return root;
    cJSON *data_v2 = cJSON_GetObjectItemCaseSensitive(data, "DataV2");
    if (data_v2) {
        cJSON *inner = cJSON_GetObjectItemCaseSensitive(data_v2, "data");
        if (inner) {
            cJSON *nested = cJSON_GetObjectItemCaseSensitive(inner, "data");
            return nested ? nested : inner;
        }
        return data_v2;
    }
    cJSON *nested = cJSON_GetObjectItemCaseSensitive(data, "data");
    return nested ? nested : data;
}

static bool parse_usage(const char *json, bailian_data_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    cJSON *data = unwrap_response(root);
    cJSON *instances = cJSON_GetObjectItemCaseSensitive(data, "codingPlanInstanceInfos");
    const cJSON *instance = NULL;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, instances) {
        const cJSON *status = cJSON_GetObjectItemCaseSensitive(entry, "status");
        if (cJSON_IsString(status) && status->valuestring &&
            strcmp(status->valuestring, "VALID") == 0) {
            instance = entry;
            break;
        }
    }
    const cJSON *quota = cJSON_GetObjectItemCaseSensitive(instance, "codingPlanQuotaInfo");
    bool ok = quota &&
        parse_quota(quota, "per5HourUsedQuota", "per5HourTotalQuota", &out->quotas[0]) &&
        parse_quota(quota, "perWeekUsedQuota", "perWeekTotalQuota", &out->quotas[1]) &&
        parse_quota(quota, "perBillMonthUsedQuota", "perBillMonthTotalQuota", &out->quotas[2]);
    if (ok) out->quota_count = BAILIAN_QUOTA_MAX;
    cJSON_Delete(root);
    return ok;
}

static esp_err_t query_usage(const char *token, bailian_data_t *out)
{
    static const char params[] =
        "{\"Api\":\"" APP_BAILIAN_CONSOLE_API "\",\"V\":\"1.0\",\"Data\":{"
        "\"queryCodingPlanInstanceInfoRequest\":{"
        "\"commodityCode\":\"" APP_BAILIAN_COMMODITY "\",\"onlyLatestOne\":true},"
        "\"cornerstoneParam\":{\"protocol\":\"V2\",\"console\":\"ONE_CONSOLE\","
        "\"productCode\":\"p_efm\",\"switchUserType\":3,"
        "\"consoleSite\":\"BAILIAN_ALIYUN\"}}}";
    char body[1536];
    strcpy(body, "params=");
    if (!form_encode(params, body + 7, sizeof(body) - 7)) return ESP_FAIL;
    if (strlcat(body, "&region=cn-beijing", sizeof(body)) >= sizeof(body)) {
        return ESP_FAIL;
    }

    size_t bearer_len = strlen(token) + 8;
    char *bearer = malloc(bearer_len);
    if (!bearer) return ESP_ERR_NO_MEM;
    snprintf(bearer, bearer_len, "Bearer %s", token);
    char *resp = NULL;
    size_t resp_len = 0;
    esp_err_t err = http_post(APP_BAILIAN_CONSOLE_URL, body,
                              "application/x-www-form-urlencoded", bearer,
                              &resp, &resp_len);
    memset(bearer, 0, bearer_len);
    free(bearer);
    if (err != ESP_OK) return err;
    bool ok = parse_usage(resp, out);
    free(resp);
    if (!ok) {
        ESP_LOGE(TAG, "控制台响应缺少有效 Coding Plan 三档额度");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "百炼三档额度解析成功");
    return ESP_OK;
}

void bailian_client_fetch(bailian_data_t *out)
{
    memset(out, 0, sizeof(*out));
    char ak[USAGE_BAILIAN_AK_MAX];
    char sk[USAGE_BAILIAN_SK_MAX];
    if (usage_store_get_bailian(ak, sizeof(ak), sk, sizeof(sk)) != ESP_OK ||
        !ak[0] || !sk[0]) {
        out->err = 1;
        return;
    }
    if (!time_sync_valid()) {
        memset(sk, 0, sizeof(sk));
        out->err = 3;
        return;
    }

    char *token = malloc(2048);
    if (!token) {
        memset(sk, 0, sizeof(sk));
        out->err = 2;
        return;
    }
    esp_err_t err = generate_console_token(ak, sk, token, 2048);
    memset(sk, 0, sizeof(sk));
    if (err != ESP_OK) {
        free(token);
        out->err = 2;
        return;
    }

    err = query_usage(token, out);
    memset(token, 0, 2048);
    free(token);
    out->err = err == ESP_OK ? 0 : 2;
}
