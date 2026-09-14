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
| **Event** | `d38a0002-…` | Notify | 11 字节事件包（跌倒/SOS 等离散事件） |
| **Control** | `d38a0003-…` | Write | 手机下行命令，1–16 字节 |
| **Data** | `d38a0004-…` | Notify | 17 字节：传感器样本 + 融合后的情绪判定 |

只有一个周期推送的特征：**原始值和结论在同一包里**。末字节是结论
（是否激动 + 置信度），前 16 字节是得出它的那三路信号。

早期版本把结论单独放在 `d38a0005` 上，拆包拆出了一个 12 字节的 Status
包和第二个通知，实际用不上它的时间戳与三个 score，反而让每秒多走一趟
发送路径，已合并。

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

### 3.2 Data（`…0004`，17 字节）—— 传感器样本 + 情绪判定

实时传感器样本，**约 1 Hz** 推送。这是"简洁发一下"的那包：四个传感器的当前值
各占几个字节，不含任何统计量，末字节是融合结论。

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 1 | version | 固定 `0x01` |
| 1 | 1 | flags | 见下 |
| 2 | 2 | GSR | 皮肤电，毫伏（uint16 LE） |
| 4 | 1 | mic level | 麦克风音量，0–100（**相对值**） |
| 5 | 1 | battery | 电量百分比；`0xff` = 未知 |
| 6 | 1 | heart rate | bpm，`0` = 无效 |
| 7 | 1 | SpO2 | 百分比，`0` = 无效 |
| 8 | 2 | accel X | 毫克（int16 **有符号** LE） |
| 10 | 2 | accel Y | 毫克（int16 LE） |
| 12 | 2 | accel Z | 毫克（int16 LE） |
| 14 | 2 | gyro | 角速度**幅值**，0.1°/s（uint16 LE） |
| 16 | 1 | **mood** | bit7 = 激动，bit0–6 = 置信度 0–100 |

flags 位定义：

| 位 | 值 | 含义 |
|---|---|---|
| 0 | `0x01` | GSR 有效 |
| 1 | `0x02` | 麦克风有效 |
| 2 | `0x04` | 电量有效 |
| 3 | `0x08` | 心率有效 |
| 4 | `0x10` | 血氧有效 |
| 5 | `0x20` | IMU 有效 |
| 6 | `0x40` | 皮电就绪（已佩戴且已采基线） |

末字节把结论和它依据的原始值放在同一个包、同一个时刻：`bit7` 单独就能
回答"激不激动"，不看低位的置信度也能用。

> **无效的字段会被填写为 0**，所以不看 flags 会把"没有数据"当成真实的 0。
> 麦克风的 0–100 是**相对刻度**（详见 §3.5），不是声压级。

### 3.3 融合后的"激动"是怎么判出来的

结论就在 Data 包的第 16 字节，不需要额外特征。判定细节见 §3.5。

### 3.4 Control（`…0003`，手机 → 手表）

**Write（带响应）**。第 0 字节是命令码：

| 字节 0 | 命令 | 后续 |
|---|---|---|
| `0x01` | ACK | 无（确认收到最后一个事件） |
| `0x02` | CLEAR | 无（清除当前挂起的事件） |
| `0x03` | INTERVAL | 字节 1 = 上报周期（秒） |

### 3.5 "激动"是怎么判出来的

三个传感器各自先算出一个 0–100 的**置信分**：

| 传感器 | 分怎么来 | 为什么这样定 |
|---|---|---|
| **IMU** | 加速度**矢量幅值**偏离 1 g 的量 + 角速度幅值，除以满量程（3000） | 用幅值而不是单轴，手表怎么戴都不影响；取绝对值，所以"掉落瞬间失去 g"和"甩动"一样算运动 |
| **麦克风** | 超过底噪门限（45）后线性映射，85 记满分 | 低于门限直接记 0，房间噪声不参与融合 |
| **皮电** | `(基线 − 当前)` 除以 `2×容差带` | 恒流驱动，**电导升高 = 电压下降**，所以只有电压低于基线才算激动；超过 2 倍容差带记满分 |

IMU 和麦克风的分各做一次**漏积分平滑**（时间常数约 3 秒 / 2 秒），这就是"**剧烈变化**"
和"**持续高值**"的落实 —— 单次抖动或一声巨响会被平滑稀释掉。皮电本身时间常数就长，
不再二次滤波。

