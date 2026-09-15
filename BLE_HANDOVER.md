# 蓝牙（BLE）问题交接文档

面向接手的 AI / 工程师。目标是让手表能通过 BLE 被 Android App 连接。
**请先读完「已确认的事实」和「我犯过的错」两节再动手** —— 这份文档里有一部分
结论是错的，而那部分错误结论曾经把排查带偏两轮。

---

## 1. 平台与架构

| 项 | 内容 |
|---|---|
| 硬件 | 立创·黄山派（LCKFB Huangshan Pi），SiFli **SF32LB52** |
| 双核 | HCPU（Cortex-M33，跑 openvela/NuttX，XIP 在 NOR）+ **LCPU**（跑闭源蓝牙控制器固件） |
| 系统 | openvela（NuttX 分支 `dev-ai-contest-2026`），`build.sh --cmake` |
| 协议栈 | NuttX 原生 BT host（`nuttx/wireless/bluetooth/`，Zephyr 风格） |
| 传输 | host → vendor `sf32lb52_bth4.c`（H4 成帧）→ `sf32lb52_bt_adapter.c`（IPC 邮箱 + 共享内存环形缓冲 + LCPU 电源）→ LCPU |
| UI | LVGL v9，应用 `mood_anchor` |

**代码位置**（重要）：

- 仓库内：`app/huangshan_hal/`（`hs_ble.h` / `hs_ble_gatt.c` / `hs_ble_host.c` / `mood_anchor_main.c`）
- **仓库外**：`../nuttx`、`../vendor`。这两处的改动**不在仓库 git 里**，记录在 `PATCHES.md`
- 接收端（Android）：`/mnt/hgfs/ubu_share/蓝牙接收端/WatchBleService.kt`

---

## 2. 现象

在 LINK 页点开蓝牙开关后，历史现象按时间顺序出现过四种，**它们是不同原因**：

1. **整机冻结**（画面定住、触摸无响应）—— 断言停机
2. **卡在 `bt_netdev_register`**（画面还能动，但状态永远不变）—— 死锁
3. **FAILED**，日志 `-110`（ETIMEDOUT）—— 超时正常上报
4. **硬错误**（已定位并修复）—— RX 共享环越界，PC 落在 `memcpy`

最新一次旧固件烧录是第 4 种。日志：

```
[00:43:46] sf32lb52 bt: LCPU up in 1560 ms
[00:43:46] Assertion failed padfault.c:186 task: mood_anchor process: mood_anchor 0x12072f55
```

注意：`0x12072f55` 是 dump 中的 pthread 入口地址，不是故障 PC。按 dump 的
任务回溯还原，真正的故障路径是
`hpwork -> sf32lb52_bt_rx_worker -> sf32lb52_bt_ring_copy -> memcpy`。
根因是 RX worker 未校验 LCPU 共享环的瞬态非法头/下标便执行拷贝，已通过
`patches/vendor-sifli-ble-host-stability.patch` 增加边界校验和有限重试。
首次实机验证后 RX 崩溃已消失，host 能广播且手机能发起连接；随后定位到旧的
3 次 `bt_netdev_register()` 重试会在首次 Reset 超时后留下悬空回调。当前代码已
将 LCPU 冷启动移到 HCI 超时之前，并移除整套 host 重试，第二版已编译成功。

---

## 3. 已确认的事实（有日志/dump 支撑）

### 3.1 LCPU 启动耗时 1460–1950 ms

修好 `HAL_GetTick()` 之后，vendor 打印变得可信：

```
sf32lb52 bt: LCPU up in 1950 ms
```

而 host 给**单条**同步命令的预算是 `TIMEOUT_MSEC = 2500` ms
（`nuttx/wireless/bluetooth/bt_hcicore.c`）。

### 3.2 控制器「只回一条命令就哑」

一次典型失败（三个重试中的第 1 次）：

```
sf32lb52 bt: LCPU up in 1950 ms
hci_initialize: ERROR: BT_HCI_OP_READ_LOCAL_FEATURES failed: -110
hs_ble_host_start: ERROR: bt_netdev_register failed: -110 (attempt 1)
```

**Reset 成功了，紧随其后的 `READ_LOCAL_FEATURES` 超时。** 第 2、3 次重试连
Reset 都超时。

### 3.3 发送环曾被判定为「非空」，控制器完全不消费

```
sf32lb52 bt tx busy: rd=00000000 wr=00040000
```

该行来自 `sf32lb52_bt_wait_tx_idle()` 的超时分支。此时命令**根本没写进环**，
因为 `sf32lb52_host_send_packet()` 开头的 `sf32lb52_bt_wait_tx_idle()`
一看环不为空就直接返回错误。

### 3.4 `HAL_GetTick()` 曾经恒为 0

`HAL_GetTick()` 在 `vendor/sifli/chips/drivers/hal/bf0_hal.c:343` 是 `__weak`，
返回 `uwTick`；而 `uwTick` 只在 `HAL_IncTick()` 里自增。
**全仓库没有任何地方调用 `HAL_IncTick`**（NuttX 自己接管了 SysTick）。

