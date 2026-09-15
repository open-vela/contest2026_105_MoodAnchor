# MoodAnchor（是非钟）

MoodAnchor（是非钟）是面向黄山派开发板与 Android 手机的智能情绪陪伴原型：手表侧采集皮电（GSR/EDA）、运动（IMU）及可选环境响度等非语义信号，通过 BLE 上报需要关注的事件；手机侧提供事件记录、提醒与陪伴对话，帮助用户在高压沟通或冲突升级前获得短暂、温和的暂停与转移。

> 本项目用于竞赛原型和交互演示，不提供医疗诊断或心理治疗结论。

## 产品概述

### 应用场景

- **高压沟通降温**：在工作争执、家庭摩擦等情境中，以手表端事件提醒和手机端陪伴对话引导用户短暂抽离、呼吸或延后回应。
- **体面暂停**：用户可基于手表提醒或手机通知选择离开现场、联系可信赖的人或开启舒缓内容，避免强制说教式干预。
- **日常情绪记录**：在 Android App 中回看事件与自我记录，辅助识别压力情境和可执行的应对方式。

### 核心体验与创新点

- **端侧优先、低打扰提醒**：事件判断与 BLE 上报优先在手表侧完成，手机在收到确认事件后再触发通知和陪伴流程。
- **IMU → EDA 串行门控原型**：先以 IMU 识别剧烈运动干扰，再允许 EDA 信号确认事件，避免将明显的运动状态直接当作生理唤醒；确认后设置冷却时间，减少重复提醒。
- **隐私最小化设计**：手机端不需要接收原始 IMU/EDA 连续流；后端密钥仅保存在受控部署环境，APK 不包含模型 API Key。麦克风接口仅用于相对响度等非语义特征的探索，不上传或保存原始音频。
- **端云协同陪伴**：手表负责即时事件触发，Android 负责记录、通知和交互；后端仅在用户发起陪伴对话时提供模型代理。

## openvela 系统能力运用

- 基于 openvela / Apache NuttX 与官方 `vendor_sifli` 黄山派 BSP，封装 ADC/GSR、LSM6DSL IMU、LCD、BLE H:4/GATT 等设备接口。
- 使用 openvela Bluetooth Host/GATT 路径实现手表到 Android 的事件通知；应用不与 Host 争抢 HCI 设备。
- 使用 LVGL 构建手表端状态与调试界面，并提供 GSR 校准、IMU 读取、BLE 广播等验证入口。
- 通过 `scripts/`、`patches/` 与 `openvela.xml` 固化同步、构建、烧录和 BSP 依赖，保证评审可复现。

## 硬件适配说明

本项目**未新建硬件平台或重写底层芯片驱动**，而是在官方黄山派 SF32LB52 BSP 上完成应用层接口和功能集成。当前已覆盖 GSR ADC、IMU、BLE、显示等接口。板载麦克风接口保留，但裁剪版配置未保证音频后端可用；当前不将其作为已完成的情绪判定能力。未实际接入和验证的传感器或执行器不作为当前作品能力宣称。

## 目标受众

| 维度 | 描述 |
| --- | --- |
| 目标人群 | 有高压沟通、日常压力管理或情绪记录需求的成年用户 |
| 年龄范围 | 建议以 18 岁及以上成年用户为原型设计对象 |
| 使用场景 | 都市职场、家庭沟通、社交压力与个人情绪记录 |
| 使用偏好 | 重视隐私、希望获得温和提醒和自主选择，而非诊断或强制干预 |

## 项目亮点

- 黄山派端完成 GSR、IMU、屏幕、BLE 等硬件接口与事件上报闭环。
- Android App 可接收手表 BLE 事件、显示记录、通知提醒并进入陪伴对话。
- 后端将模型访问密钥保留在部署环境，Android APK 不含模型 API Key。
- 提供 IMU+EDA 训练、评估、个体化流程与端侧 C 模型导出材料。

## 目录结构

