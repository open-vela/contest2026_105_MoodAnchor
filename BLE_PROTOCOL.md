# MoodAnchor BLE 通信协议

手表（SF32LB52 / openvela）作为 **GATT 外设（peripheral / server）**，手机作为
**GATT 中心（central / client）**。

协议本身**没有私有层** —— 全部走标准 GATT/ATT 流程（服务发现 + CCCD 订阅 +
Notify 推送）。所谓"协议"只是下面约定的 UUID 和几个定长数据包。

对应实现：`app/huangshan_hal/hs_ble_gatt.c`（GATT 表）、`hs_ble.h`（包格式）。

---

## 1. 设备发现

| 项 | 值 |
|---|---|
| 广播名 | `是非钟-XXXX`（XXXX = BD_ADDR 末两字节的十六进制） |
| Appearance | `0x0341`（Wrist-worn watch） |
| 广播间隔 | 空闲 200 ms；事件后 100–200 ms，持续 10 s |
| 广播数据（AD） | Flags(`0x06`) + Appearance(`0x0341`) + Complete Local Name |
| 扫描响应（SD） | 128 位服务 UUID `d38a0001-...` |

> **注意**：SF32LB52 只有 BLE，没有 BR/EDR（经典蓝牙）射频。
> 所以**Android 系统设置里的"蓝牙"列表不会显示它** —— 那个列表来自经典蓝牙的
> inquiry，用 App 扫描（`BluetoothLeScanner`）才能找到。

---

## 2. 服务与特征表

自定义服务的基 UUID：

```
d38a0001-1234-5678-9abc-def012345678
```

| 特征 | UUID | 属性 | 内容 |
|---|---|---|---|
| **Event** | `d38a0002-…` | Notify | 11 字节事件包 |
| **Control** | `d38a0003-…` | Write | 手机下行命令，1–16 字节 |
| **Data** | `d38a0004-…` | Notify | 6 字节实时传感器包 |
| **Status** | `d38a0005-…` | Notify | 16 字节完整状态包 |

另外还装了三个标准服务（可直接用系统 API 读，也可以忽略）：

| 服务 | UUID | 说明 |
|---|---|---|
| Generic Access | `0x1800` | 设备名、Appearance |
| Device Information | `0x180a` | 厂商 / 型号 / 固件版本 / 序列号 / PnP ID |
| Battery Service | `0x180f` | 电量百分比（Read + Notify） |

---

## 3. 数据包格式

**所有多字节整数一律小端（little endian）。**

### 3.1 Event（`…0002`，11 字节）

事件类通知，**不是周期性的** —— 只在发生事件时推送。

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 1 | version | 固定 `0x01` |
| 1 | 1 | event type | 见下表 |
| 2 | 2 | sequence | 递增序号（uint16 LE），用于丢包检测 |
| 4 | 4 | timestamp | 毫秒（uint32 LE） |
| 8 | 1 | risk level | `0`=低 `1`=中 `2`=高 |
| 9 | 1 | confidence | 置信度 0–100 |
| 10 | 1 | flags | `0x01`=要求 ACK；`0x80`=CLI 自测产生 |

event type 取值：

| 值 | 含义 |
|---|---|
| `0x00` | NONE |
| `0x01` | 跌倒 |
| `0x02` | SOS 按键 |
| `0x03` | 心率异常 |
| `0x04` | 血氧过低 |
| `0x05` | 睡眠事件 |
| `0x06` | 电量低 |
| `0xff` | 自检 / 台架测试 |

### 3.2 Data（`…0004`，6 字节）

实时传感器样本，**约 1 Hz** 推送。

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 2 | GSR | 皮肤电，毫伏（uint16 LE） |
| 2 | 1 | heart rate | bpm，`0` = 无效 |
| 3 | 1 | SpO2 | 百分比，`0` = 无效 |
| 4 | 1 | flags | `0x01`=GSR 有效 `0x02`=HR 有效 `0x04`=SpO2 有效 `0x80`=模拟数据 |
| 5 | 1 | reserved | 固定 `0` |

**必须看 flags 再决定用不用该字段** —— 传感器没数据时值为 0。

### 3.3 Status（`…0005`，16 字节）

完整状态快照，**约 1 Hz** 推送，和 Data 包同时发出。

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 1 | version | 固定 `0x01` |
| 1 | 1 | flags | 见下 |
| 2 | 2 | GSR | 毫伏（uint16 LE） |
| 4 | 1 | heart rate | bpm，`0` = 无效 |
| 5 | 1 | SpO2 | 百分比，`0` = 无效 |
| 6 | 2 | accel X | 毫克（int16 **有符号** LE） |
| 8 | 2 | accel Y | 毫克（int16 LE） |
| 10 | 2 | accel Z | 毫克（int16 LE） |
| 12 | 1 | battery | 电量百分比；`0xff` = 未知 |
| 13 | 1 | buttons | 按键位图 |
| 14 | 1 | vibration | 马达 0/1 |
| 15 | 1 | reserved | 固定 `0` |

