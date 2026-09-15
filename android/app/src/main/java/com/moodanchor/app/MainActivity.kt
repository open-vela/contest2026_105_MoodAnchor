package com.moodanchor.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Bundle
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.moodanchor.app.MoodUi.dp

class MainActivity : AppCompatActivity() {
    private val requestNotification = registerForActivityResult(ActivityResultContracts.RequestPermission()) { }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Color.parseColor(MoodUi.BACKGROUND)
        window.navigationBarColor = Color.parseColor(MoodUi.BACKGROUND)
        val content = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(24), dp(42), dp(24), dp(36)) }
        content.addView(MoodUi.body(this, "MOOD ANCHOR", 12f).apply { letterSpacing = .14f })
        content.addView(MoodUi.title(this, "今天，也照顾好自己", 30f).apply { setPadding(0, dp(8), 0, dp(8)) })
        content.addView(MoodUi.body(this, "觉察情绪的变化，在需要时停下来聊一聊。", 16f))
        content.addView(MoodUi.space(this, 30))
        val supportCard = MoodUi.card(this, MoodUi.PRIMARY_SOFT)
        supportCard.addView(MoodUi.title(this, "日常心理疏导", 21f))
        supportCard.addView(MoodUi.body(this, "不必等到情绪失控。现在就可以说说你的感受。", 15f).apply { setPadding(0, dp(8), 0, dp(18)) })
        supportCard.addView(MoodUi.button(this, "开始对话").apply { setOnClickListener { startActivity(Intent(this@MainActivity, ChatActivity::class.java)) } })
        content.addView(supportCard)
        content.addView(MoodUi.space(this, 16))
        val calendarCard = MoodUi.card(this)
        calendarCard.addView(MoodUi.title(this, "情绪日历", 21f))
        calendarCard.addView(MoodUi.body(this, "回顾每日情绪激动次数与检测记录。", 15f).apply { setPadding(0, dp(8), 0, dp(18)) })
        calendarCard.addView(MoodUi.button(this, "查看记录", false).apply { setOnClickListener { startActivity(Intent(this@MainActivity, MoodCalendarActivity::class.java)) } })
        content.addView(calendarCard)
        content.addView(MoodUi.space(this, 24))
        val audioCard = MoodUi.card(this)
        audioCard.addView(MoodUi.title(this, "是非钟声", 21f))
        audioCard.addView(MoodUi.body(this, "确认情绪事件后，仅通过已连接的蓝牙耳机播放你的私密钟声；不会从手机扬声器外放。", 15f).apply { setPadding(0, dp(8), 0, dp(18)) })
        audioCard.addView(MoodUi.button(this, "设置钟声", false).apply { setOnClickListener { startActivity(Intent(this@MainActivity, AudioAlertSettingsActivity::class.java)) } })
        content.addView(audioCard)
        content.addView(MoodUi.space(this, 24))
        content.addView(MoodUi.body(this, "设备状态", 13f).apply { setPadding(dp(4), 0, 0, dp(10)) })
        val deviceCard = MoodUi.card(this, MoodUi.WARM)
        deviceCard.addView(MoodUi.title(this, "等待手表连接", 19f))
        deviceCard.addView(MoodUi.body(this, "确认疑似情绪激动后，手机会自动记录并提醒。", 14f).apply { setPadding(0, dp(7), 0, dp(16)) })
        deviceCard.addView(MoodUi.button(this, "查看并连接手表", false).apply { setOnClickListener { startActivity(Intent(this@MainActivity, WatchConnectionActivity::class.java)) } })
        deviceCard.addView(MoodUi.space(this, 10))
        deviceCard.addView(MoodUi.button(this, "模拟一次确认事件", false).apply { setOnClickListener { receiveConfirmedEvent(.74f, .56f, "good") } })
        content.addView(deviceCard)
        content.addView(MoodUi.body(this, "当前为演示模式 · 接入黄山派后由 BLE 自动触发", 12f).apply { setPadding(dp(4), dp(12), 0, 0) })
        setContentView(ScrollView(this).apply { setBackgroundColor(Color.parseColor(MoodUi.BACKGROUND)); isFillViewport = true; addView(content) })
        if (android.os.Build.VERSION.SDK_INT >= 33 && ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) requestNotification.launch(Manifest.permission.POST_NOTIFICATIONS)
    }
    fun receiveConfirmedEvent(score: Float, threshold: Float, quality: String) {
        val event = EventStore(this).save(score, threshold, quality)
        EventNotifier.notify(this, event)
        val played = BluetoothAudioAlert.playForConfirmedEvent(this)
        Toast.makeText(this, if (played) "已记录、提醒，并在耳机中播放" else "已记录并发送提醒", Toast.LENGTH_SHORT).show()
    }
}
