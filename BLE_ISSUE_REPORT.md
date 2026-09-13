# BLE 集成问题报告（求助）

## 最新进展（2026-09-14 夜间更新）

### 已解决 ✅
1. **系统卡死**：根因是诊断日志洪泛（每个 HCI 事件/每次 workqueue 调度都 printf，1Mbps console 阻塞拖死系统）。已全部清理，实测 LVGL 运行 3 分钟后 NSH 依然响应。
2. **UI 路径 BLE 启动 FAILED**：BLE 启动 worker 与 LVGL 同优先级（100），启动期间同步命令 2.5s 超时被饿死。已把 worker 提到 SCHED_FIFO priority 120。CLI 路径已复测成功，UI 路径待用户验证。
3. **广播数据超长**：改用栈 API `bt_start_advertising()`/`bt_stop_advertising()` 重写了 `hs_ble_adv_apply()`（外部帮助者改动，方向正确，已合并验证），不再绕栈发原始 HCI 命令，避免了两条命令流交错导致的 opcode 错配。

### 待用户验证 ⏳
1. **UI 点开关**是否稳定变绿（worker 优先级修复后）
2. **手机连接**是否成功 —— 栈 API 统一广播后，之前"连接建立但 ATT 无响应"的问题可能已顺带修复（命令流不再交错）
3. 若仍连接失败，嫌疑方向：LPWORK(priority 100) 与 LVGL 同级，ATT 请求处理可能被 UI 抢占

---

## 平台

- **硬件**：黄山派 (LCKFB Huangshan Pi)，SiFli SF32LB52，Cortex-M33 双核
  - HCPU 跑 openvela/NuttX（XIP 运行在 NOR Flash）
  - LCPU 跑蓝牙控制器固件（SiFli 闭源）
- **系统**：openvela（NuttX 分支 `dev-ai-contest-2026`），构建 `build.sh --cmake`
- **蓝牙栈**：NuttX 原生 BT host 栈（`nuttx/wireless/bluetooth/`，Zephyr 风格）
- **UI**：LVGL v9（`lv_nuttx`），应用 `mood_anchor`（NSH 命令，priority 100，栈 40KB）
- **传输**：`/dev/ttyHCI0` → `uart_bth4`（NuttX 驱动）→ vendor `sf32lb52_bth4.c`（`/dev/ttyHCI0` 的 H4 收发）→ IPC mailbox → LCPU，H4 协议

## 当前状态

✅ 已工作：
- host 栈完整初始化（HCI_RESET → 各查询命令 → GAP/GATT 注册）
- 广播命令被控制器接受（`LE_SET_ADV_ENABLE` 返回 status=0）
- 手机 nRF Connect **能扫描到设备**
- UI 蓝牙开关变绿、状态机正常

❌ 两个问题：
1. **手机连接失败**（nRF Connect 点连接连不上）
2. **运行一段时间后系统卡死**（屏幕冻结、NSH 无响应）

## 已修复的 4 个 bug（改动点）

### 1. fd 按任务组隔离 → write EBADF（`hs_ble_host.c`）

NuttX 的 fd 表是 per-task-group 的。`hci_tx_kthread` 是**内核线程**，看不到应用线程 open 的 `/dev/ttyHCI0` fd。
**症状**：open 成功（fd=3），但内核线程 send 时 `write(3)` 返回 EBADF → HCI_RESET 发不出去 → 超时 -110。
**修复**：RX 线程用应用组的 `rx_fd`；`send()` 里在**调用线程自己的任务组**懒打开 `tx_fd`（`uart_bth4` 支持多 open，refcnt>1 时不再触发 vendor 的 open/close）。

### 2. BT_EVT 必须带完整 H4 事件头（`hs_ble_host.c`）

`bt_receive(drv, BT_EVT, data, len)` 期望 `data = evt_code + plen + params`。
只传 params 时栈把 `params[0]`（ncmd）当事件码 → "Unhandled event 0x06" → RESET 等不到 cmd_complete。

### 3. workqueue 饿死 → CMD_COMPLETE 同步处理（改 `nuttx/wireless/bluetooth/bt_hcicore.c` 的 `bt_receive`）

**这是与上游最大的差异，也是我最担心的改动。**
现象：`work_queue(HPWORK, &g_hp_work, priority_rx_work, ...)` 返回 0（入队成功），但 `priority_rx_work` 在 2.5s 超时窗口内**从未执行**（用 printf 验证过）。HPWORK 线程同一时刻在跑 vendor 的 IPC `rx_worker`（`sf32lb52_bt_adapter.c`，由 LCPU mailbox 中断触发）。该平台非 tickless，wqueue 用 pending/expired 列表 + 绝对时间 wdog 定时器；从非 idle 线程重启定时器后 HPWORK 不再被唤醒（原因未明，没再深挖）。
**当前修复**：在 `bt_receive()` 里对 `CMD_COMPLETE / CMD_STATUS / NUM_COMPLETED_PACKETS` 事件**直接同步调用** `priority_rx_work(&g_hp_rxlist)`（在应用 RX pthread 上下文），完全绕开 HPWORK。同步命令的等待者在应用线程（`bt_initialize` 由应用调用），跨线程 `nxsem_post` 唤醒，理论无死锁，实测 host 栈能完成初始化。

