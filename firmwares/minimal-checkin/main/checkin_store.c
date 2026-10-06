// main/checkin_store.c —— NVS 存取实现
#include "checkin_store.h"
#include "app_config.h"

#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

#define NS_CHECKIN "checkin"
#define NS_WIFI    "wifi"
#define NS_CARD    "card"

esp_err_t checkin_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 分区布局变化导致不可用：擦除后重试
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

// 月份键名：y2026m10
static void month_key(int year, int month, char *out, size_t len)
{
    snprintf(out, len, "y%dm%02d", year, month);
}

uint64_t checkin_store_get_month(int year, int month)
{
    if (month < 1 || month > 12) return 0;
    char key[16];
    month_key(year, month, key, sizeof(key));

    nvs_handle_t h;
    if (nvs_open(NS_CHECKIN, NVS_READONLY, &h) != ESP_OK) return 0;
    uint64_t bits = 0;
    if (nvs_get_u64(h, key, &bits) != ESP_OK) bits = 0;
    nvs_close(h);
    return bits;
}

esp_err_t checkin_store_set_month(int year, int month, uint64_t bits)
{
    if (month < 1 || month > 12) return ESP_ERR_INVALID_ARG;
    char key[16];
    month_key(year, month, key, sizeof(key));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_CHECKIN, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u64(h, key, bits);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// ---------------- 打卡事项 ----------------

#define ITEMS_BUF_LEN (CHECKIN_ITEMS_MAX * CHECKIN_ITEM_NAME_MAX + 8)

// 把 '\n' 连接的串切分为独立名称（去空行、去首尾空格、超长截断），返回条数
static int split_items(const char *joined,
                       char names[][CHECKIN_ITEM_NAME_MAX], int max)
{
    int n = 0;
    const char *p = joined;
    while (p && *p && n < max) {
        const char *nl = strchr(p, '\n');
        size_t l = nl ? (size_t)(nl - p) : strlen(p);
        while (l > 0 && *p == ' ') { p++; l--; }
        while (l > 0 && p[l - 1] == ' ') l--;
        if (l > 0) {
            if (l >= CHECKIN_ITEM_NAME_MAX) l = CHECKIN_ITEM_NAME_MAX - 1;
            memcpy(names[n], p, l);
            names[n][l] = '\0';
            n++;
        }
        p = nl ? nl + 1 : NULL;
    }
    return n;
}

// 读 NVS 中的原始 '\n' 连接串；无记录返回 false
static bool items_raw(char *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NS_CHECKIN, NVS_READONLY, &h) != ESP_OK) return false;
    size_t l = len;
    esp_err_t err = nvs_get_str(h, "items", buf, &l);
    nvs_close(h);
    return err == ESP_OK;
}

int checkin_store_get_item_count(void)
{
    char raw[ITEMS_BUF_LEN];
    char names[CHECKIN_ITEMS_MAX][CHECKIN_ITEM_NAME_MAX];
    if (!items_raw(raw, sizeof(raw))) return 1;   // 默认单项"打卡"
    int n = split_items(raw, names, CHECKIN_ITEMS_MAX);
    return n > 0 ? n : 1;
}

bool checkin_store_get_item_name(int idx, char *buf, size_t len)
{
    if (!buf || len == 0 || idx < 0) return false;
    char raw[ITEMS_BUF_LEN];
    char names[CHECKIN_ITEMS_MAX][CHECKIN_ITEM_NAME_MAX];
    if (!items_raw(raw, sizeof(raw))) {
        if (idx > 0) return false;
        strlcpy(buf, APP_CHECKIN_DEFAULT_ITEM, len);
        return true;
    }
    int n = split_items(raw, names, CHECKIN_ITEMS_MAX);
    if (idx >= n) return false;
    strlcpy(buf, names[idx], len);
    return true;
}

int checkin_store_get_items_joined(char *buf, size_t len)
{
    if (!buf || len == 0) return 0;
    if (!items_raw(buf, len)) {
        return snprintf(buf, len, "%s", APP_CHECKIN_DEFAULT_ITEM);
    }
    return (int)strlen(buf);
}

