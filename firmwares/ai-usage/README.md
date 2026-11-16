# ai-usage —— AI 用量监控胸牌

与 pc-monitor（依赖电脑 bridge 经 USB 串口转发）不同，本固件**自带网络栈**：
ESP32-C3 直接连 WiFi，自己调用火山方舟与智谱的用量接口，独立实时显示两家的
Coding Plan 用量。首次使用屏幕显示二维码，扫码连热点后在手机门户里完成全部配置。

## 页面

| 页 | 内容 |
|---|---|
| 火山 | 火山方舟 Coding Plan / Agent Plan 用量：近5小时 / 近一周 / 近一月三个窗口的进度条、已用/额度、重置倒计时 |
| zcode | 智谱 GLM Coding Plan 用量：近5小时 Token、每周 Token、MCP 月度三条进度条 + 套餐等级 |
| 服务器×N | SSH 直连采集的每台服务器状态：IP + 账号、CPU / 内存 / 磁盘（根分区）进度条与明细、Docker 容器列表（短 ID、名称、端口映射），**在门户里添加几台就多几页（最多 4 台）** |
| 设置 | 刷新间隔调整、WiFi/信号/IP/时间/电量/服务器台数 |
| 配网 | 未配置或手动进入时：屏幕二维码，扫码连热点后自动弹出配网门户 |

顶栏状态点：绿=已连接且最近一轮两家用量接口都成功；黄=已连接但刷新失败；红=WiFi 未连接；配网模式显示 AP。

## 服务器 SSH 监控

- 设备作为 SSH 客户端（wolfSSH）直接登录服务器，一条 exec 命令采集全部数据：
  两次 `/proc/stat` 采样计算 CPU 占用、`/proc/meminfo` 内存、`df` 根分区、
  `docker ps` 运行中容器列表（屏幕上最多显示 5 行，表头显示总数）
- 要求：Linux 服务器开放 SSH（22 端口）+ 账号密码认证；无 docker 时容器区为空
- 安全取舍：设备不校验服务器 host key（局域网自用场景）；密码明文存设备 NVS，
  与 WiFi 密码同级，门户修改时密码留空表示保持不变
- SSH 采集与用量接口串行执行，单台总预算 15 秒（连接超时 5 秒），
  服务器不可达时页面显示"连接失败/认证失败/采集失败"，不影响其他页

## 按键

| 按键 | 用量页 / 服务器页 | 设置页 | 配网页 |
|---|---|---|---|
| UP / DOWN | 切页 | 切页 | - |
| OK 短按 | 立即刷新 | 刷新间隔循环切换（30秒/1/2/5/10/30分钟） | - |
| OK 长按 | 暂停/恢复自动刷新 | 重新进入配网 | 退出（已保存过 WiFi 时） |

刷新间隔调整后立即生效并写入 NVS，断电保持。

## 配网流程

1. 首次开机（或设置页长按 OK）进入配网页，屏幕显示二维码与热点信息
2. 手机扫码连接热点 `Usage-XXXX`（密码 `12345678`），多数手机会自动弹出配网页；
   未弹出时浏览器手动打开 `http://192.168.4.1`
3. 门户五张卡片：
   - **WiFi**：扫描选择 + 输入密码，保存后设备立即连接
   - **火山方舟**：AccessKey / SecretKey（可选 Host，留空用默认 `ark.cn-beijing.volcengineapi.com`）
   - **智谱 zcode**：API Key（仅国内版 `open.bigmodel.cn`）
   - **刷新间隔**：下拉选择
   - **服务器**：SSH 监控列表，添加 / 修改 / 删除（最多 4 台，每台一张状态页）
4. 连接成功后设备自动返回用量页并拉取一轮数据

## 密钥获取

- **火山方舟**：火山引擎控制台 → 访问控制 IAM → API 访问密钥（AK/SK）。
  接口为 `GetCodingPlanUsage`（Coding Plan 个人版），无数据自动降级 `GetAFPUsage`
  （Agent Plan 个人版），与 pc-monitor bridge 的 `ark_config.json` 相同签名方式。
