# 新固件 ai-usage：独立联网的 AI 用量监控胸牌

## Context

pc-monitor 的火山方舟用量页依赖 PC 端 bridge（`pc_status_bridge.py`）经 USB 串口转发，设备本身没有网络能力。本次在 `firmwares/ai-usage/` 新建一个**完全独立**的固件：设备自己连 WiFi、自己调用火山引擎与智谱（zcode/GLM Coding Plan）用量 API。首次使用参考 minimal-checkin 的成熟方案：屏幕显示二维码 → 手机扫码连设备热点 → 自动弹出配网门户，在门户里配置 WiFi、火山 AK/SK、智谱 API Key。刷新间隔用户可调（设备按键 + 门户），存 NVS。

数据源（已确认）：
- **火山方舟**：沿用 pc-monitor bridge 的实现——V4 签名（HMAC-SHA256），`POST https://ark.cn-beijing.volcengineapi.com/?Action=GetCodingPlanUsage&Version=2024-01-01`（body `{}`），无 Coding Plan 数据时降级 `GetAFPUsage`
- **zcode（国内版）**：`GET https://open.bigmodel.cn/api/monitor/usage/quota/limit`，头 `Authorization: <API Key>`（**无 Bearer 前缀**）；响应 `data.level` + `data.limits[]`（`type`=TOKENS_LIMIT/TIME_LIMIT、`percentage`、`currentValue`、`usage`、`nextResetTime`）

## 目录结构（全部新建）

```
firmwares/ai-usage/
├── CMakeLists.txt              # 复制 pc-monitor：EXTRA_COMPONENT_DIRS 指向共享 BSP，project(ai-usage)
├── sdkconfig.defaults          # 复制 pc-monitor，另加 CONFIG_LV_FONT_MONTSERRAT_28=y
├── partitions.csv              # 原样复制
├── README.md                   # 新写：配网步骤、密钥获取、构建烧录、按键表
├── main/
│   ├── CMakeLists.txt          # REQUIRES bsp esp_wifi esp_netif esp_event nvs_flash json lwip esp_timer esp_http_client esp-tls mbedtls
│   ├── app_config.h            # 热点参数、ark 默认值(host/region/service/action/version)、zcode URL、刷新预设
│   ├── main.c                  # 编排：模式状态机(normal/provision) + 事件队列 + app task（minimal-checkin main.c 骨架）
│   ├── usage_store.c/h         # NVS 四个命名空间：wifi(ssid/pass)、ark(ak/sk)、zcode(apikey)、ui(refresh_sec)
│   ├── wifi_manager.c/h        # 复制 minimal-checkin，NVS 调用改为 usage_store，热点名 "Usage-XXXX"
│   ├── portal_http.c/h         # 复制改造：DNS 劫持/扫描/表单框架保留；卡片改为 ①WiFi ②火山 AK·SK ③智谱 API Key ④刷新间隔；端点 /scan /save /ark /zcode /ui
│   ├── time_sync.c/h           # 原样复制（SNTP，火山签名与倒计时都需要真实时间）
│   ├── ark_client.c/h          # V4 签名(mbedtls_sha256/mbedtls_md_hmac) + 两 Action 探测 + cJSON 解析 → ark_data_t{err,plan,3×(quota/used/reset_s)}
│   ├── zcode_client.c/h        # esp_http_client GET + cJSON 解析 → zcode_data_t{err,level,3行(5h Token/周 Token/MCP月度)}
│   ├── usage_worker.c/h        # FreeRTOS 任务：收刷新请求，串行执行 HTTPS（省 TLS 内存），结果 struct 经队列回 app task
│   ├── ui_common.c/h           # pc-monitor 深色主题配色(0x060A12 等) + make_label/make_bar/make_chip/make_container/label_printf（从 app_pcmon.c 提取）
│   ├── ui_ark.c/h              # 火山页：移植 app_pcmon.c build_ark/render_ark（标题+chips+3窗口进度条+重置倒计时）
│   ├── ui_zcode.c/h            # zcode 页：同款布局，行=近5小时 Token%/每周 Token%/MCP 月度(currentValue/usage 次)，chip 显示 data.level
│   ├── ui_settings.c/h         # 设置页：刷新间隔、WiFi/信号/IP/时间/电量、提示长按OK重配网
│   ├── ui_provision.c/h        # 配网页：qrcodegen 绘制 "WIFI:T:WPA;S:Usage-XXXX;P:12345678;;"，改深色主题
│   ├── third_party/qrcodegen.c/h   # 复制 minimal-checkin
│   └── assets/fonts/app_font_14/20/40.c  # 先复制 pc-monitor 占位
└── tools/
    ├── gen_app_fonts.py        # 复制（自动扫描本固件 main/*.c 字符串）
    ├── flash.cmd               # 复制改烧 ai-usage-full.bin
    └── verify_firmware.py      # 复制改名
```