| 路径 | 内容 | 是否参与 openvela 自动映射 |
| --- | --- | --- |
| `app/huangshan_hal/` | 黄山派手表端 C 代码：硬件抽象、GSR/IMU、BLE、事件主程序 | 是，映射到 `packages/demos/contest2026_105_huangshan_hal` |
| `android/` | Android App 源码、Gradle wrapper 和资源 | 否，独立 Android Studio / Gradle 工程 |
| `backend/` | Python 模型代理与脱敏环境变量模板 | 否，部署在受控服务器或演示主机 |
| `algorithm/imu_eda/` | IMU+EDA 训练、评估、端侧模型、参数和指标摘要；不含原始数据集 | 否 |
| `artifacts/` | 可供评审安装的演示 APK | 否 |
| `tools/` | Windows 局域网演示、端口转发及防火墙脚本 | 否 |
| `scripts/` | 黄山派同步、准备、编译、烧录、抓包与诊断脚本 | 否，供构建流程调用 |
| `patches/` | openvela/NuttX/vendor_sifli 依赖补丁及说明 | 否，按构建文档应用 |
| `docs/` | BLE 协议、接线、构建、补丁、问题排查、交接和手表端实现文档 | 否 |
| `logs/` | AI Coding 对话日志，按比赛要求提交 | 否 |
| `board/contest_board/` | 比赛模板中的板级适配骨架；本项目黄山派使用官方 `vendor_sifli` BSP | 是，但当前不是作品主实现 |
| `quickapp/hello_quickapp/` | 比赛模板快应用示例，当前不是作品主实现 | 是，但当前未使用 |
| `contest2026_105_MoodAnchor.xml` | 参赛仓与 openvela 工作区的 linkfile 映射 | 是 |
| `openvela.xml` | repo 同步清单，声明 openvela 与黄山派 BSP 依赖 | 是 |

## 当前实现状态

手表端默认采用已在演示链路中验证的原型阈值事件逻辑，以保障采集、BLE 上报和手机协同稳定。`algorithm/imu_eda/` 已提交 IMU+EDA 融合模型的训练、评估、端侧 C 模型和运动门控参数；在完成黄山派真实采样的采样率、窗口、特征顺序与单位对齐验证前，它不会被描述为默认的板端情绪识别实现。

端侧模型接入与校准说明见 [algorithm/imu_eda/README.md](algorithm/imu_eda/README.md) 和 [docs/WEARABLE_IMPLEMENTATION.md](docs/WEARABLE_IMPLEMENTATION.md)。

## 快速开始

### 黄山派手表端

在完整 openvela 工作区中同步依赖后，从本参赛仓目录执行：

```bash
OPENVELA_ROOT=/path/to/openvela scripts/build_huangshan.sh
```

烧录、串口验证、GSR 接线和补丁要求请阅读 [docs/BUILDING.md](docs/BUILDING.md)、[docs/WIRING.md](docs/WIRING.md) 与 [docs/PATCHES.md](docs/PATCHES.md)。

### Android App

使用 Android Studio 打开 `android/`，或在该目录执行：

```powershell
.\gradlew.bat :app:assembleDebug
```

演示 APK 位于 `artifacts/MoodAnchor-debug-20260915.apk`。`local.properties` 是本机 SDK 配置，已被忽略，需由每位开发者自行创建。

### 后端与局域网演示

在部署主机通过环境变量设置模型凭据；可参考 `backend/.env.example` 的变量名称，但不要提交真实值。Windows PowerShell 示例：

```powershell
$env:COZE_API_KEY = "<masked>"
$env:COZE_BOT_ID = "<masked>"
python backend/server.py
```

电脑局域网端口开放和 SSH 转发说明见 [tools/README-LAN-DEMO.txt](tools/README-LAN-DEMO.txt)。

## 文档索引

- [BLE 协议](docs/BLE_PROTOCOL.md)
- [BLE 交接与当前状态](docs/BLE_HANDOVER.md)
- [BLE 问题报告](docs/BLE_ISSUE_REPORT.md)
- [接线说明](docs/WIRING.md)
- [构建与烧录](docs/BUILDING.md)
- [补丁清单](docs/PATCHES.md)
- [手表端实现说明](docs/WEARABLE_IMPLEMENTATION.md)
- [IMU+EDA 模型指标与边界](docs/ALGORITHM_METRICS.md)
- [项目交接说明](docs/HANDOVER.md)

## AI Coding 使用说明

项目在需求拆解、硬件接口梳理、BLE 协议调试、Android 应用、后端代理、算法原型与文档编写中使用 AI 辅助。可提交的完整对话记录位于 `logs/`，不应被 `.gitignore` 排除。最终提交前应以实际日志统计补充 AI 工具、MCP/Skills 使用情况、代码占比与 Token 用量；不对尚未采集的会话数据做估计或虚构。
