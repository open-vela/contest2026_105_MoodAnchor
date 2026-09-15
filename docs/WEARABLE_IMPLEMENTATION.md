# 黄山派 SF32LB52 硬件接口与驱动示例

本作品以立创·黄山派（SiFli SF32LB52）为目标板，使用 openvela 官方
`vendor_sifli` 的 `dev-ai-contest-2026` BSP，并在 `app/huangshan_hal/`
提供统一的 I2C、ADC、PWM、LCD 和 BLE H:4 接口函数以及 `huangshan_hal_demo`
命令。底层芯片寄存器、时钟、Pinmux、LCD CO5300、FT6146 和蓝牙控制器驱动
来自官方 BSP，不在本仓库复制或改写。

## 接口位置

```text
app/huangshan_hal/huangshan_hal.h  # 应用接口声明
app/huangshan_hal/huangshan_hal.c  # NuttX 设备节点封装
app/huangshan_hal/huangshan_hal_demo.c
```

默认设备节点为 `/dev/i2c0`、`/dev/i2c1`、`/dev/adc0`（VBAT）、`/dev/adc1`
（Grove GSR/PA28）、`/dev/pwm0`、
`/dev/fb0`；启用 `LVX_USE_HUANGSHAN_HAL` 后，Kconfig 会打开官方 H:4
伪设备并提供 `/dev/ttyHCI0`。所有接口失败时返回负的 errno 值。

| 模块 | 主要函数 |
|---|---|
| I2C | `hs_i2c_open`、`hs_i2c_write`、`hs_i2c_read`、`hs_i2c_write_read` |
| ADC | `hs_adc_open`、`hs_adc_read` |
| PWM | `hs_pwm_open`、`hs_pwm_set`、`hs_pwm_stop` |
| LCD | `hs_lcd_open`、`hs_lcd_pixel`、`hs_lcd_fill`、`hs_lcd_flush` |
| BLE H:4 | `hs_ble_open`、`hs_ble_reset`、`hs_ble_command` |

BLE 的 `hs_ble_*` 是控制器 H:4 传输接口；完整 GAP/GATT 应交给 openvela
Bluetooth Host/framework。若 Host 已启用并占用 `/dev/ttyHCI0`，应用不要再
直接打开该节点，应使用 Host 的 GATT/GAP API。

## 官方 BSP 依赖

黄山派适配位于：

```text
open-vela/vendor_sifli
分支：dev-ai-contest-2026
目录：boards/sf32lb52/lckfb_huangshan_pi
```

`openvela.xml` 已声明 `vendor/sifli` 项目。`nuttx` 与 `vendor_sifli` 必须
同时使用 `dev-ai-contest-2026` 分支。

## 编译

推荐使用仓内脚本完成准备和编译（默认走已验证的 Make 入口，
产物为 `nuttx/nuttx.bin`）：

```bash
OPENVELA_ROOT=/path/to/openvela scripts/build_huangshan.sh
```

脚本会先运行 `scripts/prepare_huangshan.sh`，补齐 Make 构建兼容层、
板级源文件、LittleFS 和 defconfig。若要使用 CMake 构建，设置
`HS_USE_CMAKE=1`，或在 openvela 工作区根目录手动执行：

```bash
cmake -B cmake_out/lckfb_huangshan_pi \
  -S "$PWD/nuttx" -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"
cmake --build cmake_out/lckfb_huangshan_pi
```

需要在 `menuconfig` 中启用 `LVX_USE_HUANGSHAN_HAL` 才会编译演示命令
（`prepare_huangshan.sh` 已默认启用）。

官方黄山派配置已经启用 ADC、I2C、PWM、LCD/FB 和 Bluetooth HCI。应用
映射由本仓 manifest 自动完成：

```text
app/huangshan_hal/
  -> packages/demos/contest2026_105_huangshan_hal
```

## 烧录与验证

Make 构建生成的镜像为 `nuttx/nuttx.bin`（CMake 构建产物在
`cmake_out/lckfb_huangshan_pi/nuttx.bin`），烧录偏移为 `0x12010000`：

```bash
sftool -c SF32LB52 -p /dev/ttyUSB0 -b 1000000 \
  --before default_reset --after soft_reset \
  write_flash nuttx/nuttx.bin@0x12010000
```

串口使用 1,000,000 8N1；推荐：

```bash
picocom -b 1000000 --noreset --lower-rts --lower-dtr /dev/ttyUSB0
```

在 `nsh>` 中：

```text
ls /dev
huangshan_hal_demo i2c
huangshan_hal_demo adc
huangshan_hal_demo gsr_once
huangshan_hal_demo gsr_cal 30
huangshan_hal_demo gsr_stream <CAL_RAW10>
huangshan_hal_demo pwm
huangshan_hal_demo lcd
huangshan_hal_demo ble
huangshan_hal_demo ble_adv HuangshanPi
```

