// main/app_config.h —— 极简打卡全局配置与默认值
#pragma once

// ---------------- 休眠策略 ----------------
// 无按键操作多少秒后进入休眠页（配网页除外）
#define APP_IDLE_STANDBY_SECONDS 30

// ---------------- 配网热点 ----------------
#define APP_AP_PASS_8371 "12345678"   // 热点密码：8 位（WPA2 要求 >=8）
#define APP_AP_CHANNEL 6

// ---------------- 名片默认值（配网页可改，存 NVS "card" 命名空间） ----------------
#define APP_CARD_DEFAULT_NAME "极简打卡"
#define APP_CARD_DEFAULT_TITLE "打卡用户"
#define APP_CARD_DEFAULT_BIO "每天记录，遇见更好的自己"

// ---------------- 打卡事项默认值（配网页可改，存 NVS "checkin"/items） ----------------
#define APP_CHECKIN_DEFAULT_ITEM "打卡"

// ---------------- 对时 ----------------
#define APP_SNTP_HOST_1 "ntp.aliyun.com"
#define APP_SNTP_HOST_2 "pool.ntp.org"
// 有效时间下限：2026-01-01 UTC+8 之前的时间视为无效（还没对上）
#define APP_MIN_VALID_TIME 1767196800
#define APP_TZ "CST-8"
