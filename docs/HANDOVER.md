# MoodAnchor 项目交接说明

本页统一描述当前提交的功能范围。硬件接线见 [WIRING.md](WIRING.md)，BLE 字段见 [BLE_PROTOCOL.md](BLE_PROTOCOL.md)，构建与补丁见 [BUILDING.md](BUILDING.md) 和 [PATCHES.md](PATCHES.md)。

## 当前组成

- 黄山派应用：GSR、IMU 读取，LVGL 状态与调试页面，原型规则阈值判定和用户确认弹窗。
- BLE：Event、Data、Status 和 Control 特征，用于事件上报、状态同步及确认。
- Android：扫描连接、事件记录、通知、日历、本地音频设置与陪伴对话。
- 在线对话：本次提交 APK 内置腾讯云 SCF 地址与公开评审访问码，通过后端调用 Coze；已完成手机对话验证，不依赖电脑 SSH 或 AutoDL。模型 Key 仅保留于服务端；在线状态与额度不保证持续可用。安装包见 [artifacts](../artifacts/README.md)。
- 算法：IMU 运动过滤 → EDA 生理确认。端侧串行核心已提供，真实输入适配与主应用接入尚待验证。

算法指标统一见 [ALGORITHM_METRICS.md](ALGORITHM_METRICS.md)。两路公开数据并非同步采集，不报告端到端系统性能。

## 关键文件

| 文件 | 用途 |
| --- | --- |
| `app/huangshan_hal/huangshan_hal.c/.h` | ADC/GSR、IMU、I2C、LCD 等接口 |
| `app/huangshan_hal/mood_anchor_main.c` | 手表页面、采样线程、规则链路、确认与调试入口 |
| `app/huangshan_hal/hs_mood.c/.h` | 当前规则阈值与分数计算 |
| `app/huangshan_hal/hs_ble_host.c`、`hs_ble_gatt.c` | BLE 主机接入与服务 |
| `algorithm/imu_eda/embedded/mood_gate/` | 待接入的 IMU → EDA 串行核心 |
| `android/app/src/main/java/com/moodanchor/app/` | Android 交互、记录、播放与对话 |
| `scripts/`、`patches/` | 构建、烧录和依赖补丁 |

## 事件与演示边界

手表的用户确认结果使用事件类型 `0x07`，误报反馈使用 `0x08`。手机接收结果后更新记录，并按权限和用户设置进行提醒。

KEY2 是模拟调试入口：发送 `0xff` 自检事件并附模拟标志，临时覆盖数据包中的状态。它用于检验通信与界面，不代表真实传感器检测。

Android 模拟入口使用固定分数 `0.74`。该值与展示阈值均为交互演示参数，不是实测概率，也不作为算法评价数据。实际模型概率需要完成手表端部署、校准和验证后上报。

## 音频行为

自动音频默认关闭，需用户主动启用并选择本地文件。自动播放前检查蓝牙耳机连接状态；未检测到耳机则跳过，不主动改用扬声器。播放后的路由由系统决定，当前没有断连后始终保持指定输出的保证。

## 迁移检查

1. 使用完整 openvela 工作区，核对依赖版本和本仓补丁，按构建文档配置应用。
2. 检查芯片层麦克风适配文件及应用开关是否齐全，不将本机未提交的依赖视作自动同步内容。
3. 按 BLE 协议联调扫描、订阅、确认和误报反馈。
4. 校验实际采样周期、单位、接触质量后，再接入串行门控核心。
5. 使用同步自采数据验证效果；当前交互演示不能替代真实佩戴评估。

串口与 BLE 历史故障记录仍保留在 `logs/` 及 BLE 问题文档中，属于调试证据，不是当前功能清单。
