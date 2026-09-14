# openvela 工作区外部改动清单

本仓库（contest2026_105_MoodAnchor）只含 app/ 与 scripts/。
以下对 openvela 工作区其它路径的改动**不在本仓库 git 里**，重建时需手动确认。

## 必需改动（缺一不可）

### 1. `nuttx/wireless/bluetooth/bt_hcicore.c` — `bt_receive()` 同步处理 CMD 完成事件

搜索注释 `Process synchronously in the caller's context` 即可定位。

**为什么**：此平台上 HPWORK 被 vendor 的 IPC rx_worker 占用，且 wqueue
定时器从非 idle 线程重启后 HPWORK 不再被唤醒，导致 `priority_rx_work`
被饿死、所有同步命令超时（-110）。
**改法**：`bt_receive()` 里对 `CMD_COMPLETE / CMD_STATUS /
NUM_COMPLETED_PACKETS` 三类事件，`bt_enqueue_bufwork(&g_hp_rxlist, buf)`
之后**直接同步调用** `priority_rx_work(&g_hp_rxlist)` 并 return，
不再 `work_queue(HPWORK, ...)`。
**安全性**：等待者在应用线程（`bt_initialize` 由应用调用），post 在 RX
pthread，跨线程唤醒无死锁。实测 host 栈初始化、广播、3 分钟稳定运行均正常。

### 2. `nuttx/drivers/wireless/bluetooth/bt_uart.c` — RX 回调同步化（官方传输层补丁）

搜索注释 `Process the packet synchronously in the callback context`。

- `btuart_rxcallback()`：官方实现 `work_queue(HPWORK, ...)`；本平台改
  **同步调用 `btuart_rxwork(arg)`**（shim 读是非阻塞的，回调运行在 vendor
  rx_worker 的 HPWORK 线程上，不会卡死）。
- `btuart_read()` / `btuart_rxwork()`：非阻塞读返回 0（无数据）是正常现象，
  官方代码把它当错误打印 `Returned error 0` / `btuart_read failed`，
  已改为静默返回；分包到达的 `Incomplete packet` 警告降级为 wlinfo。
- 对应删除了未使用的 `upper` 局部变量。

### 3. `vendor/sifli/boards/.../configs/nsh/defconfig` — 官方 UART 传输层开关

```
CONFIG_UART_BTH4=y
CONFIG_BLUETOOTH_UART=y
CONFIG_BLUETOOTH_UART_SHIM=y
CONFIG_BLUETOOTH_UART_OTHER=y
```

### 4. `vendor/sifli/chips/sf32lb52/sf32lb_adc.c` — ADC 单次读取耗时

`adc_read()` 的平均采样循环里，两次转换之间要等 10 ms：

```c
total += data[i];
HAL_Delay_us(10 * 1000);      /* 20 次 × 10 ms = 单次调用阻塞约 200 ms */
```

而一次转换只需要 `sample_width + conv_width = 146` 个 ADC 时钟（几十 µs），
并且本板要测的两路信号 —— 电池电压、Grove GSR 电极 —— 都是**秒级慢变**。

**改为 `HAL_Delay_us(1000)`**，单次 `hs_adc_read()` 从约 200 ms 降到约 20 ms。

影响面：

- UI 定时器与 `huangshan_hal_demo` 的 GSR 采样不再长时间阻塞渲染线程
- 代码里声明的 **5 Hz GSR 采样率此前实际达不到**（200 ms/次 → 最多 4 Hz 且卡顿）
- 平均次数仍为 20（去最大最小后 18 次平均），**精度不变**

> 补充：`sf32lb_adc_calibrate()` 在黄山派上会**跳过读取校准数据**（注释说明是
> 怕 `HAL_LCPU_CONFIG_get()` 在单核板上挂死 HCPU），改用硬编码默认值
> `vol10=1758 / vol25=3162 / low_mv=1000 / high_mv=2500`，即
> `ratio=1068 / offset=822`。这是 vendor 的既有取舍，未改动。

### 5. `vendor/sifli/chips/sf32lb52/sf32lb_adc.h` — VBAT 通道号错误

```c
/* 原值 */
#define ADC_CHAN_VBAT          ADC_CHAN_5      /* 读到的是系统供电 3.3 V */
/* 修正 */
#define ADC_CHAN_VBAT          ADC_CHAN_7      /* 真正的电池电压输入 */
```

**根因是命名差一**：SiFli 文档对 GPADC1 输入用 **1-based 编号**（`sf32lb_adc.h`
里给 PA28 的注释已经点明了这个坑 —— 文档的 "ADC CH1" 对应代码的 channel 0）。
文档所说的电池输入 **"CH8" 即 channel 7**，而原代码写成了 channel 5，
于是读到了 VSYS/LDO 的 3.3 V，与电池电量无关。

配套改动：

- `vendor/sifli/boards/.../src/sifli_ap.c` 通过 `sf32lb_adc_init("/dev/adc0")`
  自动跟随新的 `ADC_CHAN_VBAT`，无需单独修改
- `app/huangshan_hal/huangshan_hal.h`：`HS_ADC_VBAT_CHANNEL` 由 `5` 改为 `7`
  （`hs_adc_read()` 要按通道号匹配返回样本里的 `am_channel`）

## 可选改动

### 2. `vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh/defconfig`

- `# CONFIG_DEBUG_WIRELESS_INFO is not set`（诊断期间开过，**必须保持关闭**，
  否则串口日志洪泛会把系统拖死 —— 已实测）
- 早期裁剪过：`# CONFIG_EXAMPLES_LVGLDEMO is not set`、
  `# CONFIG_LV_USE_DEMO_WIDGETS/FREETYPE/QRCODE/VECTOR_GRAPHIC is not set`、
  `# CONFIG_LIB_FREETYPE is not set`（收益很小，可回退）
- `CONFIG_DEBUG_WIRELESS=y` / `_ERROR` / `_WARN` 保持开启（错误可见）

### 3. `vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src/etc/init.d/rcS`

```
sleep 2
mood_anchor &
```
注意 rcS 经 C 预处理器，**不能有 `#` 注释**。

### 4. vendor TRACE 宏（已还原，勿改）

`sf32lb52_bth4.c` / `sf32lb52_bt_adapter.c` 的 `SF32LB52_BT_TRACE` 必须为 0。
诊断期间曾改为 1，会打印每个 HCI 包的日志（中断上下文里还踩过
printf→nxmutex_wait 断言崩溃的坑）。

## 踩坑速记

- **中断上下文禁用 printf**：LCPU mailbox 中断 → work_queue → printf → 断言崩溃
  （`semaphore.h:518 DEBUGASSERT(!up_interrupt_context())`）
- NuttX fd 表按 task group 隔离（内核线程看不到应用 fd）
- `bt_receive` 的 BT_EVT 必须带完整 `evt_code + plen + params`
- BLE 广播数据 ≤31 字节，发送参数用实际长度而非结构体 sizeof
- 串口 console 1 Mbps；打开串口 RTS 抖动会复位板子；插拔后板子因 RTS
  拉高卡在复位（上电黑屏正常）；卡在 SFBL 时需断电 >10s
