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

### 6. `nuttx/wireless/bluetooth/bt_hcicore.c` — 同步命令超时后解除悬空指针

搜索注释 `The command buffer outlives this call` 即可定位，改动在
`bt_hci_cmd_send_sync()` 里。

**为什么**：`bt_hci_cmd_send_sync()` 把 `sem_t sync_sem` 开在**自己的栈上**，
通过 `buf->u.hci.sync = &sync_sem` 交给完成回调。超时返回时它 `nxsem_destroy()`
这个信号量并丢栈帧，**但没有清掉 `buf->u.hci.sync`**。而该命令缓冲仍被
`g_btdev.sent_cmd` 引用着，于是迟到的 Command Complete 走到：

```c
  if (sent->u.hci.sync != NULL)
    {
      FAR sem_t *sem = sent->u.hci.sync;   /* 指向已复用的栈内存 */
      nxsem_post(sem);                     /* → DEBUGASSERT(!NXSEM_IS_MUTEX(sem)) */
    }
```

复用的栈槽恰好长得像 mutex，断言直接**停机**（屏幕冻在最后一帧、此后蓝牙再也
起不来）。也就是说，**一条慢一点的 HCI 命令可以把整机打死**。

**改法**：`ret < 0` 时把 `buf->u.hci.sync` 置 NULL；若在此期间完成回调已经
把响应缓冲写进该字段（竞态），则在超时路径里 `bt_buf_release()` 它再置 NULL。

**效果**：超时退回成本来的语义 —— 一次失败，`hs_ble_host_start()` 的 3 次重试
机会得以生效，而不是停机。

### 7. `vendor/sifli/chips/sf32lb52/sf32lb52_bt_adapter.c` — LCPU 启动耗时打印

`sf32lb52_bt_controller_enable()` 里加了一行：

```c
printf("sf32lb52 bt: LCPU up in %lu ms\n",
       (unsigned long)(HAL_GetTick() - t0));
```

注意本板 **`CONFIG_SYSLOG_CHAR` / `CONFIG_SYSLOG_CONSOLE` 都没开**，该文件里
原有的 `syslog()` 全是黑洞，所以这里必须用 `printf`（并补
`#include <stdio.h>`）。

> **试过但已撤回**：曾把 `sf32lb52_bt_controller_enable()` 从 `send()` 路径
> （懒启动）提到 `sf32lb52_bt_open()` 里，想让 LCPU 启动不占用同步命令的
> 2.5 s 超时预算。实测反而更容易卡死，已还原成懒启动。
> **不要**再从 `open()` 里调用 `sf32lb52_bt_controller_enable()`。

`sf32lb52_bth4.c` 的 `sf32lb52_bt_open()` 里保留了说明这一点的注释。

### 8. `vendor/sifli/boards/.../src/sifli_ap.c` — 补上 `HAL_GetTick()`

文件末尾新增一个强定义（并加 `#include <nuttx/clock.h>`）：

```c
uint32_t HAL_GetTick(void)
{
  return (uint32_t)(clock_systime_ticks() * (MSEC_PER_SEC / CLK_TCK));
}
```

**为什么**：`bf0_hal.c` 里的 `HAL_GetTick()` 是 `__weak`，返回 `uwTick`；
而 `uwTick` 只在 `HAL_IncTick()` 里自增，那是给裸机 SysTick 处理器用的。
**整个移植层没有任何地方调用 `HAL_IncTick`**（NuttX 自己接管了 SysTick），
所以 `uwTick` 永远是 0，`HAL_GetTick()` 永远返回 0。

后果不是"时间不准"，而是 vendor/HAL 里成片的超时全部失效：

```c
if (HAL_GetTick() != start) { count++; start = HAL_GetTick(); }
if (count >= limit) { return -ETIMEDOUT; }        /* 永远到不了 */
```

`sf32lb52_bt_wait_tx_idle()` 正是这个写法 → 蓝牙控制器只要一时没排空
发送环，HCI 发送线程就**永久**自旋，命令发不出去，host 2.5 s 超时
（`BT_HCI_OP_READ_LOCAL_FEATURES -110`），`cmd_queue_deinit()` 又等这个
永远退不出的线程 —— `bt_netdev_register()` 因此永久卡住，重试逻辑根本
轮不上。这就是「点开蓝牙一直停在 `bt_netdev_register`」的根因。

验证：`arm-none-eabi-nm` 里该符号由 `W` 变成 `T`。

> ⚠️ 修好之后，其它依赖 `HAL_GetTick()` 的 vendor 代码（LCDC / DSI /
> LPComp 等）的超时行为也会跟着变回正常。之前它们的判断是"要么立刻
> 满足、要么永远不满足"，取决于写法方向。显示相关路径已实测正常。

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

- **同步命令超时是致命的**：`bt_hci_cmd_send_sync()` 的 `sync_sem` 在栈上，
  超时路径不清指针 → 迟到回包对着复用栈内存 `nxsem_post()` → 断言停机。
  已修（见“必需改动 6”），但写新代码时注意同一模式。
- **本板 `syslog()` 是黑洞**：`CONFIG_SYSLOG=y` 但 `CONFIG_SYSLOG_CHAR` 与
  `CONFIG_SYSLOG_CONSOLE` 都没开，没有 sink。vendor 驱动里的 `syslog(LOG_ERR,...)`
  一直看不到输出，排障时容易误判。要可见就用 `printf`。
- **抓崩溃现场很有用**：`logs/ble-crash.txt` 里的 `sched_dumpstack: backtrace|N:`
  地址，配 `arm-none-eabi-addr2line -f -C -e
  cmake_out/lckfb_huangshan_pi_nsh/nuttx <addr...>` 可以直接还原调用栈。
  本板串口分块会打乱行，日志看着乱但地址是完整的。
- **中断上下文禁用 printf**：LCPU mailbox 中断 → work_queue → printf → 断言崩溃
  （`semaphore.h:518 DEBUGASSERT(!up_interrupt_context())`）
- NuttX fd 表按 task group 隔离（内核线程看不到应用 fd）
- `bt_receive` 的 BT_EVT 必须带完整 `evt_code + plen + params`
- BLE 广播数据 ≤31 字节，发送参数用实际长度而非结构体 sizeof
- 串口 console 1 Mbps；打开串口 RTS 抖动会复位板子；插拔后板子因 RTS
  拉高卡在复位（上电黑屏正常）；卡在 SFBL 时需断电 >10s
