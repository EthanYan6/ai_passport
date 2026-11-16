// main/zcode_client.c —— 智谱 GLM Coding Plan 用量查询实现
//
// GET https://open.bigmodel.cn/api/monitor/usage/quota/limit
// 认证头直接放 API Key（无 Bearer 前缀）。响应结构（cc-switch #1588 / 官方插件）：
//   {code:200, success:true, data:{level:"pro", limits:[
//     {type:"TOKENS_LIMIT", percentage, nextResetTime, ...},   // 两条：5小时 + 每周
//     {type:"TIME_LIMIT", percentage, currentValue, usage, ...} // MCP 月度
//   ]}}
// TOKENS_LIMIT 按 nextResetTime 升序排序后 [0]=近5小时、[1]=每周。
#include "zcode_client.h"
#include "usage_store.h"
#include "app_config.h"
#include "time_sync.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "zcode_client";

#define RESP_MAX 8192
#define TOKEN_LIMITS_MAX 4

typedef struct {
    int pct;
    long reset;
} zcode_token_t;

// 排序键：未知重置时间(-1)排最后
static long token_sort_key(long reset)
{
    return reset < 0 ? (1L << 62) : reset;
}

// nextResetTime 兼容数字/数字字符串，>1e11 视为毫秒时间戳。无有效值返回 -1。
static long next_reset_seconds(cJSON *item)
{
    cJSON *n = cJSON_GetObjectItem(item, "nextResetTime");
    double v = -1;
    if (cJSON_IsNumber(n)) {
        v = n->valuedouble;
    } else if (cJSON_IsString(n) && n->valuestring[0]) {
        char *end = NULL;
        v = strtod(n->valuestring, &end);
        if (end == n->valuestring) v = -1;
    }
    if (v <= 0) return -1;
    if (v > 1e11) v /= 1000.0;
    if (!time_sync_valid()) return -1;
    long remain = (long)(v - (double)time(NULL));
    return remain > 0 ? remain : 0;
}

static void fetch_and_parse(const char *key, zcode_data_t *out)
{
    esp_http_client_config_t cfg = {
        .url = APP_ZCODE_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = APP_ZCODE_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        out->err = 2;
        return;
    }

    char *resp = NULL;
    do {
        esp_http_client_set_header(client, "Authorization", key);
        esp_http_client_set_header(client, "Content-Type", "application/json");

        if (esp_http_client_open(client, -1) != ESP_OK) break;
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGW(TAG, "zcode HTTP %d", status);
            break;
        }

        resp = malloc(RESP_MAX + 1);
        if (!resp) break;
        int total = 0;
        int n;
        while (total < RESP_MAX &&
               (n = esp_http_client_read(client, resp + total, RESP_MAX - total)) > 0) {
            total += n;
        }
        resp[total] = '\0';
    } while (0);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!resp) {
        out->err = 2;
        return;
    }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) {
        out->err = 2;
        return;
    }

    cJSON *success = cJSON_GetObjectItem(root, "success");
    cJSON *code = cJSON_GetObjectItem(root, "code");
    if ((cJSON_IsBool(success) && !cJSON_IsTrue(success)) ||
        (cJSON_IsNumber(code) && code->valuedouble != 200)) {
        cJSON_Delete(root);
        out->err = 2;
        return;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsObject(data)) {
        cJSON_Delete(root);
        out->err = 2;
        return;
    }

    const char *level = cJSON_GetStringValue(cJSON_GetObjectItem(data, "level"));
    if (level && level[0]) strlcpy(out->level, level, sizeof(out->level));

    cJSON *limits = cJSON_GetObjectItem(data, "limits");
    if (!cJSON_IsArray(limits)) {
        cJSON_Delete(root);
        out->err = 2;
        return;
    }

    // TOKENS_LIMIT 收集后按 nextResetTime 升序排：[0]=近5小时、[1]=每周
    zcode_token_t tokens[TOKEN_LIMITS_MAX];
    int token_n = 0;
    bool have_time = time_sync_valid();

    cJSON *item;
    cJSON_ArrayForEach(item, limits) {
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(item, "type"));
        cJSON *pct = cJSON_GetObjectItem(item, "percentage");
        if (!cJSON_IsNumber(pct)) continue;
        int p = (int)pct->valuedouble;
        if (p < 0) p = 0;
        if (p > 100) p = 100;

        if (type && strcmp(type, "TOKENS_LIMIT") == 0 && token_n < TOKEN_LIMITS_MAX) {
            tokens[token_n].pct = p;
            tokens[token_n].reset = have_time ? next_reset_seconds(item) : -1;
            token_n++;
        } else if (type && strcmp(type, "TIME_LIMIT") == 0) {
            out->pct[ZCODE_ROWS - 1] = p;
            cJSON *cur = cJSON_GetObjectItem(item, "currentValue");
            cJSON *total = cJSON_GetObjectItem(item, "usage");
            out->cur[ZCODE_ROWS - 1] = cJSON_IsNumber(cur) ? (long)cur->valuedouble : -1;
            out->total[ZCODE_ROWS - 1] = cJSON_IsNumber(total) ? (long)total->valuedouble : -1;
            out->reset_s[ZCODE_ROWS - 1] = -1;
            out->has_row[ZCODE_ROWS - 1] = true;
        }
    }

    // 升序插入排序（条目少，直接排；未知重置时间排最后）
    for (int i = 1; i < token_n; i++) {
        zcode_token_t t = tokens[i];
        long tk = token_sort_key(t.reset);
        int j = i - 1;
        while (j >= 0 && token_sort_key(tokens[j].reset) > tk) {
            tokens[j + 1] = tokens[j];
            j--;
        }
        tokens[j + 1] = t;
    }

    for (int i = 0; i < 2 && i < token_n; i++) {
        out->pct[i] = tokens[i].pct;
        out->reset_s[i] = tokens[i].reset;
        out->has_row[i] = true;
    }

    cJSON_Delete(root);
    out->valid = true;
    out->err = 0;
}

void zcode_client_fetch(zcode_data_t *result)
{
    memset(result, 0, sizeof(*result));
    for (int i = 0; i < ZCODE_ROWS; i++) result->reset_s[i] = -1;

    char key[USAGE_ZCODE_KEY_MAX];
    if (usage_store_get_zcode_key(key, sizeof(key)) != ESP_OK || !key[0]) {
        result->err = 1;
        return;
    }
    fetch_and_parse(key, result);
}
