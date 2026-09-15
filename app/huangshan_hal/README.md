# 黄山派硬件接口层

`huangshan_hal.h/.c` 是面向应用的薄封装，不重复实现 SF32LB52 的寄存器
驱动。它调用官方 `vendor_sifli` BSP 已注册的 NuttX 设备节点：

| 模块 | 默认节点 | 公开接口 |
|---|---|---|
| I2C | `/dev/i2c0`、`/dev/i2c1` | `hs_i2c_open/write/read/write_read` |
| ADC | `/dev/adc0`（VBAT）、`/dev/adc1`（GSR/PA28） | `hs_adc_open/read`、`hs_gsr_open/read` |
| PWM | `/dev/pwm0` | `hs_pwm_open/set/stop` |
| LCD | `/dev/fb0` | `hs_lcd_open/fill/pixel/flush` |
| BLE H:4 | `/dev/ttyHCI0` | `hs_ble_open/reset/command` |
| 板载麦克风 | `/dev/audio/pcm0c`（需 AUDCODEC 音频后端） | `hs_mic_open/read/close` |

## IMU 和麦克风

`hs_imu_open/read/close` 使用官方 `/dev/lsm6dsl0` 驱动，返回加速度（mg）、
角速度（mdps）、温度和时间戳。命令行可用：

```text
nsh> huangshan_hal_demo imu
```

黄山派的麦克风是模拟 MEMS MIC，信号走芯片内部 AUDCODEC ADC（模组 36 脚
`MIC_BIAS`、37 脚 `MIC_ADC_IN`），不是 Grove/GPADC 通道，也没有从 30P
排针引出。`hs_mic_*` 接口和 `mic_once`/`mic_stream` 命令已经提供标准
16-bit 单声道 PCM 入口；当前裁剪版 defconfig 尚未启用 AUDCODEC 音频设备，
因此命令会明确返回 `ENODEV`，不会伪造麦克风数据或修改现有 ADC/GSR 通路。
启用板级 AUDCODEC DMA/PCM 注册后，上层无需改变接口。

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

### 给上层应用的读取接口

上层只需要请求采样数据时，使用 `hs_gsr_s` 的同步接口即可。每个调用者
独立打开/关闭设备，`hs_gsr_read()` 每次触发一次 ADC 转换并返回输入电压和
归一化的 10 位原始值；接口不会创建后台线程，也不保存校准或情绪判断状态。

```c
#include "huangshan_hal.h"

struct hs_gsr_s gsr;
struct hs_gsr_sample_s sample;
int ret = hs_gsr_open(&gsr);
if (ret == 0)
  {
    ret = hs_gsr_read(&gsr, &sample);
    if (ret == 0)
      {
        /* sample.adc_mv: millivolts, sample.raw10: 0..1023 */
        printf("GSR %ld mV (raw=%u)\\n", (long)sample.adc_mv,
               sample.raw10);
    }
  }
hs_gsr_close(&gsr);
```

采样周期、滤波、校准和上层业务含义由调用者自行决定。`hs_gsr_read()` 返回
负的 `errno`（如 `-ENODEV`、`-EIO`、`-EINVAL`）；未连接传感器时不会阻塞
系统，可直接按错误处理。

## KEY2 调试按键（模拟情绪变化）

板载 KEY2（`PA_43`）在开机自动运行的 `mood_anchor` 里是调试触发键：按一下
翻转一次融合判定（平静 ⇄ 激动），并在 BLE 事件特征（`d38a0002`）上发一包
`HS_BLE_EV_MOOD_CHANGE (0x07)` 事件，flags 带 `SIMULATED | ACK_REQ`。
同一份模拟判定会接管数据包的情绪字节 10 秒，让手机端看到与事件一致的变化。

LINK 页会显示 `sim mood #N ...`，串口打印 `[sim] mood change #N: ...`，
手机端 App 会显示"收到手表事件"（0x07 目前显示为"未知事件（0x7）"，接收端
加一行映射即可显示为"情绪变化"）。

PA20（原马达驱动）本工程设计不再使用，固件不再驱动它。

接线前先完全断开 USB 与电池。30P 排针表中 11 脚是 `VCC_3V3_S` 电源
输出、12 脚是 `VCC_3V3`，保持 11--12 跳线帽连接后，Grove 红线接 11 脚
（也可接已短接的 12 脚）；黑线接 3/4/9/10 任一 `GND`；黄线经 1 kΩ
串联电阻接 21 脚 `PA28`；白线悬空并绝缘。上电前用万用表确认 11 脚对
GND 约 3.3 V，禁止接 1/2 脚 USB 5V 或 5/6 脚 BAT。

## 已移除的模块

心率 / 血氧（MAX30102 + `hs_ppg.c`）和振动输出（`hs_vibration_*`）已经从
本工程删除：外接传感器只剩 GSR 一路，PA20 不再驱动马达。

BLE 数据/状态包里的**心率、血氧字节偏移保持不变**（接收端 App 按固定偏移
读取），但有效位永远清零、数值恒为 0，手机端显示为"无读数"。