## 关键实现点

**V4 签名移植**（`ark_client.c`，逐字节对齐 `pc_status_bridge.py` 的 `_signed_headers`）：
- canonical request = `POST\n/\nAction=..&Version=..\nhost:..\nx-content-sha256:..\nx-date:..\n` + `\n` + `host;x-content-sha256;x-date` + `\n` + payload_hash
- 派生密钥链 k_date→k_region→k_service→k_signing("request")，签名拼 `HMAC-SHA256 Credential={ak}/{date}/{region}/{service}/request, SignedHeaders=host;x-content-sha256;x-date, Signature=..`
- 请求头同步发送 X-Date / X-Content-Sha256 / Authorization / Content-Type；X-Date 取自 SNTP 对时后的 `time(NULL)`（未对时→err=3 "时间未同步"，火山页显示提示）
- Coding 解析：`Result.QuotaUsage[]` Level(session/weekly/monthly→h/w/m)、Percent→used、Cap→quota、ResetTimestamp→剩余秒；无 QuotaUsage→重签重发 GetAFPUsage（`Result.AFPFiveHour/Weekly/Monthly` 的 Quota/Used/ResetTime(ms)）

**zcode 解析**：`success`/`code==200` 校验；TOKENS_LIMIT 按 nextResetTime 升序排，[0]=近5小时、[1]=近一周，percentage 直接管进度条与配色；TIME_LIMIT=MCP 月度（detail `{currentValue}/{usage} 次`）；倒计时仅当 nextResetTime 为数值型时显示，否则 "--"。

**交互**（240x320 深色科技风，沿用 pc-monitor 按需建页模式防 LVGL 池耗尽）：
- UP/DOWN：火山→zcode→设置 循环翻页
- OK 短按：用量页=立即刷新；设置页=刷新间隔预设循环（30s/1m/2m/5m/10m/30m），即改即生效并写 NVS
- OK 长按：用量页=暂停/恢复自动刷新；设置页=重新配网（AP+QR+门户）
- 顶栏状态点：绿=已连接且最近刷新成功 / 黄=已连接但刷新失败 / 红=WiFi 未连接；右侧电池
- 自动刷新默认 60s；未配置对应密钥的页面显示"未配置"，仅配一家的密钥时另一页不请求

**TLS**：esp_http_client + `esp_crt_bundle_attach`（公共 CA，证书 bundle IDF 默认开启）；请求串行执行、每请求独立 init/cleanup，超时 8s，响应读入动态缓冲后 cJSON 解析。

## 验证

1. **构建**：`docker run --rm -v "<repo>:/project" -w /project/firmwares/ai-usage espressif/idf:v5.5.3 idf.py build` 编译通过
2. **字体**：构建一次后 `python tools/gen_app_fonts.py` 按新文案（智谱/设置/刷新等）重生成字库，再 build 确认无 tofu 缺字
3. **门户**：烧录后扫码连热点，检查配网页渲染与四个卡片的保存（/save /ark /zcode /ui 均返回 OK 且 NVS 生效）
4. **数据**：WiFi 连接后观察日志（TLS 握手、HTTP 200、解析结果），火山页与 pc-monitor bridge 同账号数据对比；OK 短按立即刷新、长按暂停、设置页改间隔后重启验证 NVS 持久化
5. 上述 3/4 需真机与真实密钥，由用户按新 README 执行；本环境中完成 1/2 与代码级自检（签名串构造、解析逻辑的代码评审）