I2C demo 访问触摸 FT6146（I2C0/0x38），ADC demo 读取通道 0，PWM demo
在 `/dev/pwm0` 输出 1 kHz/50%，LCD demo 将 CO5300 framebuffer 填蓝，BLE
demo 对 HCI 控制器发送 Reset，`ble_adv` 还会启用可被手机扫描的 BLE 广播。
这两个命令只能在 Bluetooth Host 未占用 `/dev/ttyHCI0` 时使用；生产应用应
使用 openvela Bluetooth Host/framework 创建 GATT 服务。具体接线、GPIO 和已知限制以官方
`vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/README_zh-cn.md` 为准。

---

以下是比赛模板原始说明，保留用于仓库同步和提交规范。

👋 欢迎参加 **2026 首届 openvela AI 硬件开发者大赛**！

这是组委会为你的队伍创建的**专属参赛仓库**（本仓为样例/模板，队伍编号 `105`；你看到的将是你自己的 `contest2026_<编号>_<队伍名>`）。比赛期间，你的全部参赛代码、打包产物与 AI Coding 日志都提交到这里。

> 本仓既是「代码仓」，又内置了一键拉取整套 openvela 工程的 `repo` 清单（manifest）。你只需跟它打交道，**自始至终只动一个文件夹**。

---

## 一、先读这些官方文档

**通用（所有赛道必读）：**

| 文档                                                                                                                                     | 用途                                           |
| ---------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------- |
| [《大赛总览》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)                        | 赛道、流程、评分、资源，建议先通读             |
| [《参赛代码提交指南》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md)           | 仓库获取、提交流程、时间与权限（**以此为准**） |
| [《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md) | 如何导出 AI 对话日志并提交到 `logs/`           |

**按你的赛道选读（三选一）：**

| 赛道                  | 教程导航                                                                                                                                                 |
| --------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 快应用 / 手表应用创新 | [快应用教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/quickapp/quickapp_guide_index.md)                         |
| AI 硬件产品创新       | [AI 硬件赛道教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_guide_index.md)              |
| 新硬件适配            | [新硬件适配赛道教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/hardware_porting/hardware_porting_guide_index.md) |

---

## 二、第一步：拉取完整工程

用组委会提供的命令一键拉取「openvela 全量源码 + 你的专属仓」：

```bash
repo init -u <组委会提供的 manifest 仓库地址> \
  -b dev-ai-contest-2026 -m contest2026_105_MoodAnchor.xml
repo sync -c -j8
```

官网文档中的 `<manifest 仓库地址>` 是每支队伍专属的地址，不一定等于本
仓库的 GitHub fork 地址。拿到该地址后，也可以直接运行：

```bash
scripts/sync_openvela.sh <manifest-url> contest2026_105_MoodAnchor.xml /path/to/openvela
```

完整 repo sync 后运行 `scripts/prepare_huangshan.sh`，即可把 `nuttx/`、`apps/`、`vendor/` 等源码准备到可编译状态；官方源仓和 LittleFS 下载需要网络，脚本会在下载失败时明确报错。

同步后，你的整个仓库位于工作区的 `contest2026_105_MoodAnchor/`，openvela 全量源码在外层（`nuttx/`、`apps/`、`packages/`、`vendor/` 等）。

---

## 三、第二步：在哪里写代码

**只在自己的仓目录 `contest2026_105_MoodAnchor/` 里开发。** 不同作品形态放在对应子目录，manifest 会通过 `<linkfile>` 把它们**软链**到 openvela 编译树该在的位置——你不用手动 copy：

| 作品形态 | 你的代码放这里             | 系统自动映射到                                 |
| -------- | -------------------------- | ---------------------------------------------- |
| 应用     | `app/hello_app/`           | `packages/demos/contest2026_105_hello_app`     |
| 快应用   | `quickapp/hello_quickapp/` | `packages/apps/contest2026_105_hello_quickapp` |
| 板级适配 | `board/contest_board/`     | `vendor/openvela/boards/contest2026_105_board` |

> 用不到的形态目录可以删掉；新增作品时按同样规则加子目录，并在 `contest2026_105_MoodAnchor.xml` 里补一条 `<linkfile>` 映射即可。**生产仓库（packages/nuttx/vendor 等）零改动。**

建议仓库目录约定（便于评委定位）：

```text
app/ | quickapp/ | board/   # 你的作品代码
logs/                       # AI Coding 日志（主动导出后提交，格式见 logs/README.md）
README.md                   # 作品说明（提交前请改成你自己的，见第六节）
```

