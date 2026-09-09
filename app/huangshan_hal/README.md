# 黄山派硬件接口层

`huangshan_hal.h/.c` 是面向应用的薄封装，不重复实现 SF32LB52 的寄存器
驱动。它调用官方 `vendor_sifli` BSP 已注册的 NuttX 设备节点：

| 模块 | 默认节点 | 公开接口 |
|---|---|---|
| I2C | `/dev/i2c0`、`/dev/i2c1` | `hs_i2c_open/write/read/write_read` |
| ADC | `/dev/adc0`（VBAT）、`/dev/adc1`（GSR/PA28） | `hs_adc_open/read` |
| PWM | `/dev/pwm0` | `hs_pwm_open/set/stop` |
| LCD | `/dev/fb0` | `hs_lcd_open/fill/pixel/flush` |
| BLE H:4 | `/dev/ttyHCI0` | `hs_ble_open/reset/command` |

BLE 只提供 H:4 HCI 传输接口；GAP/GATT 应由 openvela 的 Bluetooth Host 或
framework service 管理。启用 `LVX_USE_HUANGSHAN_HAL` 会同时选择
`CONFIG_UART_BTH4`，从而由官方 SF32LB52 驱动注册 `/dev/ttyHCI0`。不要在
Host 已经打开 `/dev/ttyHCI0` 时再次调用 `hs_ble_reset()`，否则会与 Host
竞争控制器。若只使用 Host/framework，则可以不构建本封装的 BLE demo。

## 使用示例

```c
#include "huangshan_hal.h"

struct hs_i2c_s i2c = { .fd = -1 };
hs_i2c_open(&i2c, 0, HS_I2C_DEFAULT_FREQUENCY);
hs_i2c_write_read(&i2c, 0x38, &reg, 1, &value, 1);
hs_i2c_close(&i2c);

struct hs_pwm_s pwm = { .fd = -1 };
hs_pwm_open(&pwm, NULL);
hs_pwm_set(&pwm, 1000, 50);
hs_pwm_stop(&pwm);
hs_pwm_close(&pwm);
```

## BLE 广播 smoke test

当 Bluetooth Host 没有打开 `/dev/ttyHCI0` 时，可以在 `nsh>` 执行：

```text
huangshan_hal_demo ble_adv HuangshanPi
```

手机应能扫描到 `HuangshanPi`。该命令使用标准 HCI LE Advertising 命令，
只用于验证控制器和天线链路；真正的 GAP/GATT 产品功能应交给 openvela
Bluetooth Host/framework，不能让两者同时占用 HCI 节点。

## 主机脚本

```bash
scripts/build_huangshan.sh
scripts/flash_huangshan.sh /dev/ttyUSB0
python3 -m pip install pyserial
scripts/test_huangshan.py --port /dev/ttyUSB0
```

错误返回值统一为负的 `errno`，例如 `-ENODEV`、`-EIO`、`-EINVAL`。

## Grove GSR 采集

黄山派的 PA28 是 SF32LB52 ADC1 的固定外部输入（软件通道 `0`），同时也是
TF 卡的 SPI1_CLK；本配置会禁用 SPI1/TF 卡，以便注册 `/dev/adc1` 给 GSR。
`/dev/adc0` 继续只读内部 VBAT 通道 `5`。

```text
nsh> huangshan_hal_demo gsr_once
nsh> huangshan_hal_demo gsr_cal 30
CAL_RAW10=... samples=150
nsh> huangshan_hal_demo gsr_stream <上一步的CAL_RAW10>
time_ms,adc_mv,raw10,ema_mv,resistance_ohm,delta_pct,status
...
```

`gsr_cal` 时保持电极悬空并调节 Grove 板电位器；`gsr_stream` 约 5 Hz 连续
输出 CSV，Ctrl+C 停止。`status` 为 `WARMUP`、`OK`、`SATURATED` 或
`CAL_INVALID`。人体电阻和趋势仅供原型调试，不能作为医疗或情绪结论。

接线前先完全断开 USB 与电池：Grove 红线接板底 `3V` 测试焊盘 TP3
(`VCC_3V3_S`)，黑线接 TP6 或 TP7 (`GND`)，黄线经 1 kΩ 串联电阻接 30P
第 21 脚 PA28，白线悬空并绝缘。上电先用万用表确认 TP3 对 GND 约 3.3 V；
禁止接板底 `5V`/`BAT`。保留 30P 11--12 的跳线帽。
