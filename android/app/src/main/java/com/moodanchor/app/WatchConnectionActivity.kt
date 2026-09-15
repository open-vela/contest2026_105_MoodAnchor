package com.moodanchor.app

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.moodanchor.app.MoodUi.dp

class WatchConnectionActivity : AppCompatActivity() {
    private var status = "点击下方按钮扫描附近的手表。"
    private var receiverRegistered = false

    private val requestBlePermissions = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { permissions ->
        if (permissions.values.all { it }) startScan() else {
            status = "需要“附近设备”权限才能扫描和连接手表。"
            render()
        }
    }

    private val stateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            status = intent.getStringExtra(WatchBleService.EXTRA_MESSAGE) ?: status
            render()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Color.parseColor(MoodUi.BACKGROUND)
        render()
    }

    override fun onStart() {
        super.onStart()
        ContextCompat.registerReceiver(
            this,
            stateReceiver,
            IntentFilter(WatchBleService.ACTION_STATE),
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
        receiverRegistered = true
        render()
    }

    override fun onStop() {
        if (receiverRegistered) unregisterReceiver(stateReceiver)
        receiverRegistered = false
        super.onStop()
    }

    private fun render() {
        val info = WatchConnectionStore(this).current()
        val content = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(24), dp(38), dp(24), dp(36)) }
        content.addView(MoodUi.body(this, "设备管理", 13f))
        content.addView(MoodUi.title(this, "我的手表", 29f).apply { setPadding(0, dp(6), 0, dp(22)) })

        val hero = MoodUi.card(this, MoodUi.PRIMARY_SOFT).apply { gravity = android.view.Gravity.CENTER_HORIZONTAL }
        hero.addView(ImageView(this).apply {
            setImageResource(R.drawable.ic_watch)
            imageTintList = android.content.res.ColorStateList.valueOf(Color.parseColor(MoodUi.PRIMARY))
        }, LinearLayout.LayoutParams(dp(92), dp(92)))
        hero.addView(MoodUi.title(this, info.name, 21f).apply { setPadding(0, dp(12), 0, dp(6)) })
        hero.addView(MoodUi.body(this, if (info.connected) "● 已通过蓝牙连接" else "○ 当前未连接", 15f).apply {
            setTextColor(Color.parseColor(if (info.connected) MoodUi.PRIMARY else MoodUi.MUTED))
        })
        content.addView(hero)
        content.addView(MoodUi.space(this, 16))

        val details = MoodUi.card(this)
        details.addView(MoodUi.title(this, "设备信息", 19f))
        details.addView(row("通信方式", "Bluetooth Low Energy · GATT"))
        details.addView(row("设备地址", info.address))
        details.addView(row("电量", info.battery?.let { "$it%" } ?: "等待手表上报"))
        details.addView(row("皮电", info.gsr?.let { "$it mV" } ?: "等待手表上报"))
        details.addView(row("心率 / 血氧", listOfNotNull(info.heartRate?.let { "$it bpm" }, info.spo2?.let { "$it%" }).joinToString(" / ").ifBlank { "等待手表上报" }))
        details.addView(row("数据通道", if (info.connected) "Event、Data、Status 已订阅" else "连接后自动订阅"))
        content.addView(details)
        content.addView(MoodUi.space(this, 18))
        content.addView(MoodUi.body(this, status, 13f).apply { setPadding(dp(4), 0, 0, dp(10)); setTextColor(Color.parseColor(MoodUi.MUTED)) })
        content.addView(MoodUi.button(this, if (info.connected) "断开设备" else "扫描并连接手表", false).apply {
            setOnClickListener {
                if (info.connected) {
                    startService(Intent(this@WatchConnectionActivity, WatchBleService::class.java).setAction(WatchBleService.ACTION_DISCONNECT))
                } else if (hasBlePermissions()) {
                    startScan()
                } else {
                    requestBlePermissions.launch(requiredPermissions())
                }
            }
        })
        content.addView(MoodUi.space(this, 14))
        content.addView(MoodUi.body(this, "连接建立后，离开本页不会断开手表；只有点击“断开设备”才会结束连接。手表会以“是非钟-xxxx”形式出现在本 App 扫描中，不会出现在系统蓝牙配对列表。", 12f))
        setContentView(ScrollView(this).apply { setBackgroundColor(Color.parseColor(MoodUi.BACKGROUND)); addView(content) })
    }

    private fun startScan() {
        status = "正在启动蓝牙扫描…"
        render()
        ContextCompat.startForegroundService(
            this,
            Intent(this, WatchBleService::class.java).setAction(WatchBleService.ACTION_SCAN),
        )
    }

    private fun hasBlePermissions() = requiredPermissions().all {
        ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
    }

    private fun requiredPermissions(): Array<String> = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
    } else {
        arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
    }

    private fun row(label: String, value: String) = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL; setPadding(0, dp(16), 0, 0)
        addView(MoodUi.body(this@WatchConnectionActivity, label, 12f))
        addView(MoodUi.body(this@WatchConnectionActivity, value, 15f).apply {
            setTextColor(Color.parseColor(MoodUi.INK)); setPadding(0, dp(3), 0, 0)
        })
    }
}
