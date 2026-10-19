# ai_passport — pc-monitor 桌面胸牌

ESP32-C3 胸牌固件 + 电脑端伴侣程序：设备通过 USB 数据线连接电脑，实时显示 CPU、内存、磁盘、网络、显卡、进程、端口、火山方舟用量等信息，并可通过 TRAE hooks 在"战况"页监管 TRAE 的命令执行。

```
TRAE hooks ──TCP 127.0.0.1:8765──┐
                                 ▼
设备(ESP32-C3) ◄──USB 串口(VID 303A)──► pc_status_bridge.py（电脑端守护进程）
```

设备是请求发起方：每隔约 1.5 秒发送 `$REQ#`，bridge 应答状态帧；bridge 自动检测端口、断线自动重连，拔插设备无需任何操作。

---

## 1. 环境要求

**电脑端（Windows）**

- Python 3（`python` / `pythonw` 在 PATH 中，且路径不含空格）
- 依赖包：`pyserial`、`psutil`（强烈建议装，否则只显示主机名/系统/开机时长）

可选增强：

- NVIDIA 显卡：装好驱动即可（通过 `nvidia-smi` 读取，无额外依赖）
- CPU 温度 / 显卡风扇：运行 LibreHardwareMonitor 并开启其 Web 服务器（`http://127.0.0.1:8085/data.json`）
- 火山方舟用量页：填写 `firmwares/pc-monitor/tools/ark_config.json`（参考 `ark_config.json.example`），填入 `access_key` / `secret_key`
- TRAE 战况页：安装了 TRAE（国内版），hooks 路径为 `%userprofile%\.trae-cn\hooks.json`

**固件构建/刷机**

- Docker（使用 `espressif/idf:v5.5.3` 镜像，无需本地安装 ESP-IDF）
- Python 的 `esptool` 模块（刷机用）

---

## 2. 安装

```powershell
# 电脑端依赖
pip install pyserial psutil
```

（可选）火山方舟用量页：把 `firmwares/pc-monitor/tools/ark_config.json.example` 复制为同目录的 `ark_config.json`，填入火山引擎的 `access_key` / `secret_key`。改 key 只需编辑该文件，bridge 会自动重新读取，无需重启。

---

## 3. 启动服务（电脑端 bridge）

**方式一：手动运行**

```powershell
cd firmwares\pc-monitor
python tools\pc_status_bridge.py              # 守护进程：自动检测端口、自动重连
python tools\pc_status_bridge.py --port COM6   # 多个串口时显式指定
python tools/pc_status_bridge.py --once        # 本机采样一次并退出（调试用）
```

**方式二：注册开机自启（推荐）**

双击 `firmwares\pc-monitor\tools\install_bridge.cmd`（一键：检查依赖 + 注册计划任务 + 立即启动），或：

```powershell
powershell -ExecutionPolicy Bypass -File tools\install_autostart.ps1    # 注册计划任务
Start-ScheduledTask -TaskName 'FoloToy PcMon Bridge'                    # 立即启动
```

之后每次登录 Windows 自动运行，插上 USB 线即可。

**服务的停止与恢复：**

```powershell
# 监控服务关闭
Stop-ScheduledTask -TaskName 'FoloToy PcMon Bridge'

# 监控服务打开（已注册过计划任务时）
Start-ScheduledTask -TaskName 'FoloToy PcMon Bridge'
# 未注册过则先跑 install_autostart.ps1 再 Start
```

彻底移除自启任务：`powershell -ExecutionPolicy Bypass -File tools\install_autostart.ps1 -Remove`

日志：`%LOCALAPPDATA%\FoloToy\pcmon-bridge.log`（加 `--verbose` 可看调试细节）。bridge 持有单实例锁，重复启动会自动退出。

---

## 4. 构建固件

用 Docker 构建（无需本地 ESP-IDF；必须把仓库根挂到 `/project`，否则 build 缓存报错）：

```powershell
docker run --rm -v "D:\k1\ai_passport:/project" -w /project/firmwares/pc-monitor espressif/idf:v5.5.3 idf.py build
```

需要刷整机的完整镜像时再合并：

```powershell
docker run --rm -v "D:\k1\ai_passport:/project" -w /project/firmwares/pc-monitor espressif/idf:v5.5.3 idf.py merge-bin -o pc-monitor-full.bin
```

改了固件里的中文文案？先重新生成字库再构建（扫描 `main/*.c` 字符串自动收集汉字，缺字的文案会显示空白）：

```powershell
python tools\gen_app_fonts.py
```

---

## 5. 刷机

