# 黄山派 IMU → EDA 串行确认模块

这是一个可编译进 **OpenVela / SF32LB52 黄山派** 固件的 C99 内置应用：

```
100 Hz 六轴 IMU → 2 s 窗口、1 s 步长 → 高运动概率
                                    │
                         p(高运动) < 0.22
                                    │
4 Hz 皮电（uS） → 5 s 窗口、1 s 步长 → p(EDA 阳性) ≥ 0.68 → 确认事件
```

它不是 65/35 加权概率融合。逻辑是严格串行：IMU 判为剧烈运动时，EDA 不作确认；IMU
通过时才允许 EDA 的阳性结果直接确认一次事件。确认后默认静默 30 秒，防止同一段波动连续上报。

## 冻结参数与边界

- `mood_gate_model_params.c` 仅从 `E:\WESAD\1` 的新实验模型导出：IMU 为 PAMAP2
  S101--S106 源受试者模型，EDA 为 WESAD S2--S9 源受试者模型。
- 两个数据集没有同步采集的同一批人/同一时段 IMU+EDA，故**不能**把两个单模态结果相乘或宣称
  已得到端到端串行系统的准确率。板端部署是工程原型，最终必须用自采同步数据做盲测。
- 首次佩戴必须保持安静约 2 分钟。模块以每秒一组特征存 120 组，用中位数/MAD 建立个人基线；
  这与本轮严谨训练的个体归一化方法一致。
- IMU 输入单位应是加速度 `m/s²` 与角速度 `rad/s`；EDA 输入必须是经过模拟前端换算的皮电导 `uS`，
  不能把 ADC 原始码直接喂进模型。

## 真实硬件必须补齐的两处

黄山派 OpenVela 板级适配已有 `/dev/adc0`、I²C 和 BLE，但当前板级 README 并没有提供可直接读取
六轴 IMU 的 `/dev/imu0` 标准节点，也没有皮电传感器。故 `mood_gate_platform.c` 刻意返回
`-ENOSYS`，防止误把假数据用于演示。

在使用前，按你的实际元件替换两个函数：

1. `mood_gate_platform_read_imu()`：接入实际六轴 IMU 的 I²C/SPI 驱动，每 10 ms 返回一次
   `(ax, ay, az, gx, gy, gz)`。
2. `mood_gate_platform_read_eda()`：外部皮电模拟前端 → ADC，先完成 ADC 码到 `uS` 的标定，**每
   250 ms 成功返回一次**；其他 100 Hz 轮询时返回非零，避免误采样成 25 Hz。

板上有 ADC0 不等于已具备皮电；EDA 电极、恒压/恒流模拟前端与安全限流仍需你们的硬件同学接线并确认。

确认回调 `mood_gate_platform_on_confirm()` 中可接震动、屏幕提示或 BLE 自定义 GATT 通知。

## Linux/OpenVela 构建与烧录

以下命令针对本仓库已经包含的 `dev-ai-contest-2026` 黄山派适配。先把本目录放在
`apps/examples/mood_gate/`；当前项目中已放好。

```bash
cd /path/to/MoodAnchor

# 1. 配置黄山派 OpenVela 固件
cmake -B cmake_out/lckfb_huangshan_pi -S "$PWD/nuttx" -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"

# 2. 开启内置应用；在菜单中选 Examples → MoodAnchor IMU + EDA serial gate
cd cmake_out/lckfb_huangshan_pi
ninja menuconfig
# 设 CONFIG_EXAMPLES_MOOD_GATE=y
# 设 CONFIG_EXAMPLES_MOOD_GATE_EDA_ADC_CHANNEL=<实际皮电 ADC 通道>
ninja savedefconfig

# 3. 编译镜像
ninja
```

产物为 `cmake_out/lckfb_huangshan_pi/nuttx.bin`。使用 CH340N 串口下载，Linux 示例：

```bash
sftool -c SF32LB52 -p /dev/ttyUSB0 -b 1000000 \
  --before default_reset --after soft_reset \
  write_flash cmake_out/lckfb_huangshan_pi/nuttx.bin@0x12010000

# 查看 1 Mbps 控制台；务必避免 RTS 一直拉低复位板子
picocom -b 1000000 --noreset --lower-rts --lower-dtr /dev/ttyUSB0
```

在 `nsh>` 中运行：

```text
mood_gate
```

正常接入真实传感器前会看到 `cal=0`；安静完成约 2 分钟后变为 `cal=1`。任意 IMU/EDA
读取函数仍为默认 `-ENOSYS` 时，不应把此固件当作传感器演示成功。

## BLE 上报建议

保持现有 OpenVela 蓝牙栈，不要自己访问 HCI。定义一个自定义 GATT 服务，事件特征可只通知：

```
byte 0:  0x01 = confirmed_event
byte 1-4: monotonic timestamp (little endian)
byte 5:  EDA probability × 100
byte 6:  IMU high-motion probability × 100
```

手机订阅 notification 后收到 `0x01`，映射为现有 Android 的「疑似事件确认」通知。手机端不应接收
原始 EDA/IMU 流，除非另做用户授权和数据保护设计。