flags 位定义：

| 位 | 值 | 含义 |
|---|---|---|
| 0 | `0x01` | GSR 有效 |
| 1 | `0x02` | 心率有效 |
| 2 | `0x04` | 血氧有效 |
| 3 | `0x08` | 加速度有效 |
| 4 | `0x10` | 电量有效 |
| 5 | `0x20` | 按键有效 |
| 6 | `0x40` | 马达正在振动 |

### 3.4 Control（`…0003`，手机 → 手表）

**Write（带响应）**。第 0 字节是命令码：

| 字节 0 | 命令 | 后续 |
|---|---|---|
| `0x01` | ACK | 无（确认收到最后一个事件） |
| `0x02` | CLEAR | 无（清除当前挂起的事件） |
| `0x03` | INTERVAL | 字节 1 = 上报周期（秒） |

---

## 4. 手机端接收流程

因为所有包都 ≤ 20 字节，**不需要协商 MTU** —— 默认 ATT MTU 23（有效载荷 20）
就够用。

```
1. BluetoothLeScanner.startScan()          扫描
2. 按名字前缀 "是非钟-" 或服务 UUID 过滤
3. device.connectGatt()                    连接
4. gatt.discoverServices()                 服务发现
5. getService(d38a0001-...)                取自定义服务
6. 对 0004 / 0005 两个特征：
     gatt.setCharacteristicNotification(ch, true)
     再往 CCCD (00002902-0000-1000-8000-00805f9b34fb) 写
     ENABLE_NOTIFICATION_VALUE = {0x01, 0x00}
7. onCharacteristicChanged 里解析
```

**第 6 步两个动作都要做**，缺一个都不会收到通知：`setCharacteristicNotification()`
只是让本地栈打开接收，真正让手表开始推送的是往 CCCD 写 `0x0001`。

### Kotlin 解析示例

```kotlin
val SVC = UUID.fromString("d38a0001-1234-5678-9abc-def012345678")
val CH_DATA   = UUID.fromString("d38a0004-1234-5678-9abc-def012345678")
val CH_STATUS = UUID.fromString("d38a0005-1234-5678-9abc-def012345678")
val CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

private fun le16(b: ByteArray, o: Int) =
    (b[o].toInt() and 0xFF) or ((b[o + 1].toInt() and 0xFF) shl 8)

private fun sle16(b: ByteArray, o: Int) = le16(b, o).toShort().toInt()

override fun onCharacteristicChanged(
    gatt: BluetoothGatt, ch: BluetoothGattCharacteristic, value: ByteArray
) {
    when (ch.uuid) {
        CH_DATA -> {
            if (value.size < 6 || value[0] != 0x01.toByte()) return
            val f = value[4].toInt() and 0xFF
            val gsr = if (f and 0x01 != 0) le16(value, 0) else null
            val hr  = if (f and 0x02 != 0) value[2].toInt() and 0xFF else null
            val spo2= if (f and 0x04 != 0) value[3].toInt() and 0xFF else null
            // ...
        }
        CH_STATUS -> {
            if (value.size < 16) return
            val flags   = value[1].toInt() and 0xFF
            val batt    = value[12].toInt() and 0xFF   // 0xFF = 未知
            val ax = if (flags and 0x08 != 0) sle16(value, 6)  else null
            val ay = if (flags and 0x08 != 0) sle16(value, 8)  else null
            val az = if (flags and 0x08 != 0) sle16(value, 10) else null
            // ...
        }
    }
}
```

> Android 13（API 33）起 `onCharacteristicChanged` 有带 `value: ByteArray` 的
> 新重载，旧的无参版本已被废弃。

---

## 5. 几个容易踩的点

- **不要用系统蓝牙列表找设备**（原因见第 1 节），必须用 App 扫描。
- **断连后手机会停止收到通知**，因为 CCCD 绑定在连接上；重连后需要**重新订阅**。
  手表侧会在断连后自动恢复广播，可以直接重连。
- **Data / Status 是约 1 Hz 的周期推送**，不是每个样本都推；想要更高频率可以
  写 Control 的 `0x03` 命令改上报周期。
- **GSR 的"是否佩戴"是手表本地判定的**，蓝牙发的是原始毫伏值。判据是
  **电压高于 1500 mV 表示未佩戴**（详见 `WIRING.md` §3.1）—— 手机端如果要显示
  佩戴状态，需要自己按这个阈值判断。
- **Event 包是 fire-and-forget**：除非 flags 的 bit0 置位，否则不需要回 ACK。
