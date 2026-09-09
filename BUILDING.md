# 构建 / 烧录 / 迁移指南

> 最后验证：2026-08-22，立创·黄山派（SF32LB52）实机跑通：系统启动、I2C 触摸、
> PWM、LCD 蓝屏、BLE HCI、LVGL 控件演示、`sysinfo` 调试面板全部 OK。

## 1. 目录结构

```text
/path/to/openvela/                      ← repo sync 拉下来的全量源码（nuttx/apps/packages/vendor 等）
/path/to/openvela/contest2026_105_MoodAnchor/   ← 本参赛仓（唯一要改的代码在这里）
```

本仓的 `openvela.xml` 已把 `app/huangshan_hal/` 软链到
`packages/demos/contest2026_105_huangshan_hal`。

## 2. 环境要求

| 组件 | 版本/位置 | 备注 |
|---|---|---|
| arm-none-eabi-gcc | 10.3（ARM 官方 2021.07） | 建议装在 `~/.local/opt/arm-none-eabi-10.3` |
| cmake | ≥3.22 | |
| ninja | ≥1.10 | 源码树 `prebuilts/build-tools/linux-x86_64/bin/ninja` 里也有 |
| python3 | ≥3.10 | `pip install --user kconfiglib pyelftools cxxfilt pyserial` |
| sftool | 0.2.5 | https://github.com/OpenSiFli/sftool/releases 预编译包解压到 `~/.local/bin` |
| 系统 | Ubuntu 22.04 | 必须 `sudo apt purge -y brltty`（它抢占 CH340 串口） |
| 串口权限 | `sudo usermod -aG dialout $USER` 后重新登录 | 设备节点属 root:dialout |

## 3. 同步源码

```bash
scripts/sync_openvela.sh <组委会提供的 manifest 仓库 URL> \
  contest2026_105_MoodAnchor.xml /path/to/openvela
```

同步完成后检查：

- `vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi` 存在
- `vendor/sifli/boards/sf32lb52/libs/nsh_cmake` 存在（SiFli 预编译库）
- `apps/graphics/lvgl/lvgl`、`apps/system/libuv/libuv`、`external/freetype/freetype` 存在
- 没有 `external/curl/curl`、`external/libpng/libpng`、`external/protobuf-c/protobuf-c`
  是正常的（defconfig 已关闭对应组件，不需要它们）

> 本仓自带 `scripts/apply_huangshan_patches.sh` 负责把底层改动应用到同步后的
> `nuttx` 和 `vendor/sifli`。`scripts/prepare_huangshan.sh` 只用于旧 Make 构建
> 入口；当前推荐先应用补丁，再按下面的 CMake 流程构建。

## 4. 应用本仓携带的底层补丁

本仓已经把 NuttX 和 SiFli 厂商仓的必要改动保存到 `patches/`，无需再手工
复制 vendor 仓。在 `repo sync` 完成后运行：

```bash
cd /path/to/openvela/contest2026_105_MoodAnchor
./scripts/apply_huangshan_patches.sh
```

脚本可重复执行：已应用的补丁会显示“已存在”，基线不匹配时会停止而不会
覆盖文件。补丁基于比赛源码的以下版本制作：

- `nuttx`: `dd92bcf425738734d1b8aed09c2bd4dbe3f2e438`
- `vendor/sifli`: `af6f365eaa04a674af0467aa1a803bc4c77691ba`

补丁包含：

- NuttX ARMv8-M FPSCR/SysTick 中断修复；
- `/dev/adc1`（PA28）GSR 通道和 `/dev/adc0` 原供电通道；
- PA28 模拟输入配置，并关闭与其冲突的 TF 卡/SPI1；
- PA44 VBUS 检测输入，用于区分有线供电和电池供电；
- LCD 冷启动供电延时，避免断电重启后黑屏；
- 开机 3 秒自动启动中文 `sysinfo` 面板；
- SiFli UART 的非法 `return ch` 编译修复；
- 为当前同步源码裁剪不可用的 QuickApp/C++/PNG 等配置。

以下章节保留关键修复原理，日常迁移只需运行上述脚本。

### 4.1 关键 BUG 修复：nuttx/arch/arm/src/armv8-m/arm_doirq.c

