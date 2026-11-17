# 门户配置页支持隐藏 方舟 / zcode / 百炼

## Context

ai-usage 固件（ESP32-C3 + LVGL 胸牌）目前固定轮播：火山方舟 → zcode → 百炼 → 服务器×N → 设置，三平台全部无条件采集与显示。用户希望**门户配置页**（Captive Portal，portal_http.c）能勾选控制是否显示这三个平台；已确认：隐藏后**停止拉取**该平台数据（不显示页面、不发 API 请求，状态点只统计可见平台）。

## 改动文件

### 1. `main/usage_store.c` + `usage_store.h` — 持久化 3 个开关

在 NS `"ui"` 加 u8 键 `show_ark` / `show_zcode` / `show_bailian`，缺省（读取失败）为 true。仿现有样板 `usage_store_get_refresh_s` / `usage_store_set_refresh_s`（usage_store.c:167-185），用一个共享 static helper（`show_get(key)` / `show_set(key, show)`）实现 6 个函数，在 usage_store.h:49-51（刷新间隔段）附近声明：

```c
// ---------------- 页面显示开关（命名空间 "ui"，缺省显示） ----------------
bool usage_store_get_show_ark(void);   void usage_store_set_show_ark(bool show);
bool usage_store_get_show_zcode(void); void usage_store_set_show_zcode(bool show);
bool usage_store_get_show_bailian(void); void usage_store_set_show_bailian(bool show);
```

### 2. `main/usage_worker.c` — 隐藏平台跳过采集

`worker_task`（usage_worker.c:22-76）中 ark/zcode/bailian 三个 fetch 块各加 `if (usage_store_get_show_xxx()) { ...原有 fetch+入队... }`，隐藏时整块跳过（不 fetch、不入队）。服务器采集与 `USAGE_RES_DONE` 不变。

### 3. `main/main.c` — 页面布局固定索引 → 动态映射

核心改动：`PAGE_ARK/ZCODE/BAILIAN/PAGE_SRV_BASE`（main.c:40-43）固定索引改为按开关计算的动态布局（复用 `s_srv_order` 位置→槽位映射的既有模式，main.c:96/358）：

- 新增状态：`static bool s_show[3]`（[0]=方舟 [1]=zcode [2]=百炼）、`static int s_prov_view[3]`（逻辑视图位置→provider kind）、`static int s_prov_count`。每 tick 在 `render()` 开头与 `usage_store_srv_list`（main.c:358）一起刷新（每秒 3 次 NVS u8 读取，与现有 per-tick srv_list 同级开销）。
- `page_count()` = `s_prov_count + s_srv_count + 1`；`page_settings()` = `s_prov_count + s_srv_count`（main.c:307-315）。
- `page_show()` switch（main.c:325-342）：`view < s_prov_count` 按 `s_prov_view[view]` 分发 `ui_ark_build`/`ui_zcode_build`/`ui_bailian_build`；服务器页判断改 `view < page_settings()`（现有 default 分支已按此写法）。
- 标题（main.c:367-379）、render 分发（main.c:435-443，服务器 slot 索引 `s_view - PAGE_SRV_BASE` → `s_view - s_prov_count`）、长按 UP/DN 服务器翻页（main.c:287）、提示行（main.c:424）：`PAGE_SRV_BASE` 全部替换为 `s_prov_count`。
- 状态点聚合（main.c:392-398）：NO KEY / WIFI / FAIL 只统计可见平台；`s_prov_count == 0` 视为全 ok。
- 布局失效：tick 内发现 `s_show[]` 与上一 tick 不同时置 `s_built_view = -1`，强制 `page_show` 重建当前页（防止隐藏某家后逻辑视图换指向但旧容器未重建）。
- `handle_result` 的 `USAGE_RES_DONE` 分支加 `s_ever_fetched = true;`（修正三家全隐藏且无服务器时状态点永远 WAIT；对现状无影响）。
- `PAGE_SLOTS_MAX` 保持 `(USAGE_SRV_MAX + 3 + 1)` 上限，删除对 `PAGE_SRV_BASE` 常量本身的依赖。

### 4. `main/portal_http.c` — 门户新增「显示设置」卡片 + `POST /show`

- `send_portal_page`：在卡 5（刷新间隔，portal_http.c:359-391）后插入卡 6「显示设置」：3 个 checkbox（方舟/zcode/百炼，用 `send_frag` + `%s` 预填 NVS 当前值为 `checked`），保存按钮 JS 显式 POST `ark=0|1&zcode=0|1&bailian=0|1` 到 `/show`；原卡 6 服务器标题改为「7. 服务器…」。
- 新增 `handle_show`（仿 `handle_ui` portal_http.c:588-603）：逐字段解析，**字段缺席 = 保持原值**（防异常请求把三家全隐藏），0/1 写入对应 `usage_store_set_show_*`。
- `http_task_fn` 分发表（portal_http.c:767-768 `/ui` 分支后）加 `POST /show` → `handle_show`。

### 5. `README.md` — 文档

「配网」步骤列表与「页面顺序」节补充：门户新增「显示设置」卡片；取消勾选的平台不出现在轮播、不再查询、状态点只统计可见平台。

### 不做的事

- 设备设置页不加开关（已确认仅门户）；设备端中文字符串不变，**无需**重跑 `tools/gen_app_fonts.py`。
- 不改 ui_settings.c / ui_server.c / 各 client。

## 验证

1. **编译**：ESP-IDF 5.5.3（build-validation 为容器内产物，IDF_PATH=/opt/esp/idf）。在可用环境跑 `idf.py -B build-validation build`；若本机无 ESP-IDF 则做静态检查并告知需自行构建。
2. **静态自查**：grep `PAGE_ARK|PAGE_SRV_BASE` 确认 main.c 无遗漏的固定索引引用。
3. **烧录手动验证**（tools/flash.cmd 刷 build-validation 产物，不抹 NVS）：门户勾选/取消→保存后设备轮播页数即时变化；串口日志中被隐藏平台无 fetch 行；状态点只看可见平台；重启后开关保留（NVS）；三家全隐藏时轮播只剩服务器+设置且状态点正常。