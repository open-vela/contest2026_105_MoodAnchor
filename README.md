# MoodAnchor（是非钟）

是非钟定位为“智能情绪管理与人际冲突无感干预”的可穿戴产品概念。我们关注的不是对人的情绪下诊断，也不是在事后复盘对错；而是在争执、压力或冲动决策即将升级的临界前数秒，给用户一个低打扰、体面且可自主选择的暂停机会。

> 本项目用于竞赛原型和交互演示，不提供医疗诊断或心理治疗结论。

## 产品愿景：在情绪临界点，给人一个暂停键

> “心有是非，则钟声自起。它不辨人间对错，只在心火欲燃时，赠你三秒清凉。”

### 场景痛点：现代人的“理智真空期”

在职场争执、家庭琐事或高压社交中，人可能在极短时间内说出伤人的话、做出冲动决定。传统设备多在事后记录，或以突兀的提示要求用户“深呼吸”，容易在情绪当下引发抵触。是非钟尝试以更温和的外部线索打断循环：不评判、不说教，只提醒用户“此刻可以停一下”。

### 三大功能目标

1. **无感实时觉察**：规划通过 IMU 动作、皮电（GSR/EDA）、环境响度，以及后续可扩展的心率/HRV 等多源信号，形成端侧的疑似情绪激化提示；不使用语义识别来判断对话内容。
2. **阻断冲动危机**：在疑似事件出现时，按场景选择低打扰的模拟来电震动、耳机舒缓音频或手机提醒，让用户获得短暂的注意力转移与离场理由。
3. **给予情绪出口**：手机 App 在事后提供 CBT 风格的陪伴对话、情绪记录与自我反思入口，帮助用户理解事件、想法、身体反应和行为之间的关系。

### 分级防御干预策略（产品规划）

| 级别 | 适用设想 | 干预方式 | 设计目的 |
| --- | --- | --- | --- |
| 初级：声东击西 | 未佩戴耳机或处于公开社交场景 | 模拟来电节奏的轻微震动与界面提示 | 以熟悉的“来电”线索唤回注意力，为用户提供体面暂停的理由 |
| 高级：唤醒转移 | 用户佩戴蓝牙耳机且适合私密干预 | 低音量播放白噪音、舒缓音频或经授权的亲友留言 | 以温和声音线索帮助用户从冲突现场抽离注意力 |
| App 陪伴 | 事件后或用户主动开启时 | 通知、记录、CBT 风格陪伴对话与小行动建议 | 把短暂的暂停延展为可回看的情绪调节过程 |

上述震动模式、耳机音频与亲友留言是产品交互愿景；是否启用始终由用户授权与场景设置决定，不作为医疗干预或强制行为控制。

### 隐私原则

- 以端侧特征和事件为优先，不将连续原始生理信号作为产品默认上传内容。
- 声音相关功能只规划使用响度、频段等非语义特征；不分析谈话语义、不将原始录音作为默认数据采集内容。
- 模型服务密钥仅存在于受控后端，Android APK 不包含真实 API Key。

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
| `backend/` | **云端对话服务中转站**：手机把用户主动发起的陪伴对话发送到这里；它再调用部署者配置的大语言模型，并把回答返回手机。模型密钥只放在服务器，不进入 APK | 否，可部署在个人电脑、云服务器或比赛演示主机 |
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

## 演示视频

| 演示内容 | 对应视频 |
| --- | --- |
| 黄山派手表与 Android 手机 BLE/GATT 真实连接和状态订阅 | [完整演示视频（主视频，Git LFS 文件导航）](videos/01_watch_phone_ble_connection.mp4) |
| 手机连接手表并确认状态 | 见下方“设备配对演示”播放器 |
| 手表确认事件后，Android 状态栏提示 | 见下方“事件通知演示”播放器 |
| Android App 首页和手表连接入口 | 见下方“App 首页演示”播放器 |
| Android App 的事件触发与交互 | 见下方“App 事件流程演示”播放器 |
| 情绪日历、铃声和设备状态设置 | 见下方“日历与音频设置演示”播放器 |

### 可直接播放的分段演示

#### 设备配对演示

https://github.com/user-attachments/assets/e2f21015-0fa6-4112-af10-c110c8390606

#### 事件通知演示

https://github.com/user-attachments/assets/865346d2-2e24-44c7-a382-379679ea1128

#### App 首页演示

https://github.com/user-attachments/assets/f6358fc4-588d-48b1-95fe-a7ebce02dab6

#### App 事件流程演示

https://github.com/user-attachments/assets/c4ca6f70-2def-453b-b568-aeb5bdd7c8c5

#### 日历与音频设置演示

https://github.com/user-attachments/assets/7daae2d2-b47a-4a51-96dd-6ee684ebf6d6

各视频的功能说明和当前演示边界见 [videos/README.md](videos/README.md)。

### 后端与局域网演示

`backend/` 可以理解为 App 与云端大语言模型之间的“安全接线员”：App 不直接保存或调用模型平台的密钥，而是向你们部署的服务发起对话请求；服务再调用 Coze 或 SiliconFlow/Qwen，并把结果回传。演示时它可运行在电脑或云服务器上；正式部署应使用 HTTPS、访问控制与限流。

代码和配置模板可以完全开源，**真实 API Key 不可以开源**：一旦出现在 GitHub、APK、截图或视频里，任何人都能消耗账户额度、读取对应服务资源，且撤销前无法收回。仓库中的 `backend/.env.example` 只保留变量名与占位符；每位部署者在自己的服务器环境变量中填写自己的 Key。

Windows PowerShell 演示部署示例：

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
