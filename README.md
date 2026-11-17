# ai_passport — AI Passport 胸牌固件合集

同一块 AI Passport 胸牌（ESP32-C3 + 彩屏 + 三键）上的三个固件，共享 `ai-passport-repo` 子模块里的 BSP 与同一套构建方式（Docker ESP-IDF v5.5.3）：

| 固件 | 定位 | 联网方式 | 详细文档 |
|------|------|----------|----------|
| [minimal-checkin](firmwares/minimal-checkin/) | 极简打卡日历胸牌 | WiFi 仅对时 + 配网（平时可离线） | 本页 §2 |
| [pc-monitor](firmwares/pc-monitor/) | 电脑状态桌面仪表盘 | USB 数据线连电脑（bridge 推流） | 本页 §4 |
| [ai-usage](firmwares/ai-usage/) | AI 用量 + 服务器监控胸牌 | WiFi 独立联网 | [firmwares/ai-usage/README.md](firmwares/ai-usage/README.md) |

克隆时带子模块：

```sh
git clone --recursive https://github.com/EthanYan6/ai_passport.git
# 已克隆过的：git submodule update --init
```

---

## 通用：构建与刷机

三个固件构建方式一致（无需本地 ESP-IDF；必须把仓库根挂到 `/project`）：

```sh
# 构建（在仓库根目录执行，<固件名> 换成 minimal-checkin / pc-monitor / ai-usage）
docker run --rm -v "F:\code\ai_passport:/project" -w //project/firmwares/<固件名> espressif/idf:v5.5.3 idf.py build

# 需要整机完整镜像时再合并（产物 build/<固件名>-full.bin，可烧 0x0）
docker run --rm -v "F:\code\ai_passport:/project" -w //project/firmwares/<固件名> espressif/idf:v5.5.3 idf.py merge-bin -o <固件名>-full.bin
```

- 完整镜像校验：`python -I tools/verify_firmware.py build`（每个固件的 tools/ 里都有同名脚本，校验合并布局）。
- 刷机（本机装好 esptool，用 `python -m esptool` 调用）：
  - 整机首次刷机：`python -m esptool --chip esp32c3 -p COMx -b 460800 write-flash 0x0 build/<固件名>-full.bin`（覆盖含 NVS 的全部数据）；
  - 升级且想保留设备上的配置：只烧应用分区 `0x10000 build/<固件名>.bin`，或用固件自带的 `tools/flash.cmd`（ai-usage 的 flash.cmd 三文件分别写入、不动 NVS）。
- 修改固件内中文文案后需重新生成字库再构建：pc-monitor / ai-usage 用 `tools/gen_app_fonts.py`，minimal-checkin 用 `tools/gen_font*.mjs` + `gen_lunar.py`（Node + Python）。
- 三键一致：UP / DOWN 翻页，OK 短按确认/刷新，OK 长按暂停或进入配网（各固件略有差异，见各自章节）。

---

## 1. minimal-checkin — 极简打卡胸牌

把胸牌变成一枚桌面打卡日历：

- 主页：年月/时间 + 大日数字，带**农历**和节假日「休/班」角标；右侧 ✗/√ 胶囊显示今日未完成（红）/已完成（绿）数；底部滚动显示今日打卡记录（事项/状态/时间）。
- 打卡：UP/DN 弹出事项选择窗，OK 确认落库；事项与个人名片在**配网门户**里配置。
- 月历页按月查看打卡位图，单日详情页回看记录。
- 30 秒无操作自动休眠（配网页不休眠），任意键唤醒并恢复休眠前的页面。
- 打卡数据（月位图/事项/名片）全部存设备本地 NVS，**平时离线可用**；WiFi 仅用于 SNTP 对时和扫码配网：屏幕显示二维码，手机扫码直连 `Checkin-XXXX` 热点（密码 `12345678`），门户通常自动弹出，否则打开 `http://192.168.4.1/`（仅 2.4GHz WiFi）。
- 配网门户里还能增删打卡事项（含删除按钮）、修改名片信息。

---

## 2. ai-usage — AI 用量胸牌

完全独立联网的 AI 额度仪表盘，屏幕自动轮播：

1. 火山方舟（Coding Plan 用量，V4 签名查询）
2. 智谱 GLM Coding Plan（zcode，5 小时/每周 Token、月度 MCP）
3. 阿里云百炼 Coding Plan（5 小时/每周/每月三档真实请求额度）
4. SSH 服务器 ×N（CPU、内存、磁盘占用 + Docker 容器列表，长按翻容器页）
5. 设置（刷新间隔 30 秒～30 分钟、重新配网）

- 每屏一页自动轮播：UP/DN 换页、OK 立即刷新、OK 长按暂停/恢复。
- 首次开机自动进配网页：手机扫屏幕二维码直连 `Usage-XXXX` 热点（密码 `12345678`），门户自动弹出（否则打开 `http://192.168.4.1/`），在门户里填 WiFi、各平台凭据、SSH 服务器，并可用「显示设置」勾选隐藏任一平台（隐藏后不再查询）。仅支持 2.4GHz WiFi。
- 额度/资源快用完时进度条自动变红预警；各页面未配置对应服务时显示配置提示，不展示模拟数据。
- 平台凭据的获取方式与安全建议（RAM 子账号等）见 [firmwares/ai-usage/README.md](firmwares/ai-usage/README.md)。

---

## 3. pc-monitor — 电脑状态桌面胸牌

