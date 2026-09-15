package com.moodanchor.app

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import java.util.ArrayDeque
import java.util.UUID

/** BLE Central implementation of the SF32LB52 / openvela GATT protocol. */
class WatchBleService : Service() {
    private val store by lazy { WatchConnectionStore(this) }
    private val bluetoothManager by lazy { getSystemService(BluetoothManager::class.java) }
    private var scanner: BluetoothLeScanner? = null
    private var gatt: BluetoothGatt? = null
    private var connectingName = "是非钟手表"
    private val subscribeQueue = ArrayDeque<BluetoothGattCharacteristic>()
    private var fusedTensionActive = false

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_SCAN -> startScan()
            ACTION_DISCONNECT -> disconnect()
        }
        return START_NOT_STICKY
    }

    private fun startScan() {
        if (!hasBlePermission()) {
            publish("请先授权附近设备权限")
            return
        }
        val adapter = bluetoothManager.adapter
        if (adapter == null || !adapter.isEnabled) {
            publish("请先打开手机蓝牙")
            return
        }
        stopScan()
        startForeground(NOTIFICATION_ID, notification("正在扫描附近的手表…"))
        scanner = adapter.bluetoothLeScanner
        scanner?.startScan(null, ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCallback)
        publish("正在扫描“是非钟-xxxx”…")
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            // The watch puts its custom UUID in the scan response. Some Android
            // stacks deliver the advertising packet before the scan response, so
            // never require a name before checking the advertised service UUID.
            val record = result.scanRecord
            val advertisedName = record?.deviceName ?: runCatching { result.device.name }.getOrNull()
            val hasService = record?.serviceUuids?.any { it.uuid == SERVICE_UUID } == true
            if (advertisedName?.startsWith(NAME_PREFIX) != true && !hasService) return
            connectingName = advertisedName ?: "是非钟手表"
            stopScan()
            publish("已发现 $connectingName，正在连接…")
            gatt?.close()
            gatt = result.device.connectGatt(this@WatchBleService, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        }

        override fun onScanFailed(errorCode: Int) {
            publish("扫描失败（代码 $errorCode）")
            stopForeground(STOP_FOREGROUND_REMOVE)
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothGatt.STATE_CONNECTED) {
                publish("已连接，正在发现服务…")
                gatt.discoverServices()
            } else if (newState == BluetoothGatt.STATE_DISCONNECTED || status != BluetoothGatt.GATT_SUCCESS) {
                store.disconnected()
                publish("手表已断开")
                gatt.close()
                if (this@WatchBleService.gatt === gatt) this@WatchBleService.gatt = null
                stopForeground(STOP_FOREGROUND_REMOVE)
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            val service = gatt.getService(SERVICE_UUID)
            if (status != BluetoothGatt.GATT_SUCCESS || service == null) {
                publish("未找到是非钟服务，请确认手表固件协议")
                gatt.disconnect()
                return
            }
            subscribeQueue.clear()
            listOf(EVENT_UUID, DATA_UUID, STATUS_UUID).mapNotNull { service.getCharacteristic(it) }
                .filter { it.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY != 0 }
                .forEach { subscribeQueue.add(it) }
            subscribeNext(gatt)
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) publish("数据订阅失败（代码 $status）")
            subscribeNext(gatt)
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            parseNotification(gatt, characteristic.uuid, characteristic.value ?: return)
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            parseNotification(gatt, characteristic.uuid, value)
        }
    }

    private fun subscribeNext(gatt: BluetoothGatt) {
        val characteristic = subscribeQueue.pollFirst()
        if (characteristic == null) {
            val device = gatt.device
            store.connected(connectingName, device.address, 0, null)
            publish("$connectingName 已连接，实时数据通道已启用")
            return
        }
        val descriptor = characteristic.getDescriptor(CCCD_UUID)
        if (descriptor == null || !gatt.setCharacteristicNotification(characteristic, true)) {
            subscribeNext(gatt)
            return
        }
        descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        if (!gatt.writeDescriptor(descriptor)) subscribeNext(gatt)
    }

    private fun parseNotification(gatt: BluetoothGatt, uuid: UUID, value: ByteArray) {
        when (uuid) {
            DATA_UUID -> if (value.size >= 16) {
                val flags = u8(value[4])
                val fusedEmotion = u8(value[15])
                val fusedTension = fusedEmotion and 0x80 != 0
                val confidence = (fusedEmotion and 0x7F) / 100f
                store.telemetry(
                    gsr = if (flags and 0x01 != 0) le16(value, 0) else null,
                    heartRate = u8(value[2]).takeIf { it != 0 },
                    spo2 = u8(value[3]).takeIf { it != 0 },
                    battery = u8(value[6]).takeIf { flags and 0x10 != 0 && it != 0xFF },
                )
                // A continuous Data notification is not itself a confirmed event.
                // Save one pending candidate on the rising edge, then wait for the
                // wearer's 0x07 / 0x08 decision from the watch confirmation dialog.
                if (fusedTension && !fusedTensionActive) {
                    EventStore(this).savePendingWatchEvent(confidence)
                    publish("检测到情绪波动，等待手表端确认")
                } else if (!fusedTension) {
                    fusedTensionActive = false
                }
                if (fusedTension) fusedTensionActive = true
            }
            STATUS_UUID -> if (value.size >= 16 && value[0] == 0x01.toByte()) {
                val flags = u8(value[1])
                val battery = u8(value[12]).takeIf { flags and 0x10 != 0 && it != 0xFF }
                store.telemetry(
                    gsr = if (flags and 0x01 != 0) le16(value, 2) else null,
                    heartRate = if (flags and 0x02 != 0) u8(value[4]) else null,
                    spo2 = if (flags and 0x04 != 0) u8(value[5]) else null,
                    battery = battery,
                )
                publish("手表状态已更新")
            }
            EVENT_UUID -> if (value.size >= 11 && value[0] == 0x01.toByte()) {
                val type = u8(value[1])
                val confidence = u8(value[9])
                if (u8(value[10]) and 0x01 != 0) writeAck(gatt)
                when (type) {
                    0x07 -> {
                        val event = EventStore(this).confirmLatestWatchEvent(confidence / 100f)
                        EventNotifier.notify(this, event)
                        BluetoothAudioAlert.playForConfirmedEvent(this)
                        publish("手表已确认情绪事件，已记录并发送提醒")
                    }
                    0x08 -> {
                        EventStore(this).markLatestWatchEventFalsePositive()
                        publish("手表已标记本次情绪事件为误报")
                    }
                    else -> publish("收到手表事件：${eventName(type)}（置信度 $confidence%）")
                }
            }
        }
    }

    private fun writeAck(gatt: BluetoothGatt) {
        val control = gatt.getService(SERVICE_UUID)?.getCharacteristic(CONTROL_UUID) ?: return
        control.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        control.value = byteArrayOf(0x01)
        gatt.writeCharacteristic(control)
    }

    private fun disconnect() {
        stopScan()
        gatt?.disconnect()
        gatt?.close()
        gatt = null
        store.disconnected()
        publish("已断开手表")
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun stopScan() {
        if (hasBlePermission()) scanner?.stopScan(scanCallback)
        scanner = null
    }

    private fun hasBlePermission(): Boolean = Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
        checkSelfPermission(android.Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED

    private fun publish(message: String) {
        sendBroadcast(Intent(ACTION_STATE).setPackage(packageName).putExtra(EXTRA_MESSAGE, message))
    }

    private fun notification(text: String) = NotificationCompat.Builder(this, CHANNEL_ID)
        .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
        .setContentTitle("是非钟手表连接")
        .setContentText(text)
        .setOngoing(true)
        .build()

    private fun createChannel() {
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "是非钟手表连接", NotificationManager.IMPORTANCE_LOW),
        )
    }

    override fun onCreate() {
        super.onCreate()
        createChannel()
    }

    override fun onDestroy() {
        stopScan()
        gatt?.close()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun u8(value: Byte) = value.toInt() and 0xFF
    private fun le16(bytes: ByteArray, offset: Int) = u8(bytes[offset]) or (u8(bytes[offset + 1]) shl 8)
    private fun eventName(type: Int) = when (type) {
        0x01 -> "跌倒"
        0x02 -> "SOS 按键"
        0x03 -> "心率异常"
        0x04 -> "血氧过低"
        0x05 -> "睡眠事件"
        0x06 -> "电量低"
        0xFF -> "自检 / 台架测试"
        else -> "未知事件（0x${type.toString(16)}）"
    }

    companion object {
        const val ACTION_SCAN = "com.moodanchor.app.action.SCAN_WATCH"
        const val ACTION_DISCONNECT = "com.moodanchor.app.action.DISCONNECT_WATCH"
        const val ACTION_STATE = "com.moodanchor.app.action.WATCH_STATE"
        const val EXTRA_MESSAGE = "watch_state_message"
        // 固件编号会变化，只匹配产品名前缀；随后仍以主服务 UUID 作兜底校验。
        private const val NAME_PREFIX = "是非钟-"
        private const val CHANNEL_ID = "moodanchor_watch_connection"
        private const val NOTIFICATION_ID = 4101
        private val SERVICE_UUID = UUID.fromString("d38a0001-1234-5678-9abc-def012345678")
        private val EVENT_UUID = UUID.fromString("d38a0002-1234-5678-9abc-def012345678")
        private val CONTROL_UUID = UUID.fromString("d38a0003-1234-5678-9abc-def012345678")
        private val DATA_UUID = UUID.fromString("d38a0004-1234-5678-9abc-def012345678")
        private val STATUS_UUID = UUID.fromString("d38a0005-1234-5678-9abc-def012345678")
        private val CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