未修复时 SysTick 会因 FPSCR 内联汇编破坏 r0 导致中断风暴
（`irq_unexpected_isr: ERROR irq: 262144`，无法启动）。

```diff
@@ void exception_direct(void)
 #ifdef CONFIG_ARCH_FPU
   __asm__ __volatile__
     (
-      "mov r0, %0\n"
-      "vmsr fpscr, r0\n"
+      "vmsr fpscr, %0\n"
       :
-      : "i" (ARM_FPSCR_LTPSIZE_NONE)
+      : "r" (ARM_FPSCR_LTPSIZE_NONE)
+      : "memory"
     );
 #endif
```

### 4.2 编译修复：vendor/sifli/chips/sf32lb52/sifli_uart.c

`void up_putc(int ch)` 里删掉一行非法的 `return ch;`
（在 `#endif /* USE_SERIALDRIVER */` 之前）。

### 4.3 SiFli 板级和驱动补丁

`patches/vendor-sifli-huangshan.patch` 包含板级 `defconfig`、ADC、引脚、LCD、
USB 供电检测、开机脚本及 UART 修改。相对官方 `defconfig` 的主要改动：

- 关闭源码未同步/不兼容的组件：`INTERPRETERS_QUICKJS`、`LIB_YOGA`、`UNQLITE`、
  `LIB_PNG`、`LV_USE_LIBPNG`、`PROTOBUF_C`、`UTILS_CURL`、`CXX_LOCALIZATION`、
  `HAVE_CXX`、`LIBCXX`、`QUICKAPP*`
- 新增：`CONFIG_LVX_USE_HUANGSHAN_HAL=y`（本仓 HAL + demo）
- 新增：`CONFIG_PTHREAD_MUTEX_TYPES=y`（否则 lvgldemo 断言崩溃）

## 5. 工具链修补（新机器也要做）

ARM 官方裸工具链缺两样东西，CMake 配置会失败：

```bash
T=~/.local/opt/arm-none-eabi-10.3

# 5.1 nosys.specs（CMake 编译器探测用）
cat > $T/usr/lib/gcc/arm-none-eabi/10.3.1/nosys.specs <<'EOF'
%rename link_gcc_c_sequence                nosys_link_gcc_c_sequence

*startfile:
%{!pg:crt0.o%s}%{pg:gcrt0.o%s}

*lib:
%{!nostdlib:-lc -lgcc}
EOF

# 5.2 libm.a 符号链接（--print-file-name 解析不到 multilib 目录）
mkdir -p $T/usr/lib/arm-none-eabi/lib
ln -sf ../../arm-none-eabi/newlib/thumb/v8-m.main+fp/hard/libm.a \
    $T/usr/lib/arm-none-eabi/lib/libm.a
```

## 6. 构建

```bash
cd /path/to/openvela
export PATH=~/.local/opt/arm-none-eabi-10.3/usr/bin:$PWD/prebuilts/build-tools/linux-x86_64/bin:$PATH

cmake -B cmake_out/lckfb_huangshan_pi -S "$PWD/nuttx" -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"

cmake --build cmake_out/lckfb_huangshan_pi -j$(nproc)
# 产物：cmake_out/lckfb_huangshan_pi/nuttx.bin（约 3.8 MB）
```

注意事项：

- 第一次 configure 前如果 nuttx 里有旧 Make 构建状态，先 `make distclean`；
- 改 defconfig 后要 `rm cmake_out/lckfb_huangshan_pi/.config` 再重新 configure；
- 构建目录里也需要 littlefs：`nuttx/fs/littlefs/littlefs/lfs.c` 存在即可
  （缺失时从 GitHub 下载 v2.5.1 并 `git apply littlefs/*.patch`）。

## 7. 烧录

```bash
sftool -c SF32LB52 -p /dev/ttyUSB0 -b 1000000 \
  --before no_reset --after soft_reset --compat true \
  write_flash cmake_out/lckfb_huangshan_pi/nuttx.bin@0x12010000
```

推荐直接运行 `scripts/flash_huangshan.sh`。脚本先用 pyserial 正确脉冲
RTS，再让 sftool 以 `--before no_reset --compat true` 连接，不需要重新插拔。

