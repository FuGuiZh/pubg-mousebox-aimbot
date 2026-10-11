# 鼠标盒子固件开发

本目录独立管理 Arduino Leonardo + USB Host Shield 的鼠标接收器透传固件、USB 核心、依赖源码、测试、备份和烧录流程。与上级目录的 Visual Studio 项目分离，不依赖 Radar 或 Reader 启动。

当前版本已烧录：移除约 100Hz 的软件瓶颈，板端持续移动实测约 999–1000 份原始报文/秒；Windows **前台** Raw Input 的一个完整约 1 秒窗口测得 **1000.6Hz**。用户确认移动、按键、滚轮及 G HUB 型号、电量正常。长时间前台稳定性尚未做完整验收，不把后台约 125Hz 的合并事件数当作 USB 回报率。

接手请先读 [HANDOFF.md](HANDOFF.md)。

## 硬件连接

```text
鼠标 ↔ 自有 Logitech 接收器 → Host Shield 的 USB-A
                              ↓ MAX3421E / SPI
                         Leonardo / ATmega32u4
                              ↓ 原生 USB 数据线
                        接收输入的电脑
```

Leonardo 原生 USB 接哪台电脑，鼠标输入就送到哪台电脑。红色 USB-TTL 模块不是此版本的报文通道；无需运行 PC 项目来转发物理鼠标。本目录没有串口移动指令、TTL 注入、自动瞄准或游戏联动功能。

当前只支持实测的 **046D:C54D** 接收器直接接 Shield；不能把固件直接用于其他接收器或经过 USB Hub 的链路。

## 文件职责

```text
firmware/logitech_native_bridge/
  logitech_native_bridge.ino   初始化、接口声明、每端点调度
  native_descriptors.h        从真实接收器读取的 HID 描述符
  host_transport.h           MAX3421E 运行期异步传输状态机
  report_pipeline.h          按原始报文尺寸排队、背压与计数
  diagnostics.h              非阻塞诊断输出
hardware/logitech-relay/avr/  独立 Arduino 平台与定制 USB 核心
libraries/                   保留许可证的依赖源码
tools/                       编译、备份、烧录、回报率测量
tests/                       本机 C++ 状态机与报文测试
backups/                     历史 Flash/EEPROM 与可恢复 HEX
logs/                        真实设备描述符和本机测试记录
releases/                    固定版本的应用 HEX
build/                       可重新生成的构建产物
```

使用独立 FQBN `logitech-relay:avr:receiver`。不要用默认 `arduino:avr:leonardo` 构建，也不要直接修改全局 Arduino15 的核心文件。`build.ps1` 只在当前进程临时指定本目录为 Arduino 用户目录，结束后还原。

## 环境

- Windows / 64 位 PowerShell 7；测量程序使用 Windows Raw Input 和 WinForms。
- Arduino CLI（本机由 Arduino IDE 2.3.8 提供）、已安装的 Arduino AVR Boards 1.8.7 及其 AVR 编译器。
- AVRDUDE 8.0.0-arduino1：脚本从 Arduino15 的已安装工具中定位。
- 本机单元测试使用 `g++`（当前为 `C:\mingw64\bin\g++.exe`），不是 AVR 模拟器。

工具可执行文件没有复制进本目录；完整自定义源码及本轮所用库源码已放在这里。迁移到另一台电脑需要安装以上工具，不要把“源码已迁移”误认为“无需工具链”。

## 编译、检查与烧录

在 `G:\pubg-mousebox-aimbot\mousebox-firmware` 下使用 PowerShell：

```powershell
& .\tools\device.ps1 -Action List
& .\tools\build.ps1
& .\tools\test.ps1
& .\tools\verify-descriptors.ps1
& .\tools\device.ps1 -Action Upload -RuntimePort COM12
```

如果 Arduino CLI 没有被自动找到：

```powershell
& .\tools\build.ps1 -ArduinoCliPath 'D:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
```

当前运行端口为 COM12，身份须为 `USB\VID_046D&PID_C54D&MI_03`。脚本在 1200bps 触发现有 bootloader，自动寻找其临时端口（本轮为 COM9，不要硬编码 bootloader 端口）。目标不匹配或 bootloader 不唯一时拒绝烧录。