- **智谱 zcode**：[bigmodel.cn](https://open.bigmodel.cn) 控制台 → API Key。
  查询接口 `GET /api/monitor/usage/quota/limit`，`Authorization` 头直接放 Key（无 Bearer 前缀）。

> 只配其中一家的密钥也可以用：未配置的页面显示"未配置"，不发起请求。

## 构建与烧录

依赖 Docker（与 pc-monitor 相同，ESP-IDF v5.5.3，目标 esp32c3）：

```bash
# 编译
docker run --rm -v "F:\code\ai_passport:/project" -w /project/firmwares/ai-usage espressif/idf:v5.5.3 idf.py build

# 首次编译后重新生成中文字库（按本固件 main/*.c 的文案扫描），再编译一次
python tools/gen_app_fonts.py
docker run --rm -v "F:\code\ai_passport:/project" -w /project/firmwares/ai-usage espressif/idf:v5.5.3 idf.py build

# 合并整机镜像
docker run --rm -v "F:\code\ai_passport:/project" -w /project/firmwares/ai-usage espressif/idf:v5.5.3 idf.py merge-bin -o ai-usage-full.bin

# 校验镜像布局（可选）
python tools/verify_firmware.py build

# 烧录到 0x0（自动探测 USB-Serial-JTAG 串口，VID 303A）
tools\flash.cmd
```

分区表与 pc-monitor 一致（factory@0x10000，8MB Flash）。也可用 esptool 手动烧：
`python -m esptool --chip esp32c3 --port COMx --baud 460800 write-flash 0x10000 build\ai-usage.bin`

## 实现说明

- **火山 V4 签名**：`main/ark_client.c` 用 mbedtls 在设备端完成 HMAC-SHA256 签名，
  与 `pc-monitor/tools/pc_status_bridge.py` 的 `ArkUsageSampler` 逐字节一致；
  X-Date 取自 SNTP 对时后的 UTC 时间，未对时时显示"时间未同步"
- **zcode 解析**：`main/zcode_client.c` 解析 `data.limits[]`，TOKENS_LIMIT 按
  nextResetTime 升序排（[0]=近5小时、[1]=每周），TIME_LIMIT 映射 MCP 月度
- **串行 HTTPS**：用量接口两路请求串行执行（ESP32-C3 无 PSRAM，TLS 握手内存受限），
  TLS 校验用系统证书 bundle
- **SSH 采集**：`wolfssl/wolfssh` 管理组件（v1.4.20），密码认证 + exec；非阻塞
  socket + select 控制超时，主机密钥不校验（见上文安全取舍）。采集命令在
  `main/app_config.h` 的 `APP_SRV_CMD`（一条 shell 流水线），输出按
  S1/S2/MEM/DSK/C| 前缀分段解析，容器端口串在设备端压缩
- **按需建页**：与 pc-monitor 相同，切页时销毁旧页重建，规避 80KB LVGL 池耗尽
- **字库**：`tools/gen_app_fonts.py` 扫描 `main/*.c` 字符串自动收集中文字符，
  改了界面文案后重新运行即可

## 排查

| 现象 | 处理 |
|---|---|
| 火山页"时间未同步" | SNTP 还没对上时（开机后几秒内或网络不通），等对时成功后按 OK 手动刷新 |
| 火山页"获取失败" | 检查 AK/SK 是否正确、账号是否有 Coding/Agent Plan；日志看 HTTP 状态码 |
| zcode 页"获取失败" | 确认是国内版 API Key（国际版 z.ai 不支持）；Key 授权头不带 Bearer 前缀 |
| 服务器页"连接失败" | 确认服务器可达、SSH 22 端口开放；首次连接含 DNS/握手，等下一轮重试 |
| 服务器页"认证失败" | 门户里核对 SSH 账号密码；修改密码留空表示保持原密码 |
| 服务器页"采集失败" | 服务器需为 Linux 且允许执行 awk/df；容器列表需要 docker 命令可用（无 docker 只影响容器区） |
| 服务器页数据不全 | 单台总预算 15 秒，网络慢会截断；调大刷新间隔或检查服务器负载 |
| 扫码后没弹配网页 | 浏览器手动访问 `http://192.168.4.1` |
| 白屏/重启 | 抓日志看 LVGL 池或堆内存；正常情况三页远低于 pc-monitor 13 页的占用 |