> 仓内附带了一个 `.gitignore.example`，给出了**编译产物**等不需要进仓的文件示例。如需启用，`cp .gitignore.example .gitignore` 后按需增删即可。**注意 `logs/` 下最终导出的 AI Coding 日志必须提交，不要忽略。**
>
> `logs/` 的目录结构与提交格式见 [logs/README.md](logs/README.md)。

---

## 四、第三步：编译与运行

编译/运行步骤随作品形态不同而不同，请参考你所在赛道的教程导航：

- 快应用 / 手表应用：[快应用教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/quickapp/quickapp_guide_index.md)（含模拟器与开发板部署）。
- AI 硬件产品创新：[AI 硬件赛道教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_guide_index.md)（环境搭建、编译烧录、Skill 开发）。
- 新硬件适配：[新硬件适配赛道教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/hardware_porting/hardware_porting_guide_index.md)（BSP 移植、最小 NSH 基线）。

子目录已通过 manifest 中的 `<linkfile>` 软链进 openvela 编译树，因此构建在 openvela 工作区**根目录**（即你这个仓的上一级）进行。openvela 使用 `build.sh` 作为统一入口，接收一个 **board config 路径**作为参数：

```bash
# 进入 openvela 工作区根目录（你的仓的上一级）
cd ..

# 通用语法：第一个参数是 board config 路径，第二个参数可以是 menuconfig / distclean 等
./build.sh <board-config-path> [menuconfig|distclean] [-j8]
```

> 具体的 board config 路径、目标产物、模拟器/真机部署方式请以你所在赛道的教程导航为准。本仓 `app/` `quickapp/` `board/` 三个示例骨架对应的 Kconfig 选项可通过 `menuconfig` 启用。

---

## 五、第四步：提交作品

1. **fork** 你的专属仓 → 开发 → `git commit` 并推送 → 向专属仓发起 **Pull Request**，可**自行 review 并合入**（无需等组委会）。
2. **AI Coding 日志**：与 AI 工具的对话会自动记录到本机 staging（不会自动上传），需你**主动导出/打包**选定会话到仓内 `logs/` 目录后一并提交。详见[《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)。
3. 若需改动 **nuttx 等公共仓库**，不在本仓改，而是 fork 对应公共仓、以 PR 提交到 `dev-ai-contest-2026` 分支，由组委会 review 后合入。

> ⏰ **提交作品截止：9 月 20 日**。截止后统一收回 push 权限，仍可查看 / clone。
>
> 获奖后再按要求将作品 PR 至 openvela 上游对应仓库（走标准 PR + CI 流程）。

### 关于 PR 与 CLA

- 本仓所有改动通过 **Pull Request** 合入（分支保护强制，可自行合入自己的 PR）。
- 首次贡献需在[**官网签署 CLA**](https://openvela.com/#/community/cla)；PR 上会自动跑 `cla/signature` 检查，在官网签署成功后，在 PR 评论 `/check-cla` 复检即可通过。

---

## 六、提交前：把本 README 改成你的作品说明

本文件目前是组委会给的**使用说明书**。**作品提交前，请把它替换成你自己作品的说明**，方便评委快速了解你做了什么、怎么跑起来。建议至少包含以下内容：

```markdown
# <你的作品名>

## 一、作品简介
<一句话/一段话说明这个作品是什么、解决什么问题、亮点在哪>

## 二、选题方向
<快应用 / 手表应用创新 ｜ AI 硬件产品创新 ｜ 新硬件适配 ｜ 自定方向，并简述理由>

## 三、目录结构
<列出你这个仓里各目录/文件的作用，例如：>
- `app/xxx/`        — <说明>
- `board/xxx/`      — <说明>
- `quickapp/xxx/`   — <说明>
- `logs/`           — AI Coding 日志
- `docs/` 或其他    — <说明>

## 四、运行方式
<拉取工程后，如何编译、烧录/部署、运行的完整步骤；最好能让评委照着一步步复现>

## 五、AI Coding 使用说明
<说明本作品如何借助 AI 辅助开发：
- 在需求拆解 / 方案设计 / 编码 / 调试 / 文档等环节如何与 AI 协作；
- AI 对开发效率或质量带来的实际帮助。
完整对话日志见 logs/ 目录>
```

> 提示：将会根据「作品本身 + 你的 README 说明 + `logs/` 里的 AI Coding 日志」来理解和评估你的作品，README 写清楚很重要。

---

## 附：仓库命名规范

`contest2026_<编号>_<队伍名>` — 编号三位零填充；队名 slug（全小写、英文/拼音、连字符）。例：`contest2026_105_MoodAnchor`。
（仓库由组委会统一创建，**每队仅一个仓**，无需自行命名。）