> 请教点：这个同步处理在**连接建立后**（ACL/ATT 流量、L2CAP 协商）会不会有隐患？`priority_rx_work` 内部 `hci_cmd_done → bt_buf_release` 跨线程释放 `sent_cmd`（发送线程持有引用），官方 HPWORK 模型下本来就跨线程，应无碍？

### 4. 广播数据超长（`hs_ble_gatt.c`）

`LE_SET_ADV_DATA` 用 `sizeof(结构体)`=36 字节发送（1 长度 + 35 数据），超过 BLE 31 字节上限 → 控制器拒绝 → 手机搜不到。已改为按实际长度 `1 + data.len`（21 字节）。

### 其它

- `hs_ble_host_start()` 失败自动重试 3 次 × 1s（冷上电时 LCPU 就绪慢，偶发首批命令被丢，超时 -110；热复位则正常）。

## 遗留疑点（求助重点）

### 疑点 1：连接失败

手机能看到设备、点连接连不上。连接流程：手机发 CONNECT_IND → LCPU 产生 `LE_CONN_COMPLETE` 事件 → 我们的 RX 线程 → `bt_receive` → 栈建立 conn → L2CAP/ATT 协商。**未采集到连接时的日志**（诊断已清理，且串口一打开板子就复位，难以保留现场）。
可能的点：
- 连接事件（BT_EVT 中非 cmd 类的 LE 事件）走 LPWORK 路径，LPWORK(priority 100) 与 LVGL(100) 竞争，处理是否及时？
- `bt_receive` 的同步处理改动是否影响连接事件？
- 栈初始化阶段有杂散 cmd_status（opcode 错位 0x0801 vs 期望 0x2008，`Uncleared pending sent_cmd`、`bt_add_services: Advertising failed (-110)`），是否说明 controller 与 host 的同步仍有问题？

### 疑点 2：系统卡死

BLE 广播运行一段时间后整机卡死（屏幕冻、NSH 无响应）。**当时打开了大量诊断 printf**（bt_hcicore 每个事件、workqueue 每次 dispatch、vendor trace），怀疑 1 Mbps console 输出阻塞拖死系统，现已全部清理（保留错误路径打印）。**待验证卡死是否随诊断移除而消失**。若仍卡死，备选怀疑：
- HPWORK(priority 224) 上 vendor `rx_worker` 周期性空转/抢占，把 LVGL(100) 饿死
- LCPU 周期性中断（广播相关）导致的优先级倒置/死锁

### 疑点 3：cmd_status opcode 错位

LCPU 对 `LE_SET_ADV_DATA`(0x2008) 返回 cmd_status：`[status=01][ncmd=06][opcode=01 08]` → 解析为 0x0801，与发送的 0x2008 不符（"Unexpected completion"）。vendor 注释提到过 ring buffer 幽灵事件问题，已在 `controller_enable` 里做 `read=write` 修复，但偶发仍出现。这会破坏 sync 命令配对 → 超时。

### 疑点 4：广播名称显示为 "moodanchor"

手机显示的是 `CONFIG_BLUETOOTH_DEVICE_NAME="MoodAnchor"`（栈自动广播 `bt_add_services`），而非我们自定义 scan rsp 的 `是非钟-3412`。是栈自动广播成功后覆盖了我们手动广播？还是 scan rsp 未生效？

## 关键文件

- `app/huangshan_hal/hs_ble_host.c`（H4 绑定 + 双 fd + 事件头 + 重试）
- `app/huangshan_hal/hs_ble_gatt.c`（GATT 表 + 手动广播）
- `nuttx/wireless/bluetooth/bt_hcicore.c`（`bt_receive` 同步处理改动，diff 很小，可 grep `Process synchronously`）
- `vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c` / `sf32lb52_bt_adapter.c`（H4/IPC/ring buffer）

## 复现步骤

1. 上电（USB 直连，注意 CH340 RTS 接 SoC 复位脚，上电后需按复位键或串口 `rts=False` 释放复位）
2. 系统启动后滑到 LINK 页，点蓝牙开关（变绿 = host 栈起来，约 2~5s，含自动重试）
3. nRF Connect 扫描：可见 `moodanchor`（或 `是非钟-xxxx`）
4. 点击连接 → 失败
5. 观察一段时间 → 屏幕/系统卡死（诊断清理后待验证）