void checkin_store_set_items(const char *joined)
{
    char raw[ITEMS_BUF_LEN];
    char names[CHECKIN_ITEMS_MAX][CHECKIN_ITEM_NAME_MAX];
    int n = split_items(joined ? joined : "", names, CHECKIN_ITEMS_MAX);
    if (n == 0) {
        strlcpy(names[0], APP_CHECKIN_DEFAULT_ITEM, CHECKIN_ITEM_NAME_MAX);
        n = 1;
    }
    // 规范化后重新用 '\n' 连接存储
    raw[0] = '\0';
    int off = 0;
    for (int i = 0; i < n; i++) {
        off += snprintf(raw + off, sizeof(raw) - off, "%s%s",
                        i ? "\n" : "", names[i]);
    }
    (void)off;

    nvs_handle_t h;
    if (nvs_open(NS_CHECKIN, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_str(h, "items", raw) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

// ---------------- 打卡流水 ----------------

// 日键名：D20261006（大写 D 区别于旧固件 "d..." 3 字节格式，互不误读）
static void day_key(int year, int month, int day, char *out, size_t len)
{
    snprintf(out, len, "D%04d%02d%02d", year, month, day);
}

void checkin_store_mark_item(int year, int month, int day,
                             int item, int state, int minutes)
{
    if (month < 1 || month > 12 || day < 1 || day > 31) return;
    if (item < 0 || item >= CHECKIN_ITEMS_MAX) return;
    char key[16];
    day_key(year, month, day, key, sizeof(key));

    // 格式：[ver=1][{item, state, 分钟低, 分钟高} × n]
    uint8_t buf[1 + CHECKIN_RECORDS_MAX * 4];
    nvs_handle_t h;
    if (nvs_open(NS_CHECKIN, NVS_READWRITE, &h) != ESP_OK) return;
    size_t len = sizeof(buf);
    if (nvs_get_blob(h, key, buf, &len) != ESP_OK || len < 1 || buf[0] != 1)
        len = 0;
    int count = (int)(len / 4);   // len = 1 + count*4；异常置 0
    if (count > CHECKIN_RECORDS_MAX) count = CHECKIN_RECORDS_MAX;

    // upsert：同事项只更新状态与时间
    bool found = false;
    for (int i = 0; i < count; i++) {
        if (buf[1 + i * 4] == (uint8_t)item) {
            buf[1 + i * 4 + 1] = (uint8_t)state;
            buf[1 + i * 4 + 2] = (uint8_t)(minutes & 0xFF);
            buf[1 + i * 4 + 3] = (uint8_t)(minutes >> 8);
            found = true;
            break;
        }
    }
    if (!found) {
        if (count >= CHECKIN_RECORDS_MAX) {
            memmove(buf + 1, buf + 1 + 4, (CHECKIN_RECORDS_MAX - 1) * 4);
            count = CHECKIN_RECORDS_MAX - 1;
        }
        buf[1 + count * 4 + 0] = (uint8_t)item;
        buf[1 + count * 4 + 1] = (uint8_t)state;
        buf[1 + count * 4 + 2] = (uint8_t)(minutes & 0xFF);
        buf[1 + count * 4 + 3] = (uint8_t)(minutes >> 8);
        count++;
    }
    buf[0] = 1;
    nvs_set_blob(h, key, buf, (size_t)(1 + count * 4));
    nvs_commit(h);
    nvs_close(h);

    // 月位图当日重算：全部事项完成 → 完成；任一有记录 → 未完成
    int done_cnt = 0;
    for (int i = 0; i < count; i++)
        if (buf[1 + i * 4 + 1] == 2) done_cnt++;
    int items = checkin_store_get_item_count();
    checkin_state_t st = CHECKIN_NONE;
    if (count > 0) st = (done_cnt >= items) ? CHECKIN_DONE : CHECKIN_UNDONE;
    uint64_t bits = checkin_store_get_month(year, month);
    checkin_store_set_month(year, month, checkin_set(bits, day, st));
}

void checkin_store_cleanup_month(int year, int month)
{
    if (month < 1 || month > 12) return;
    uint64_t bits = checkin_store_get_month(year, month);
    if (bits == 0) return;   // 本来就干净

    int days = checkin_days_in_month(year, month);
    uint64_t cleaned = bits;
    for (int d = 1; d <= days; d++) {
        if (checkin_get(bits, d) == CHECKIN_NONE) continue;
        checkin_record_t tmp[1];
        if (checkin_store_get_records(year, month, d, tmp, 1) == 0)
            cleaned = checkin_set(cleaned, d, CHECKIN_NONE);   // 无记录 → 清状态
    }
    if (cleaned != bits) checkin_store_set_month(year, month, cleaned);
}

int checkin_store_get_records(int year, int month, int day,
                              checkin_record_t *out, int max)
{
    if (!out || max <= 0) return 0;
    if (month < 1 || month > 12 || day < 1 || day > 31) return 0;
    char key[16];
    day_key(year, month, day, key, sizeof(key));

    nvs_handle_t h;
    if (nvs_open(NS_CHECKIN, NVS_READONLY, &h) != ESP_OK) return 0;
    uint8_t buf[1 + CHECKIN_RECORDS_MAX * 4];
    size_t len = sizeof(buf);
    esp_err_t err = nvs_get_blob(h, key, buf, &len);
    nvs_close(h);
    if (err != ESP_OK || len < 1 || buf[0] != 1) return 0;

    int count = (int)(len / 4);
    if (count > CHECKIN_RECORDS_MAX) count = CHECKIN_RECORDS_MAX;

    checkin_record_t tmp[CHECKIN_RECORDS_MAX];
    for (int i = 0; i < count; i++) {
        tmp[i].item = buf[1 + i * 4];
        tmp[i].state = buf[1 + i * 4 + 1];
        tmp[i].minutes = (uint16_t)(buf[1 + i * 4 + 2] | (buf[1 + i * 4 + 3] << 8));
    }
    // 按时间倒序（最新在前），同分钟按事项号升序
    for (int i = 1; i < count; i++) {
        checkin_record_t k = tmp[i];
        int j = i - 1;
        while (j >= 0 && (tmp[j].minutes < k.minutes ||
               (tmp[j].minutes == k.minutes && tmp[j].item > k.item))) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = k;
    }
    if (count > max) count = max;
    for (int i = 0; i < count; i++) out[i] = tmp[i];
    return count;
}

static esp_err_t get_str(const char *ns, const char *key,
                         char *out, size_t out_len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return ESP_ERR_NOT_FOUND;
    size_t len = out_len;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    return err;
}

// ---------------- WiFi 凭据 ----------------

esp_err_t checkin_store_get_wifi(char *ssid, size_t ssid_len,
                                 char *pass, size_t pass_len)
{
    esp_err_t e1 = get_str(NS_WIFI, "ssid", ssid, ssid_len);
    esp_err_t e2 = get_str(NS_WIFI, "pass", pass, pass_len);
    if (e1 != ESP_OK || e2 != ESP_OK) return ESP_ERR_NOT_FOUND;
    return ESP_OK;
}

esp_err_t checkin_store_set_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_WIFI, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(h, "pass", pass);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool checkin_store_has_wifi(void)
{
    char ssid[33] = { 0 }, pass[65] = { 0 };
    return checkin_store_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK;
}

// ---------------- 名片 ----------------

esp_err_t checkin_store_get_card(card_info_t *card)
{
    if (!card) return ESP_ERR_INVALID_ARG;
    memset(card, 0, sizeof(*card));
    if (get_str(NS_CARD, "name", card->name, sizeof(card->name)) != ESP_OK)
        strlcpy(card->name, APP_CARD_DEFAULT_NAME, sizeof(card->name));
    if (get_str(NS_CARD, "title", card->title, sizeof(card->title)) != ESP_OK)
        strlcpy(card->title, APP_CARD_DEFAULT_TITLE, sizeof(card->title));
    if (get_str(NS_CARD, "bio", card->bio, sizeof(card->bio)) != ESP_OK)
        strlcpy(card->bio, APP_CARD_DEFAULT_BIO, sizeof(card->bio));
    return ESP_OK;
}

esp_err_t checkin_store_set_card(const char *name, const char *title, const char *bio)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_CARD, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (name && name[0]) err = nvs_set_str(h, "name", name);
    if (err == ESP_OK && title && title[0]) err = nvs_set_str(h, "title", title);
    if (err == ESP_OK && bio && bio[0]) err = nvs_set_str(h, "bio", bio);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