设备通过 USB 数据线连接电脑，实时显示 CPU、内存、磁盘、网络、显卡、进程、端口、火山方舟用量等信息，并可通过 TRAE hooks 在「战况」页监管 TRAE 的命令执行。

```
TRAE hooks ──TCP 127.0.0.1:8765──┐
                                 ▼
设备(ESP32-C3) ◄──USB 串口(VID 303A)──► pc_status_bridge.py（电脑端守护进程）
```

设备是请求发起方：每隔约 1.5 秒发送 `$REQ#`，bridge 应答状态帧；bridge 自动检测端口、断线自动重连，拔插设备无需任何操作。

### 3.1 环境要求

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

### 3.2 安装

```powershell
# 电脑端依赖
pip install pyserial psutil
```

（可选）火山方舟用量页：把 `firmwares/pc-monitor/tools/ark_config.json.example` 复制为同目录的 `ark_config.json`，填入火山引擎的 `access_key` / `secret_key`。改 key 只需编辑该文件，bridge 会自动重新读取，无需重启。

### 3.3 启动服务（电脑端 bridge）

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

### 3.4 构建固件

见「通用：构建与刷机」，把 `<固件名>` 换成 `pc-monitor`。改了固件里的中文文案？先重新生成字库再构建（扫描 `main/*.c` 字符串自动收集汉字，缺字的文案会显示空白）：

```powershell
python tools\gen_app_fonts.py
```

### 3.5 刷机

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

刷完重新启动 bridge：`Start-ScheduledTask -TaskName 'FoloToy PcMon Bridge'`

### 3.6 屏幕显示

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

### 3.7 按键操作

| 按键 | 作用 |
|------|------|
| UP / DOWN | 翻页（循环） |
| OK 短按 | 立即刷新；进程页 = 切换 CPU/内存排序；战况页 = **批准**待审批命令 |
| OK 长按 | 暂停/恢复自动轮询；战况页 = **拒绝**待审批命令 |

### 3.8 TRAE 战况页（可选）

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

### 3.9 远程项目的 hook 配置（可选）

TRAE 的远程项目（SSH 等）的 agent 在远端跑，hooks 也由远端触发——**远端机器需要单独装一份 hook**，事件通过 SSH 反向隧道送回本机 bridge。

**前提**：远端已可 SSH 直连；本机 bridge 已启动。

**1. 本机部署到远端**（一次性，在本机仓库根目录跑）：

```bash
# 在仓库根目录执行；user@host 换成远端 SSH 登录名和地址
bash firmwares/pc-monitor/tools/deploy_remote_hook.sh user@your-remote-host
```

脚本会做三件事：把 `trae_hook.py` 和 `hooks-remote.json` 拷到远端 `~/.trae-hooks/`、备份远端 `~/.trae-cn/hooks.json`、把配置写入。

**2. 本机启动反向隧道**（把远端的 8765 映射回本机的 8765）：

方式一：开机自启（推荐，装一次永久生效；断线自动重连）

```powershell
# 一次性前提：把本机 key 授权到远端（会输一次远端密码）
type $env:USERPROFILE\.ssh\id_rsa.pub | ssh user@your-remote-host "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"

# 注册计划任务（-Remote 填你的远端 SSH 登录名@地址）+ 立即启动
powershell -ExecutionPolicy Bypass -File firmwares\pc-monitor\tools\install_tunnel_autostart.ps1 -Remote user@your-remote-host
Start-ScheduledTask -TaskName 'FoloToy PcMon Tunnel'
```

之后每次登录 Windows 自动后台运行（经 `conhost --headless` 启动，**无控制台窗口**），断线 5 秒自动重连。

方式二：手动 ssh（调试用；挂着的终端不能关，断开即失效）

```powershell
ssh -N -R 8765:127.0.0.1:8765 user@your-remote-host
```

隧道的停止与移除：

```powershell
# 手动关闭（立即断开；下次登录 Windows 仍会自启）
Stop-ScheduledTask -TaskName 'FoloToy PcMon Tunnel'

# 彻底关闭（删除自启任务并清掉残留隧道进程）
powershell -ExecutionPolicy Bypass -File firmwares\pc-monitor\tools\install_tunnel_autostart.ps1 -Remove
```

隧道日志：`%LOCALAPPDATA%\FoloToy\pcmon-tunnel.log`（每次连接/断开都记一行）。

**3. 在远端 TRAE 窗口里启用**：

- 设置 > Hooks：确认 5 条出现在列表，**打开开关**
- 运行方式：**本地自动运行**（沙箱连不了 8765）
- **重启那个窗口**（hooks.json 只在窗口启动时读）

**验证**：远端窗口发条消息，本机 hook 日志 `%LOCALAPPDATA%\FoloToy\trae-hook.log` 会出现一个新的 sid（与本机窗口不同），胸牌战况页状态徽章同步亮起。

**注意事项**：

- 远端只转发事件到隧道，**审批按键回本机**——你在胸牌上按 OK/长按 OK，决策经隧道发回远端 TRAE 放行/拦截
- 多个窗口同时工作时，胸牌显示最活跃的那个；本机 + 远端窗口互不干扰（按 session_id 区分）
- 隧道断了远端 hook 会回退为 TRAE 原生确认弹窗，不影响命令执行
- 远端移除：`ssh user@host 'rm -rf ~/.trae-hooks ~/.trae-cn/hooks.json'`（如需还原，恢复 `.bak` 备份）