证据：修好之前 `LCPU up in 0 ms`（假数据）；修好后变成 `1460 ms` / `1950 ms`。

---

## 4. 已定位并已修复的问题

### A. `HAL_GetTick()` 恒为 0 → vendor 所有超时失效 ★真实且重要

vendor/HAL 里成片的超时是这种写法：

```c
if (HAL_GetTick() != start) { count++; start = HAL_GetTick(); }
if (count >= limit) { return -ETIMEDOUT; }        /* tick 不动 → 永远到不了 */
```

`sf32lb52_bt_wait_tx_idle()` 正是如此 → 控制器一时不排空发送环，HCI 发送
线程就**永久自旋** → 命令发不出 → host 2.5 s 超时 → `cmd_queue_deinit()`
等这个永远退不出的线程 → **`bt_netdev_register()` 永久卡住**（现象 2）。

**修法**：在 `vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src/sifli_ap.c`
末尾给出强定义（并 `#include <nuttx/clock.h>`）：

```c
uint32_t HAL_GetTick(void)
{
  return (uint32_t)(clock_systime_ticks() * (MSEC_PER_SEC / CLK_TCK));
}
```

验证：`arm-none-eabi-nm` 里该符号由 `W` 变 `T`。

> ⚠️ 副作用：其它依赖 `HAL_GetTick()` 的 vendor 代码（LCDC / DSI / LPComp）
> 的超时行为也一并变回正常。显示路径实测正常，但接手时请留意。

### B. NuttX 同步命令超时后留下悬空信号量指针 ★真实且重要

`bt_hci_cmd_send_sync()` 把 `sem_t sync_sem` 开在**自己栈上**，经
`buf->u.hci.sync` 交给完成回调。超时返回时它 `nxsem_destroy()` 并丢栈帧，
**却没清掉 `buf->u.hci.sync`**，而该缓冲仍被 `g_btdev.sent_cmd` 引用。

迟到的 Command Complete 于是走到 `hci_cmd_done()`：

```c
if (sent->u.hci.sync != NULL)
  {
    FAR sem_t *sem = sent->u.hci.sync;   /* 指向已复用的栈内存 */
    nxsem_post(sem);                     /* → DEBUGASSERT(!NXSEM_IS_MUTEX(sem)) */
  }
```

复用的栈槽恰好带 mutex 标志位 → **整机停机**（现象 1）。
曾经的现场：`Assertion failed at nuttx/include/nuttx/semaphore.h:761 task: hpwork`。

**修法**：超时路径里清 `buf->u.hci.sync`；竞态下若响应缓冲已写入该字段，
释放它再清。见 `nuttx/wireless/bluetooth/bt_hcicore.c`
（搜注释 `The command buffer outlives this call`）。

### C. H2L 发送环下标从未被初始化 ★真实

`sf32lb52_bt_controller_enable()` 里的注释写着：

> *Flush H2L TX ring to SRAM ... so LCPU sees the reset indices (read_idx=write_idx=0) ...*

**但那段代码只调了 `up_clean_dcache()`** —— 那是把 cache 内容**回写**到 SRAM，
它从不写那两个 0。上一次会话残留的写指针因此原样存活（现象 3 的 `wr=0x40000`）。

**修法**：在回写之前显式置 `read_idx_mirror = 0; write_idx_mirror = 0;`。

### D. LCPU 启动吃掉了同步命令的预算

1950 ms / 2500 ms = 78%。**修法**：把 `sf32lb52_bt_controller_enable()` 从
`sf32lb52_bt_send()`（懒启动）提前到 `sf32lb52_bt_open()`。

> 这个改动被反复试过两次。**第一次失败是因为 A 没修**（tick 坏 → 等待环排空
> 变成永久自旋）。A 修好之后才值得再试。

### E. 我自己的诊断代码把系统打死过两次 ★务必引以为戒

为了在 LINK 页显示 `heap XXXX KB`，我在 LVGL 线程里每 500 ms 调一次
`mallinfo()`。它遍历整个堆空闲链表并加堆锁，在 `mm_foreach.c` 里踩断言：

```
Assertion failed : process: mood_anchor 0x12074405
  → ma_refresh_ble_ui → mallinfo() → mm_mallinfo() → mm_foreach()
```

**这导致连续三轮误判**，包括：
- 我把 `heap 7777 KB` 当成「内存不足」，去缩了一轮栈 —— **那条线索本身来自崩溃源**
- 我据此得出「不要在 `open()` 里 enable 控制器」的错误结论

**现已全部移除。诊断代码不允许有能力打死设备。**

---

## 5. 当前状态

- 上述 A–E **全部已烧录**
- 结果：硬错误，PC 指向 `hs_ble_host_start()`（`mood_anchor_main.c:2699`）
- 所有诊断代码（阶段文字、trace、`[ui]` 心跳、每步 0.7 s 停顿）**仍在**，
  清理它们应该等 BLE 真正跑通之后

---

## 6. 最强的剩余线索（建议优先级）

### 6.1 ★ 先确认官方用哪个协议栈

