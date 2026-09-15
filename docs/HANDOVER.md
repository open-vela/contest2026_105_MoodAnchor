# MoodAnchor 项目交接文档

> 生成时间：2026-09-14
> 当前分支：`wsy-gsr-panel`，最新提交 `d0e9cdb`
> ⚠️ **`d0e9cdb` 已编译但尚未烧录到板子**，烧录操作被中断了。

---

## 1. 硬件与工具链

| 项 | 值 |
|---|---|
| 板子 | LCKFB 黄山派（SiFli SF32LB52，Cortex-M33 双核 HCPU+LCPU） |
| 系统 | openvela / NuttX，分支 `dev-ai-contest-2026` |
| 显示 | CO5300 AMOLED，LVGL v9 + `lv_nuttx` |
| 串口 | `/dev/ttyUSB0`，CH340N，**1000000 8N1** |
| 宿主 | VMware 虚拟机（USB 需手动透传，见下） |

### 路径

```
工作区（git 仓库）      /home/wsy/Desktop/openvela/work/contest2026_105_MoodAnchor
openvela 源码根         /home/wsy/Desktop/openvela/work/{nuttx,apps,vendor}   ← 在工作区之外！
实际 defconfig          /home/wsy/Desktop/openvela/work/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh/defconfig
构建产物                /home/wsy/Desktop/openvela/work/cmake_out/lckfb_huangshan_pi_nsh/nuttx.bin
```

> ⚠️ **工作区里的 `board/contest_board/` 不是实际构建用的板目录**，修改板级配置要去上面那个 `vendor/...` 路径。

### 构建

```bash
cd /home/wsy/Desktop/openvela/work/contest2026_105_MoodAnchor
JOBS=12 ./scripts/build_huangshan.sh     # 增量约 20~30 s
```

改动 **defconfig 后必须全量重编**：
```bash
rm -rf /home/wsy/Desktop/openvela/work/cmake_out/lckfb_huangshan_pi_nsh
```
（全量重编约 4~8 分钟）

### 烧录（约 4.5 分钟，很慢）

```bash
cd /home/wsy/Desktop/openvela/work/contest2026_105_MoodAnchor
sg dialout -c "HS_FLASH_BAUD=460800 HS_FLASH_COMPAT=true \
  /home/wsy/Desktop/openvela/work/contest2026_105_MoodAnchor/scripts/flash_huangshan.sh /dev/ttyUSB0"
```

**踩坑提示：**
- `Errno 5 / Input/output error` 是常态，**直接重试**通常第 2 次成功
- `/dev/ttyUSB0` 消失 → 在 VMware 菜单「可移动设备」里重新连接 CH340，或拔插 USB
- 卡在 `SFBL` → 拔 USB 等 10 秒以上再插

### 读串口日志

**⚠️ 打开串口会通过 RTS 复位板子**，无法做到"不复位地监听"。

```bash
sg dialout -c "timeout 25 python3 -c \"
import serial,sys,time
s=serial.Serial('/dev/ttyUSB0',1000000,timeout=1)
try: s.rts=False
except Exception: pass
t=time.time()
while time.time()-t<18:
    d=s.read(8192)
    if d: sys.stdout.write(d.decode('utf-8','replace')); sys.stdout.flush()
\""
```

---

## 2. BLE 架构（**关键，不要走弯路**）

参考 openvela 官方 `frameworks_bluetooth` 说明：

```
application → bt_netdev_register(vendor bt_driver_s)
vendor drv->open/send/close  ↔  LCPU IPC mailbox
vendor receive 回调 → bt_netdev_receive() → NuttX 主机栈
```

**厂商驱动 `vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c` 本身就是一个完整的 `bt_driver_s`**，`receive` 由协议栈初始化，厂商只需调 `bt_netdev_receive()`。

> ❌ **历史教训**：早期版本自创了 `/dev/ttyHCI0` 字符设备 + `bt_uart_shim` + 手写 H4 层，共 682 行，**全部是无用功**，已删除。
> 现在的 `hs_ble_host.c` 只有约 145 行，核心就是一句 `bt_netdev_register(sf32lb52_bt_get_driver())`。

---

## 3. 已完成并验证的功能

