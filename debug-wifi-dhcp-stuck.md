# Debug Session: wifi-dhcp-stuck
- **Status**: [OPEN]
- **progress_status**: collecting
- **Issue**: ai-usage 配网后设备持续显示“正在连接”；同一设备的其它固件配置后可直接联网。
- **Evidence Source**: HTTP Debug Server + ESP32 serial-to-HTTP bridge
- **Evidence Directory**: C:\Users\yan\AppData\Local\Temp\trae-debug\ai-passport-wifi-dhcp-stuck
- **Log File**: C:\Users\yan\AppData\Local\Temp\trae-debug\ai-passport-wifi-dhcp-stuck\trae-debug-log-wifi-dhcp-stuck.ndjson
- **Environment File**: C:\Users\yan\AppData\Local\Temp\trae-debug\ai-passport-wifi-dhcp-stuck\wifi-dhcp-stuck.env

## Transport Verification
- `GET /health`: `status=ok`, `session_id=wifi-dhcp-stuck`.
- `POST /event`: returned `ok`.
- `trae-debug-log-wifi-dhcp-stuck.ndjson` line 1 contains the `debug-server-probe` event.

## Reproduction Steps
1. 启动 ai-usage，进入设置页并长按 OK 打开配网门户。
2. 提交可用 Wi-Fi 名称和密码。
3. 观察设备连接状态、Wi-Fi 事件、DHCP 状态和 IP 事件。
4. 对比同设备正常固件的 Wi-Fi/netif 初始化与连接结果。

## Existing Observations
- 旧运行日志曾出现 `connected with CMCC-ala-2.4G` 和 DHCP 启动日志，但没有 `GOT_IP`。
- 用户确认其它固件在同一设备上配置后可以直接联网。
- 最新配网流程已保持门户，不再因 STA 仅关联而在 8 秒后自动退出。

## Hypotheses & Verification
| ID | Hypothesis | Likelihood | Effort | Evidence |
|----|------------|------------|--------|----------|
| A | STA 已关联，但 DHCP 请求或响应未进入 lwIP | High | Medium | Pending: DHCP 状态、netif 状态、IP 事件 |
| B | APSTA 配网共存或 Wi-Fi 参数导致 DHCP 无法完成 | Medium | Medium | Pending: APSTA 与纯 STA 对照 |
| C | 实际已拿到 IP，但事件或 UI 状态未更新 | Medium | Low | Pending: ip_info 与 GOT_IP/UI 状态对比 |
| D | 保存的目标热点或连接状态切换不正确 | Medium | Low | Pending: 保存 SSID、NVS 读取值、实际关联 SSID |
| E | ai-usage 与正常固件的 netif/Wi-Fi 初始化顺序存在关键差异 | Medium | Medium | Pending: 代码对照与运行事件对照 |

## Instrumentation Plan
- Wi-Fi 初始化完成后记录运行模式、默认 netif 和 DHCP 初始状态。
- 保存/连接入口记录目标 SSID（不记录密码）及 NVS 回读结果。
- STA_CONNECTED、STA_DISCONNECTED、GOT_IP 记录事件、netif up/link 状态、DHCP 状态和 IP。
- 使用现有 ESP_LOGI 输出带 sessionId/runId/hypothesisId 的结构化字段；HTTP 传输不可用时以串口日志为本地证据源。

## Log Evidence
### pre-01：纯 STA 启动（旧保存热点）
- `wifi_init`: mode=1、ps=0、netif up=0、DHCP=INIT。
- `connect_request`: ssid=CMCC-ala-2.4G、mode=1、ap_active=0、connect_err=0。
- Wi-Fi 驱动关联 CMCC-ala-2.4G 成功。
- `sta_connected`: netif up=1、ip=0.0.0.0、DHCP=START、dhcp_err=0、start_err=20484（已经启动）。
- 30 秒采集窗口内没有 `got_ip`，也没有收到 IP 地址。
- 结论：C（有 IP 但 UI 未更新）在该轮被否定；APSTA 共存不是失败的必要条件；当前对照仍使用旧热点，尚不能与 minimal-checkin 的成功结果直接等价比较。

### 静态对照
- minimal-checkin 与 ai-usage 均创建默认 STA netif、默认事件处理器、默认 Wi-Fi 初始化配置，相关 sdkconfig Wi-Fi/LWIP 配置一致。
- minimal-checkin 依赖默认事件处理器启动 DHCP；ai-usage 额外手动启动，但实测返回 ALREADY_STARTED，不是重复启动成功。
- 已增加 `save_wifi` 观测点，等待用户提交与 minimal-checkin 相同的热点。

## Verification Conclusion
Pending same-hotspot save and connection evidence.

## Final Evidence Gate
- **Evidence Sources**: Pending
- **progress_status**: collecting
- **Hypothesis Conclusions**: A=INCONCLUSIVE, B=INCONCLUSIVE, C=INCONCLUSIVE, D=INCONCLUSIVE, E=INCONCLUSIVE