vendor 驱动里有这么一段（`sf32lb52_bth4.c:172`）：

```c
#if defined(CONFIG_BT) && defined(CONFIG_ZBLUE)
extern void z_sys_init(void);
static void sf32lb52_bt_zblue_init_once(void) { ... z_sys_init(); ... }
#else
static void sf32lb52_bt_zblue_init_once(void) { }
#endif
```

`sf32lb52_bt_initialize()` 会调它。也就是说 **SiFli 官方的那条路是 zblue**
（openvela 里的 Zephyr BLE 栈，在 `external/zblue/`），而不是 NuttX 自带的
`wireless/bluetooth` host。

**目前我们走的是 NuttX 自带 host + `bt_netdev_register()` 直绑 vendor 驱动。**
请优先确认：这条路径是否是官方支持的集成方式？如果官方根本不走这条，
后面的所有挣扎可能都是在逆着一个不匹配的设计做。

参考：`vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/src/sifli_ap.c`
（SiFli 官方板级实现，我们的板级文件与它逐行一致）。

### 6.2 环缓冲协议与 LCPU 握手

`sf32lb52_bt_adapter.c` 的注释里记录过一串历史问题（幽灵事件、ring 指针
不同步、`Command Disallowed`）。当前症状「回一条就哑」很像**握手/流控仍然
不匹配**。重点看：

- `sf32lb52_bt_controller_enable()` 里 RX ring 的 `read=write` 丢弃逻辑
- `sf32lb52_bt_wait_rx_ring_ready()`
- `sf32lb52_host_send_packet()` 把包**拆成两块**写（先写 1 字节 H4 类型，
  再写负载），每块都 `sf32lb52_bt_trigger_tx()`
- `sf32lb52_bt_emulate_cmd()` —— 一大串本地伪造 Command Complete 的命令，
  检查 `ncmd` 字段与 `g_btdev.ncmd` 流控是否自洽

### 6.3 硬错误而非断言

当前是 hardfault（不是 `DEBUGASSERT`），更可能是**内存踩踏或非法访问**，
而不是逻辑断言。dump 里有完整 backtrace，用 `addr2line` 逐层还原。

---

## 7. 工具与操作

### 构建

```bash
JOBS=12 ./scripts/build_huangshan.sh
# 改过 defconfig 后必须先：
rm -rf ../cmake_out/lckfb_huangshan_pi_nsh
```

### 烧录（约 260 s，第一次常失败，重试即可）

```bash
sg dialout -c "HS_FLASH_BAUD=460800 HS_FLASH_COMPAT=true scripts/flash_huangshan.sh /dev/ttyUSB0"
```

### 串口

- `/dev/ttyUSB0`，**1 000 000 8N1**
- **打开端口会通过 RTS 复位一次板子**（这是用户抱怨过多次的点）；
  打开之后保持只读就不再干扰
- `scripts/ble_capture.py` 自带重连与「静默 6 s」标记：
  ```bash
  sg dialout -c "python3 scripts/ble_capture.py --log logs/x.txt --seconds 1200"
  ```

### 解析崩溃 dump

```bash
arm-none-eabi-addr2line -f -C -e ../cmake_out/lckfb_huangshan_pi_nsh/nuttx <地址...>
```

dump 里的 `sched_dumpstack: backtrace| N:` 行，N 是任务号，
按叶子到根的顺序排列地址。

### 两个环境坑

- **本板 `syslog()` 是黑洞**：`CONFIG_SYSLOG=y` 但 `CONFIG_SYSLOG_CHAR` 与
  `CONFIG_SYSLOG_CONSOLE` 都没开，没有 sink。vendor 驱动里的
  `syslog(LOG_ERR, ...)` 一直看不到输出，排查时容易误判。**要用 `printf`。**
- CH340 经常掉 USB 总线 / 打开报 EIO，需要在 VMware 里重新插拔。

---

## 8. 用户的明确要求

1. **烧录前必须先问**，不要自作主张刷固件
2. **不要瞎加诊断代码**（已经因此崩过两次，见第 4 节 E）
3. 官方怎么做的就去照官方做，不要自己发明
4. 用户平时需要**把板子从电脑上拔下来**测试（插着电脑时串口 RTS 会干扰）

---

## 9. BLE 协议（如果最后需要回到这一层）

固件已按 Android 接收端对齐（`WatchBleService.kt` 是硬编码的字段偏移）：

```
…0004 Data   (16 B, ~1 Hz)
  0-1 GSR(u16 LE)  2 HR  3 SpO2  4 flags  5 mic  6 battery
  7-12 accel XYZ(i16 LE)  13-14 gyro(u16 LE)  15 mood(bit7=激动, bit0-6=置信度)

…0005 Status (16 B, 仅电量变化时)
  0 ver  1 flags  2-3 GSR  4 HR  5 SpO2  6-11 reserved  12 battery  13-15 reserved
```

接收端只读 `gsr / heartRate / spo2 / battery`，IMU 与麦克风它不解析。
详见 `BLE_PROTOCOL.md`。