### 广播

| 项 | 值 |
|---|---|
| 设备名 | `是非钟-0001`（UTF-8，主广播包内） |
| 主包 | Flags `0x06` + Appearance `0x0341`(Watch) + Complete Local Name |
| 扫描响应 | 128 位 MoodAnchor Service UUID |
| 间隔 | 空闲 200 ms；事件触发后 100–200 ms（boost） |
| 配对 | 不需要，免配对可读写 |

### GATT 服务表

```
GAP        0x0001   Device Name / Appearance
GATT       0x0006   Service Changed
MOOD       0x0010   d38a0001-1234-5678-9abc-def012345678
  ├ 0x0011/12/13   d38a0002 事件   Notify + Read   11 字节
  ├ 0x0014/15      d38a0003 控制   Write  + Read
  ├ 0x0016/17/18   d38a0004 数据   Notify + Read    6 字节
  └ 0x0019/1a/1b   d38a0005 状态   Notify + Read   16 字节
DIS        0x0020   厂商 / 型号 / 固件 / 序列号 / PnP ID
BAS        0x0030   电量（值变化时 notify）
```

**事件包（`...0002`，11 字节）**
`版本1 | 类型1 | 序号2 | 时间戳4 | 风险1 | 置信度1 | 标志1`

**数据包（`...0004`，6 字节）**
`GSR_mV_LE2 | HR1 | SpO2 1 | 标志1 | 保留1`

**状态包（`...0005`，16 字节）**
`[0]版本 [1]标志 [2-3]GSR_mV [4]HR [5]SpO2 [6-7]Ax [8-9]Ay [10-11]Az(mg) [12]电量% [13]按键 [14]振动 [15]保留`

**控制特征（`...0003`）**：`0x01`=ACK，`0x02`=CLEAR，`0x03`=设置上报周期

### 手机侧实测结果（已由用户确认）

nRF Connect 连接后可见：

```
CONNECTED / NOT BONDED（预期，我们不做加密配对）
Generic Access / Generic Attribute / Device Information / Battery Service
Unknown Service（= 自定义 MoodAnchor 服务）
  4 个自定义特征 ...0002 / ...0003 / ...0004 / ...0005 均存在
```

### 其他已完成项

- 断连后**自动重开广播**（BLE 规范：连接建立后控制器停播，断开不会自动恢复）
- **开机自动广播**，LINK 页开关作为手动控制
- **PPG 真实心率算法**（`hs_ppg.c`，详见 §4）
- UI 性能优化：重绘节流、IMU 1 Hz、LVGL 日志静默
- 电量跟随实测 VBAT；按键进入状态包

---

## 4. 待完成 / 待验证

| # | 项 | 状态 |
|---|---|---|
| 1 | **烧录 `d0e9cdb`** | ⏳ 未烧录，见 §6 |
| 2 | PPG 算法硬件验证 | ⏳ 需烧录后手指贴传感器测试 |
| 3 | `...0005` 状态特征联调 | ⏳ 待手机端订阅验证 |
| 4 | **手机系统蓝牙搜不到** | ❌ 见 §5，属于手机侧限制 |
| 5 | 设备名后缀固定 `0001` | 未做（需内核导出 BD_ADDR） |
| 6 | GSR「情绪指数」 | 简单线性映射，非真实校准 |
| 7 | SpO2 | 通用经验公式，**未做临床标定**，只能看趋势 |
| 8 | 振动/按键 | 已接入数据链路，UI 尚未展示 |

---

## 5. 重大已知问题详解

### 5.1 手机系统蓝牙看不到设备（**无法在表端修复**）

**现象**：nRF Connect 能搜到并连接，但手机「设置 → 蓝牙 → 可用设备」里始终看不到 `是非钟-0001`。

**已排除的假设**：
- ❌ 广播数据超长 → 已确认主包仅 23 字节
- ❌ 名字放在扫描响应里 → 已移到主包
- ❌ 缺 Appearance → 已加 `0x0341`
- ❌ 广播间隔太慢 → 已从 500 ms 降到 200 ms
- ❌ `BR/EDR Not Supported` 位导致过滤 → **试过去掉该位（flags `0x02`），结果更糟**：手机和 nRF 把它当双模设备，去尝试不存在的经典蓝牙侧，扫描卡住、连接开始失败。**已回滚为 `0x06`**。

