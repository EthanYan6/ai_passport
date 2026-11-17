# AI 用量胸牌（ai-usage）

在 ESP32-C3 + 1.69" 彩屏胸牌上周期显示火山方舟、智谱 zcode、阿里云百炼 Coding Plan 与多台 SSH 服务器用量。

- 方舟：显示 Token 用量、限额、百分比与用量进度条
- 智谱 zcode：显示 5 小时窗口、每周限额与月度 MCP 调用次数
- 阿里云百炼：显示 Coding Plan 的 5 小时、每周、每月三档真实请求额度
- 服务器：经 wolfSSH 登录执行一条采集命令（/proc/stat 两次采样算 CPU、/proc/meminfo、df 根分区、docker ps），显示 CPU、内存、磁盘占用与 Docker 容器列表（名称/状态/端口映射，长按上/下翻容器页）
- 每屏自动轮播、OK 手动换页、OK 长按暂停、30 秒～30 分钟可选自动刷新
- 手机扫码进入 Captive Portal，配置 WiFi、各服务凭据、服务器与刷新间隔
- 门户「显示设置」可勾选隐藏 方舟 / zcode / 百炼 任一平台：不进轮播、不再查询，状态点只统计可见平台
- 页面按需构建并复用 LVGL 缓冲；后台网络与 SSH 采集串行执行

## 构建

```sh
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor
```

## 配网

1. 首次开机（未配置 WiFi）自动进入配网页；平时切到设置页长按 `OK` 进入配网
2. 用手机相机扫屏幕二维码直连热点，或手动连接 `Usage-XXXX`（密码 `12345678`，见 `APP_AP_PASS_8371`）；设备仅支持 2.4GHz WiFi
3. 连接后配置门户通常自动弹出；未弹出时打开 `http://192.168.4.1/`
4. 依次填写 WiFi、火山方舟、智谱 zcode、阿里云百炼及 SSH 服务器信息
5. 需要时在「显示设置」卡片勾选/取消各平台页面（默认全部显示）

### 火山方舟

填写 Access Key ID / Secret Access Key（AK/SK），可修改 Host 与套餐标签。设备通过 V4 签名查询用量，已保存的密钥不会回显。

### 智谱 zcode

填写国内版智谱开放平台 API Key，设备查询 GLM Coding Plan 用量。

### 阿里云百炼 Coding Plan

填写阿里云 OpenAPI `AccessKey ID` 和 `AccessKey Secret`。设备按官方 `bailian-cli` 的流程，先用 AK/SK 换取控制台访问令牌，再查询有效 Coding Plan 实例的三档额度；页面直接显示接口返回的已用次数与总次数，不硬编码套餐上限。

这里的 AK/SK 不是 `sk-sp-*` 模型 API Key。建议使用仅授予百炼控制台所需权限的 RAM 子账号，已保存凭据不会回显，也不会写入日志。

### SSH 服务器

服务器地址支持 `域名/IP 域名/IP:端口 ...` 或每行一个服务器；添加单台服务器时也可独立填写 1–65535 的 SSH 端口，未填写时默认为 22。旧配置仍按 22 读取。

## 页面顺序

1. 火山方舟
2. 智谱 zcode
3. 阿里云百炼 Coding Plan
4. SSH 服务器 ×N
5. 设置页（刷新间隔 / 重新配网）

未配置对应服务时，页面显示配置提示；网络或接口异常时显示错误状态，不展示模拟用量。

门户「显示设置」中取消勾选的平台从上述顺序中移除（其余顺延），后台也不再查询该平台；
顶部状态点（WIFI / FAIL / NO KEY）只统计可见平台，重启后设置保留。

## 源码结构

```
main/
  main.c               应用状态机与页面调度
  usage_worker.c       后台串行采集
  ark_client.c         方舟 V4 签名与 HTTP 查询
  zcode_client.c       zcode HTTPS 查询
  bailian_client.c     百炼 ACS3 签名、控制台令牌与三档额度查询
  srv_client.c         wolfSSH 采集（CPU/内存/磁盘 + Docker 容器）
  usage_store.c        NVS 持久化
  portal_http.c        Captive Portal（DNS + HTTP）
  wifi_manager.c       WiFi STA/AP 管理
  time_sync.c          SNTP 对时
  ui_ark.c             方舟页
  ui_zcode.c           zcode 页
  ui_bailian.c         百炼 Coding Plan 页
  ui_server.c          服务器页
  ui_settings.c        设置页
  ui_provision.c       配网页
  ui_common.c          页面骨架与公共控件
  assets/fonts/        生成中文字库
```

修改中文 UI 后运行：

```sh
py -3.15 tools/gen_app_fonts.py
```