- 镜像烧到内置 NOR `0x12010000`，约 2-3 分钟；
- 报 `Failed to connect` 时先确认没有其他串口程序占用 `/dev/ttyUSB0`，然后重跑
  `scripts/flash_huangshan.sh`；不要把反复插拔当作正常操作；
- 烧录不需要按任何按键（黄山派无 BOOT 键，RTS 自动复位进下载模式）。

## 8. 串口与复位（重要！）

- 串口参数：**1000000 8N1，无流控**；
- 板子 **RTS# 直连复位**（低电平有效）：
  - pyserial：`ser.rts = True` = **按住复位**，`ser.rts = False` = 释放运行；
  - 打开串口时驱动可能短暂拉 RTS，打开后必须显式设置 `ser.rts = False`；
  - 手动复位顺序：`rts=True` → 等 50ms → `rts=False`；退出前也保持 False；
- 推荐串口工具：`picocom -b 1000000 --noreset --lower-rts --lower-dtr /dev/ttyUSB0`
  或 `pyserial-miniterm /dev/ttyUSB0 1000000`；
- 启动日志正常应为 `SFBL` → `ABCD` → ADC 校准 → NSH 提示符 `nsh>`。

## 9. 上板验收命令

```text
uname -a                        # NuttX 0.0.0 ... arm nsh
ls /dev                         # adc0 i2c0 i2c1 pwm0 fb0 input0 lcd0 ttyHCI0 lsm6dsl0 ...
huangshan_hal_demo i2c          # FT6146: ok
huangshan_hal_demo adc          # 内部 VBAT ADC（channel 5），输出毫伏值
huangshan_hal_demo power        # 一次输出 USB/VBAT/充电芯片原始寄存器/KEY2
huangshan_hal_demo pwm          # pwm 1kHz/50%: ok
huangshan_hal_demo lcd          # 整屏蓝色
huangshan_hal_demo lcdtest      # 彩条/棋盘格/文字循环，独立验证 LCD + framebuffer + 刷新
huangshan_hal_demo ble          # HCI reset: ok
huangshan_hal_demo sysinfo      # LCD 调试面板（I2C/触摸/VBAT/KEY2/时间，1Hz 刷新，Ctrl+C 退出）
huangshan_hal_demo gsr_once     # Grove GSR：PA28 /dev/adc1 单次采样
huangshan_hal_demo gsr_cal 30   # 空载校准，记录 CAL_RAW10
huangshan_hal_demo gsr_stream <CAL_RAW10>  # 约5Hz CSV，Ctrl+C退出
lvgldemo widgets                # LVGL 控件演示 + 触摸交互
fb                              # 嵌套彩色矩形
```

## 10. 硬件要点

- 两个按键与烧录无关：KEY1 长按 10 秒 = 硬件复位，KEY2 = 功能键；
- 电池规格为 3.7V 单节锂聚合物、GH-1.25 2P 插头（红正黑负，按电池座
  丝印）。板子可以仅靠 USB 运行；实测拔掉电池后 VBAT ADC 仍约 3315mV，
  因此不能只凭 ADC 电压判断电池是否接入。请对比 `power` 命令中的 AW32001
  原始寄存器，确认位定义前不要把它标成“充电/满电/无电池”；
- 若主机内核报告 `ch341-uart ... control message: -110` 且打开串口返回
  `EIO`，故障位于 CH340/USB 链路（虚拟机环境还包括 USB 透传），不是 NSH
  应用主动断开串口。此时需在主机侧复位 USB 设备或重新附加 USB 透传；
- 屏幕排线：22-pin FPC，两头锁扣翻开→插到底→压紧，金手指朝下；
- 无屏幕时启动日志会报 `ft6146_touch_initialize failed: -5`（正常，触摸在屏幕板上）。

## 11. 已知无害告警

- 启动时 `nsh: vapp: command not found`：QUICKAPP 已关闭，无害；
- `WARN: skip HAL_FLASH_Init during XIP bringup, NOR write/erase disabled`：
  XIP 启动的正常提示；
- `ADC calibration data missing, use defaults`：首次上电正常。

## 12. 本仓代码位置

- 应用代码：`app/huangshan_hal/`（HAL 封装 + `huangshan_hal_demo` 命令，
  `sysinfo` 屏幕调试面板含内置 5x7 点阵字库）
- 作品提交前把 `README.md` 改成作品说明，AI 日志放 `logs/`。