**结论**：Android 系统蓝牙的「可用设备」列表主要来自 **经典蓝牙 inquiry**，纯 BLE 外设（声明 BR/EDR Not Supported）在多数 ROM（尤其中文定制 ROM）上不会出现。SF32LB52 **没有经典蓝牙射频**，所以表端无法解决。

**验证方法**：让用户观察手机系统蓝牙里是否出现过**其它** BLE 手环/设备。若也没有，即证实是 ROM 行为。

**推荐方案**：改用 App 扫描（BLE 的标准用法），这正是合作方 Android 端 `WatchBleService` 该做的事。

### 5.2 连接失败（前一版本引入，已修复）

`flags = 0x02` 导致扫描器/手机按双模设备处理，连接失败。改回 `0x06`。

**另外**：每当 GATT 服务表有增减（例如新增 `...0005`），Android 会使用旧的服务缓存。测试前应先在系统蓝牙里「忘记此设备」，或关闭再打开手机蓝牙。

### 5.3 MAX30102 长期返回 `EAGAIN`

FIFO 指针（`0x04`/`0x06`）一直相等。传感器配置本身是对的（SpO2 模式、100 Hz、411 µs、7.2 mA）。

已修正一处：`FIFO_CONFIG`(0x08) 从 `0x0f` 改为 **`0x1f`**，使能 FIFO rollover —— 原配置下 FIFO 写满会**直接停止出数**。

**待验证**。若仍无数据，用 `huangshan_hal_demo` 的 NSH 命令查 I2C 探测情况。

---

## 6. 当前代码状态（`d0e9cdb`，**未烧录**）

相对上一版烧录的固件，`d0e9cdb` 包含三处修复：

1. **广播 flags `0x02` → `0x06`**（修复连不上）
2. **BLE 开关跟随状态机同步**（修复「没开开关却显示 ON」）
3. **PPG 轮询周期 5 ms → 10 ms**（与传感器 100 Hz 出数匹配）

> 板子上现在跑的仍是**有问题的那一版**（flags `0x02`）。**必须重新烧录 `d0e9cdb` 才能得到正确行为。**

---

## 7. 关键文件

### 工作区内（本仓库）

| 文件 | 说明 |
|---|---|
| `app/huangshan_hal/hs_ble.h` | BLE 对外接口、包格式、常量 |
| `app/huangshan_hal/hs_ble_host.c` | 主机栈绑定（~145 行，官方 BTH4 架构） |
| `app/huangshan_hal/hs_ble_gatt.c` | GATT 数据库、广播、事件/数据/状态特征 |
| `app/huangshan_hal/hs_ppg.h/.c` | **PPG 算法**（DC 跟踪 / 峰值检测 / 心率 / SpO2） |
| `app/huangshan_hal/mood_anchor_main.c` | 4 页 LVGL UI、BLE 状态机、PPG 采样线程 |
| `app/huangshan_hal/huangshan_hal.c/.h` | 传感器 HAL（GSR/IMU/MAX30102/ADC/振动/按键/I2C/LCD） |
| `app/huangshan_hal/CMakeLists.txt` | 两个 app target 的源文件表 |
| `scripts/{build,flash}_huangshan.sh` | 构建/烧录封装 |
| `PATCHES.md` | **对内核/厂商代码的改动记录** |
| `BLE_ISSUE_REPORT.md` | BLE 问题报告 |

### 工作区外（需要一并交接！）

| 文件 | 改动 |
|---|---|
| `nuttx/wireless/bluetooth/bt_hcicore.c` | `bt_receive()` 中 CMD_COMPLETE/CMD_STATUS/NUM_COMPLETED_PACKETS **改为同步处理**，不再投递 HPWORK |
| `nuttx/wireless/bluetooth/bt_buf.c` | `bt_buf_release()` 增加 **double-release 保护**（原先 `DEBUGASSERT` 会被触发） |
| `vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c` | 文件末新增 `sf32lb52_bt_get_driver()` 导出驱动 |
| `vendor/sifli/boards/.../configs/nsh/defconfig` | `CONFIG_UART_BTH4=y`、`CONFIG_DEBUG_WIRELESS*`、关闭 `LV_USE_DEMO_WIDGETS` 等 |
| `vendor/sifli/boards/.../src/etc/init.d/rcS` | `sleep 2` + `mood_anchor &`（C 预处理，**不能写 `#` 注释**） |

