package com.moodanchor.app

import android.os.Bundle
import android.graphics.Color
import android.widget.CalendarView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.moodanchor.app.MoodUi.dp
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.format.DateTimeFormatter

class MoodCalendarActivity : AppCompatActivity() {
    private lateinit var summary: TextView
    private lateinit var details: TextView
    private val zone: ZoneId = ZoneId.systemDefault()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Color.parseColor(MoodUi.BACKGROUND)

        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(22), dp(36), dp(22), dp(36))
        }
        content.addView(MoodUi.body(this, "情绪回顾", 13f))
        content.addView(MoodUi.title(this, "情绪日历", 28f).apply { setPadding(0, dp(6), 0, dp(6)) })
        content.addView(MoodUi.body(this, "选择日期，查看当天的情绪激动次数。", 15f).apply { setPadding(0, 0, 0, dp(18)) })

        val calendar = CalendarView(this)
        val calendarCard = MoodUi.card(this)
        calendarCard.addView(calendar)
        content.addView(calendarCard)
        content.addView(MoodUi.space(this, 16))
        val detailCard = MoodUi.card(this, MoodUi.PRIMARY_SOFT)
        summary = MoodUi.title(this, "", 19f)
        details = MoodUi.body(this, "", 15f).apply { setPadding(0, dp(12), 0, 0) }
        detailCard.addView(summary)
        detailCard.addView(details)
        content.addView(detailCard)
        setContentView(ScrollView(this).apply { setBackgroundColor(Color.parseColor(MoodUi.BACKGROUND)); addView(content) })

        showDay(LocalDate.now())
        calendar.setOnDateChangeListener { _, year, month, day ->
            showDay(LocalDate.of(year, month + 1, day))
        }
    }

    private fun showDay(date: LocalDate) {
        val events = EventStore(this).all().filter {
            Instant.ofEpochMilli(it.timestamp).atZone(zone).toLocalDate() == date
        }.filter { it.quality != "pending_watch" && it.quality != "false_positive_watch" }
        val dateText = date.format(DateTimeFormatter.ofPattern("yyyy年M月d日"))
        summary.text = "$dateText · 情绪激动 ${events.size} 次"
        details.text = if (events.isEmpty()) {
            "当天没有记录。愿你拥有平稳的一天。"
        } else {
            events.joinToString("\n\n") {
                val time = Instant.ofEpochMilli(it.timestamp).atZone(zone).format(DateTimeFormatter.ofPattern("HH:mm:ss"))
                "• $time　检测分数 ${"%.2f".format(it.score)}"
            }
        }
    }
}
