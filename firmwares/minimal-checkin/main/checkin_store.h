// main/checkin_store.h —— NVS 存取：月打卡记录 + 个人名片
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "checkin_logic.h"

#define APP_CARD_NAME_MAX 32
#define APP_CARD_TITLE_MAX 32
#define APP_CARD_BIO_MAX 96

typedef struct {
    char name[APP_CARD_NAME_MAX];
    char title[APP_CARD_TITLE_MAX];
    char bio[APP_CARD_BIO_MAX];
} card_info_t;

// NVS 初始化（nvs_flash_init + 可选擦除恢复）。应在启动时先调一次。
esp_err_t checkin_store_init(void);

// 读某年某月（1-12）的打卡位图；无记录返回 0
uint64_t checkin_store_get_month(int year, int month);

// 写某年某月的打卡位图
esp_err_t checkin_store_set_month(int year, int month, uint64_t bits);

// ---------------- 打卡事项（配网页配置，NVS "checkin"/items，'\n' 连接存储） ----------------

#define CHECKIN_ITEMS_MAX      6   // 最多事项数
#define CHECKIN_ITEM_NAME_MAX 16  // 单项名称缓冲区长度（15 字节 + '\0'）

// 事项数（无配置时默认 1 项："打卡"）
int checkin_store_get_item_count(void);

// 取第 idx 个事项名；越界返回 false
bool checkin_store_get_item_name(int idx, char *buf, size_t len);

// 保存事项列表（'\n' 连接串；剔除空行，全空则存默认单项）
void checkin_store_set_items(const char *joined);

// 读原始 '\n' 连接串（无配置返回默认项），返回字节数
int checkin_store_get_items_joined(char *buf, size_t len);

// ---------------- 打卡流水（按日：事项 + 状态 + 时间） ----------------

#define CHECKIN_RECORDS_MAX 16   // 每天最多保留条数（超出丢最旧）

typedef struct {
    uint8_t  item;     // 事项索引（对应 get_item_name）
    uint8_t  state;    // 1=未完成 2=完成（与位图编码一致）
    uint16_t minutes;  // 当天第几分钟（0..1439）
} checkin_record_t;

// 事项打卡（upsert：同一事项当天只保留最新状态与时间），并同步月位图：
// 当天全部事项完成 → "完成"；任一事项有记录 → "未完成"；无记录 → 无
void checkin_store_mark_item(int year, int month, int day,
                             int item, int state, int minutes);

// 净化月位图：位图中有状态、但当日无任何记录流水的日子，状态清零
// （用于清掉旧固件遗留的"有状态无记录"数据，保证日历只认真实打卡记录）
void checkin_store_cleanup_month(int year, int month);

// 读某日流水（按时间倒序：最新一条在前），返回实际条数（≤max）
int checkin_store_get_records(int year, int month, int day,
                              checkin_record_t *out, int max);

// WiFi 凭据（命名空间 "wifi"）
esp_err_t checkin_store_get_wifi(char *ssid, size_t ssid_len,
                                char *pass, size_t pass_len);
esp_err_t checkin_store_set_wifi(const char *ssid, const char *pass);
bool checkin_store_has_wifi(void);

// 名片（命名空间 "card"）。get 时缺失的项填默认值（来自 app_config.h）
esp_err_t checkin_store_get_card(card_info_t *card);
esp_err_t checkin_store_set_card(const char *name, const char *title, const char *bio);