Upload 默认要求 `backups/backup-native-v1-20261012/flash-before.bin` 这个已留存的 32KiB 备份。需要另建备份时，使用不重复的名字；该操作会短暂重启 USB，但不写 Flash/EEPROM：

```powershell
& .\tools\device.ps1 -Action Backup -RuntimePort COM12 -BackupName backup-before-next-change
```

只写应用 HEX，并自动读回校验；不改 fuse、bootloader 或 EEPROM。**绝不能把完整 32KiB `flash-before.bin` 当应用固件烧录。**

## 回滚

回到用户已确认 G HUB 正常的原生透传 v1（回报率优化之前）：

```powershell
& .\tools\device.ps1 -Action Upload -RuntimePort COM12 -HexPath .\backups\backup-native-v1-20261012\logitech_native_bridge-v1.hex
```

重新烧录本轮固定发布 HEX：

```powershell
& .\tools\device.ps1 -Action Upload -RuntimePort COM12 -HexPath .\releases\native-v2-20261012\logitech_native_bridge.ino.hex
```

如果运行端口消失、设备身份变化，先检查连接并 List；不要猜端口或把身份检查改成匹配所有串口。当前自动脚本不处理“应用完全无法启动、只能手动 reset 进 bootloader”的恢复情形。

## 验证真实回报率

```powershell
& .\tools\device.ps1 -Action Monitor -RuntimePort COM12 -MonitorSeconds 30
& .\tools\measure.ps1 -Seconds 30 -Foreground -LogPath .\logs\raw-new-foreground.log
```

日志名必须是新的，测量工具拒绝覆盖已有文件。连续快速移动鼠标，点击测试窗口并保持其在前台；窗口约 30 秒后自动关闭，也可提前关闭。要同时收集串口计数，请在另一个 PowerShell 中先启动 Monitor，然后再打开前台测量窗口。

Windows 11 对后台 Raw Input 监听器会合并、限频，因此默认无窗口模式只适合验证输入存在，不适合验收高回报率。日志中 `foreground=1` 且 `window_foreground_reports=window_reports` 才说明该窗口的所有记录都在测试窗口为前台时收到；混合前后台、停止移动的秒不能作为稳定回报率证据。

- `rx`：板端收到的原始报文；`moving`：其中非零 XY 位移报文。
- `submit`：交给本机 USB 端点的报文，**不代表已被电脑 ACK**。
- `overflow/invalid/xerr`：队列溢出、长度不符合描述符、传输错误。
- `missed`：板端错过的 1ms 调度槽；不是自动计算出的丢包数。
- 打开串口后的第一组计数通常跨越启动或关闭串口的历史区间，不用于每秒频率统计。
- Raw Input 统计的是 Windows 提供给这个程序的输入记录，不是 USB 总线分析仪的逐包时间戳。

## 设计边界

报文保持原生尺寸和字节顺序，不拆位移、不合并 XY、不补造 1000Hz 空报文。鼠标报文 13 字节；HID++ 支持 ID 0x10 的 7 字节和 ID 0x11 的 20 字节；键盘/媒体报文遵循已读取的描述符。三个 HID 报告描述符逐字节保持不变，仅将鼠标输出端点的 `bInterval` 改为 1ms；恢复用 CDC 接口额外存在，因此不是整个 USB 设备描述符完全一致的克隆。

运行期中断输入、原生 HID 控制请求和输出队列采用合作式非阻塞处理。USB 枚举及故障恢复仍会调用库里的同步路径。有限 SRAM 决定队列必须有限；队列满时暂停新轮询，不覆盖已排队的按键/移动，但不能据此保证上游接收器永远不合并未读输入。

ATmega32u4 和 MAX3421E 都是 USB Full Speed；本版本的单鼠标中断端点目标为 1ms / 1000Hz，不承诺 G HUB 选择 2000Hz 后盒子能输出 2000Hz。[芯片资料](https://www.analog.com/en/products/max3421e.html)、[Windows 端点周期说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbspec/ns-usbspec-_usb_endpoint_descriptor)。

参考：[Windows 后台鼠标限频说明](https://blogs.windows.com/windowsdeveloper/2023/05/26/delivering-delightful-performance-for-more-than-one-billion-users-worldwide/)、[Raw Input 正确读取模式](https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input)。