---

## 8. 踩坑清单（**请务必阅读，可省大量时间**）

1. **不要用 `sizeof(struct)` 当 HCI 命令参数长度** —— 结构体填充会让长度虚高，控制器直接拒绝（曾导致广告数据被丢弃，手机搜不到）
2. **NuttX 的 fd 表是 per-task-group 的** —— 一个线程 open 的 fd，另一个线程组看不到（曾导致 `write()` 返回 EBADF）
3. **绝不能在中断上下文 `printf`** —— `LCPU2HCPU_IRQHandler` 里打印会在 `semaphore.h` 触发断言
4. **1 Mbps 控制台刷日志会冻死整个系统** —— 所有诊断打印都要删干净；`CONFIG_DEBUG_WIRELESS_INFO` 必须保持关闭
5. **`lv_nuttx_init()` 内部会注册自己的日志回调**，覆盖用户先前的注册 → 静默 LVGL 日志必须在它**之后**调用 `lv_log_register_print_cb()`
6. **`lv_label_set_text_fmt()` 每次都会重新分配字符串并让控件失效**，哪怕文字没变 → 高频刷新路径必须先比较再更新（这是 UI 卡顿的主因之一）
7. **BLE 规范：连接建立后控制器自动停止广播，断开后不会恢复**；NuttX 主机侧 `adv_enable` 标志仍为 1，协议栈不会自己重开 → 必须在断开时重新下发广播
8. **不要在广播 flags 上写 `0x02`**（假装支持 BR/EDR）—— 会让手机去连不存在的经典蓝牙，连接直接失败
9. **HPWORK 可能被厂商 IPC 线程占用**，`work_queue()` 返回成功但 worker 永不执行 → 关键 HCI 事件要走同步处理
10. **烧录 EIO 是常态**，重试即可；连续失败就拔插 USB

---

## 9. 建议的下一步

1. **先烧录 `d0e9cdb`**（这是当前唯一未落地的改动），验证：
   - LINK 页开关与状态一致
   - nRF Connect 能重新连上
   - VITALS 页手指贴传感器后是否出现真实心率
2. **系统蓝牙问题转向 App 方案**，不要在表端继续投入
3. 若要区分多台设备 → 在 `bt_hcicore.c` 增加 BD_ADDR 导出接口（当前 `hs_ble_host_bdaddr()` 返回 NULL）
4. 对接 Android 端 `WatchBleService`：扫描 → 连接 → 订阅 `...0002`/`...0004`/`...0005` → 触发通知

---

## 10. 提交历史（近期）

```
d0e9cdb  fix(ble): 回滚广播 flags 到 0x06，修正 BLE 开关与状态机不同步   ← 未烧录
5f8d5f8  fix(ui): LVGL 日志回调被 NuttX 端口覆盖，改到 lv_nuttx_init() 之后注册
de8c5bc  feat(vitals): 用真实 PPG 算法替换占位心率，新增 hs_ppg 模块
53ee1d7  perf(ui): 消除无谓重绘与串口日志，提升界面流畅度
2b16d55  feat(ble): 开机自动广播，并调整广播 flags 提高系统蓝牙可见性   ← flags 0x02 的引入点
5fd9d8b  fix(ble): 断连后广播未恢复导致无法重连，并完善状态上报
7108431  feat(ble): 新增状态聚合特征 d38a0005，广播包携带完整设备名
4db1491  BLE: 采用 openvela 官方 BTH4 架构，连接成功          ← 里程碑
ea1001f  docs: 记录官方传输层方案的两个 nuttx 外部补丁
afbd0e5  BLE: 改用官方 HCI-UART 传输层替换自制 H4 实现
dd4fbf0  BLE: 修复 ACL 头缺失与启动并发崩溃
08ecada  docs: 外部补丁清单 PATCHES.md；BLE 问题报告更新最新进展
```
