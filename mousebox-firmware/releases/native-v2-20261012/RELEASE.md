# 原生接收器透传 v2 — 2026-10-12

应用文件：`logitech_native_bridge.ino.hex`。

- SHA256：`B727B970F100BAD4A554FAE0DDFD97DB99297818DED12F0C6E71D42D7122D00E`
- Flash：20922 字节；静态 RAM：1883 字节。
- FQBN：`logitech-relay:avr:receiver`；MCU：ATmega32u4 16MHz。
- 配套源码：本目录上两级的 `firmware/logitech_native_bridge` 与 `hardware/logitech-relay/avr`。
- 已使用 AVRDUDE 烧录并自动读回校验通过，只写应用 Flash。
- 非阻塞原生透传、独立 1ms 鼠标调度、无 Serial bool 隐藏延迟、原始报文和非零 XY 计数。
- 板端持续移动约 999–1000 份/秒；Windows 一个完整前台窗口实测 1000.6Hz。不是连续 30 秒前台压力测试结论。

烧录及回滚方法见 [README](../../README.md)，完整接手状态见 [HANDOFF](../../HANDOFF.md)。