**注意：烧录前必须先停掉 bridge（它占用串口）：**

```powershell
Stop-ScheduledTask -TaskName 'FoloToy PcMon Bridge'
```

**方式一：完整镜像（推荐初次刷机）**

```powershell
cd firmwares\pc-monitor
tools\flash.cmd            # 自动检测 ESP32-C3 的 USB-Serial-JTAG 端口（VID 303A）
tools\flash.cmd COM6       # 或显式指定端口
```

`flash.cmd` 把 `build\pc-monitor-full.bin` 烧到 `0x0`（ bootloader + 分区表 + 应用 + NVS 全部覆盖）。

**方式二：只更新应用分区（保留设备上的 NVS 配置）**

```powershell
python -m esptool --port COM6 -b 921600 write-flash 0x10000 build\pc-monitor.bin
```

（本机 esptool 不在 PATH，用 `python -m esptool` 调用。）

刷完重新启动 bridge：`Start-ScheduledTask -TaskName 'FoloToy PcMon Bridge'`

---

## 6. 屏幕显示

设备上电后直接进入仪表界面，共 13 个页面，循环轮询显示：

| 页面 | 内容 |
|------|------|
| 核心 | CPU 型号、核心数、各核心占用条 |
| CPU | 总占用、频率、趋势图 |
| 内存 | 内存/交换分区占用、已用/总量 |
| 磁盘 | 磁盘占用、读写速率 |
| 网络 | 上下行速率、累计流量 |
| 趋势 | CPU 占用历史曲线 |
| 系统 | 主机名、系统版本、用户、开机时长、电池 |
| 显卡 | GPU 型号、利用率、显存、温度、风扇、功耗 |
| 进程 | Top 进程列表（CPU 或内存排序） |
| 端口 | 监听端口列表、已建立连接数 |
| 火山 | 火山方舟 Coding Plan / Agent Plan 用量、额度、重置倒计时 |
| 战况 | TRAE 状态徽章、任务号、编码时长、最近审批、通过率、命令审批 |
| 代理 | bridge 代理状态（协议版本、psutil 可用性） |

顶部标题栏显示当前页名、链路状态点和电池电量。

---

## 7. 按键操作

| 按键 | 作用 |
|------|------|
| UP / DOWN | 翻页（循环） |
| OK 短按 | 立即刷新；进程页 = 切换 CPU/内存排序；战况页 = **批准**待审批命令 |
| OK 长按 | 暂停/恢复自动轮询；战况页 = **拒绝**待审批命令 |

---

## 8. TRAE 战况页（可选）

bridge 内置一个 loopback TCP 服务（`127.0.0.1:8765`），TRAE 的 hooks 把事件推给它，随状态帧下发到设备战况页。

**安装 hooks：**

```powershell
cd firmwares\pc-monitor
python tools\install_hooks.py          # 安装/刷新（幂等）
python tools\install_hooks.py --status # 查看安装状态
python tools/install_hooks.py --remove # 移除
```

它会写入 `%userprofile%\.trae-cn\hooks.json` 共 5 条 hook（UserPromptSubmit / Stop / PreToolUse(RunCommand) / PreToolUse(Edit|Write) / PostToolUse(RunCommand)），并自动备份原文件。

**启用（重要）：**

1. **重启 TRAE**（hooks.json 仅在启动时加载）
2. TRAE 设置 > Hooks > 打开所配置 Hooks 的开关
3. 运行方式选 **"本地自动运行"**（沙箱模式连不了 127.0.0.1:8765）

**效果：**

- TRAE 思考/敲代码/空闲时，战况页小人头顶的状态徽章变色；无任务时小人躺平、徽章显示"已累瘫"
- RunCommand（含危险命令）触发时，设备自动跳到战况页弹出审批框（小人大小不变，下方显示待执行命令与按键提示）：
  - OK 短按 = 允许执行，OK 长按 = 拒绝执行
  - 120 秒不操作 = 超时回退为 TRAE 原生确认弹窗（`permissionDecision: ask`）
- 命令执行后在战况页显示通过/失败与累计通过率

**排查：**

- TRAE 设置 > Hooks > 运行日志（Agent Hooks 输出面板）：看 TRAE 是否调用/报错
- `%LOCALAPPDATA%\FoloToy\trae-hook.log`：hook 每次被调写一行
- bridge 日志：`%LOCALAPPDATA%\FoloToy\pcmon-bridge.log`
- 注意：hooks.json 中的命令禁止带引号（TRAE 的 PowerShell 包装脚本会解析失败），`install_hooks.py` 已强制无空格裸路径；Python / 仓库路径都不能含空格