然后：

```
融合置信度 = (逼电分×40 + IMU分×30 + 麦克风分×30) / 100

激动 = (融合置信度 >= 55) 且 (三路中至少 2 路判定阳性，即分数 >= 50)
```

**两个条件是关键**：融合分高说明"强度够"，路数够说明"不止一种传感器注意到了"。
同时要求两者，单路传感器再怎么飙也点不亮这个标志 —— 这正是"多传感器融合"的意义，
也是为什么表被磕一下、门被摔一下、电极松一下都不会被报成情绪事件。

确认后至少维持 5 秒（消抖），避免在阈值附近来回跳导致手机反复通知。

阈值和权重都集中在 `app/huangshan_hal/hs_mood.h` 顶部的宏里，可以直接调。

> **说明**：这是**基于规则**的融合，不是训练出来的模型。共享目录里 `mood_gate`
> 那套冻结模型来自两个不同数据集（PAMAP2 的 IMU、WESAD 的 EDA），其 README 自己
> 声明不能宣称端到端准确率，且平台层是 `-ENOSYS` 桩。在拿到同源同步数据做盲测之前，
> 一套**透明可调**的规则比一个来路不明的概率数字更诚实。

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
6. 对 0004 一个特征：
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
val CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

private fun le16(b: ByteArray, o: Int) =
    (b[o].toInt() and 0xFF) or ((b[o + 1].toInt() and 0xFF) shl 8)

private fun sle16(b: ByteArray, o: Int) = le16(b, o).toShort().toInt()

private fun le32(b: ByteArray, o: Int) =
    le16(b, o) or (le16(b, o + 2) shl 16)

override fun onCharacteristicChanged(
    gatt: BluetoothGatt, ch: BluetoothGattCharacteristic, value: ByteArray
) {
    when (ch.uuid) {

        CH_DATA -> {
            if (value.size < 17 || value[0] != 0x01.toByte()) return
            val f = value[1].toInt() and 0xFF

            val gsr      = if (f and 0x01 != 0) le16(value, 2) else null
            val micLevel = if (f and 0x02 != 0) value[4].toInt() and 0xFF else null
            val battery  = if (f and 0x04 != 0) value[5].toInt() and 0xFF else null
            val hr       = if (f and 0x08 != 0) value[6].toInt() and 0xFF else null
            val spo2     = if (f and 0x10 != 0) value[7].toInt() and 0xFF else null

            if (f and 0x20 != 0) {
                val ax = sle16(value, 8)
                val ay = sle16(value, 10)
                val az = sle16(value, 12)
                val gyroDps = le16(value, 14) / 10.0   // 角速度幅值，单位 °/s
            }

            // 同一个包里的结论，直接拿去触发通知
            val mood       = value[16].toInt() and 0xFF
            val agitated   = (mood and 0x80) != 0
            val confidence = mood and 0x7F
            val gsrReady   = (f and 0x40) != 0
            // if (agitated) 触发“疑似情绪激动”通知
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
- **Data 是约 1 Hz 的周期推送**，不是每个样本都推。想改频率可以写 Control
  的 `0x03` 命令。
- **无效字段会被填成 0**，必须看 flags 再决定用不用。麦克风、皮电在没有数据时和
  真实的 0 长得一模一样。
- **麦克风的 0–100 是相对刻度**，不是声压级。麦克风偏置、灵敏度、增益个体差异很大，
  这个数只对**它自己的历史**有意义。判据是 `WIRING.md` 里的实测标定。
- **GSR 的"是否佩戴"是手表本地判定的**，蓝牙发的是原始毫伏值。判据是**电压高于
  1500 mV 表示未佩戴**（详见 `WIRING.md` §3.1）—— 手机若要自己显示佩戴状态，
  需要按同样的阈值判断，否则会和表端不一致。
- **"激动"这个结论是手表算的**（规则见 §3.5），就在 Data 包的第 16 字节。
  如果手机想用自己的模型复算，同一包里就有原始三路信号。
- **Event 包是 fire-and-forget**：除非 flags 的 bit0 置位，否则不需要回 ACK。
- **改协议要注意版本字节**：两个包的 byte 0 都是版本号，格式变了就改它，手机端可以
  据此拒绝解析老固件发来的包。
