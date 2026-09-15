package com.moodanchor.app

import android.graphics.Color
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.activity.OnBackPressedCallback
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.moodanchor.app.MoodUi.dp
import java.util.UUID

class ChatActivity : AppCompatActivity() {
    private lateinit var gateway: CozeGateway
    private lateinit var messages: LinearLayout
    private lateinit var scroll: ScrollView
    private lateinit var clientId: String
    private lateinit var chatPreferences: android.content.SharedPreferences

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        chatPreferences = getSharedPreferences("chat_preferences", MODE_PRIVATE)
        // Earlier builds could save a provider selection. The app is now Coze-only.
        chatPreferences.edit().remove("provider").apply()
        clientId = chatPreferences.getString("client_id", null) ?: UUID.randomUUID().toString().also {
            chatPreferences.edit().putString("client_id", it).apply()
        }
        gateway = createGateway()
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() = confirmExitConversation()
        })
        window.statusBarColor = Color.parseColor(MoodUi.BACKGROUND)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(18), dp(30), dp(18), dp(18)); setBackgroundColor(Color.parseColor(MoodUi.BACKGROUND)) }
        ViewCompat.setOnApplyWindowInsetsListener(root) { view, insets ->
            val bottomInset = maxOf(
                insets.getInsets(WindowInsetsCompat.Type.ime()).bottom,
                insets.getInsets(WindowInsetsCompat.Type.systemBars()).bottom,
            )
            view.setPadding(dp(18), dp(30), dp(18), maxOf(dp(18), bottomInset + dp(8)))
            insets
        }
        val header = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        val exit = TextView(this).apply {
            text = "‹"; textSize = 38f; gravity = Gravity.CENTER; setTextColor(Color.parseColor(MoodUi.INK))
            contentDescription = "退出当前对话"
            setOnClickListener { confirmExitConversation() }
        }
        val headings = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; addView(MoodUi.body(this@ChatActivity, "安心空间", 13f)); addView(MoodUi.title(this@ChatActivity, "陪伴对话", 27f).apply { setPadding(0, dp(5), 0, 0) }) }
        header.addView(exit, LinearLayout.LayoutParams(dp(34), dp(54)))
        header.addView(MoodUi.space(this, 4), LinearLayout.LayoutParams(dp(4), 1))
        header.addView(headings, LinearLayout.LayoutParams(0, -2, 1f))
        root.addView(header)
        root.addView(MoodUi.space(this, 16))
        messages = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(0, dp(4), 0, dp(12)) }
        scroll = ScrollView(this).apply { isFillViewport = true; addView(messages) }
        root.addView(scroll, LinearLayout.LayoutParams(-1, 0, 1f))
        addBubble("我在这里。你不需要立刻变好，可以慢慢说。", false)

        val composer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(14), dp(8), dp(10), dp(10))
            background = MoodUi.rounded(MoodUi.SURFACE, 24, this@ChatActivity, "#E2E7E4")
            elevation = dp(2).toFloat()
        }
        val input = EditText(this).apply {
            hint = "随心输入"; textSize = 16f; minLines = 2; maxLines = 4; gravity = Gravity.TOP
            setTextColor(Color.parseColor(MoodUi.INK)); setHintTextColor(Color.parseColor("#A6AEAA"))
            setPadding(dp(4), dp(7), dp(4), dp(4)); background = null
        }
        val actionRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(dp(2), dp(3), 0, 0) }
        val providerLabel = MoodUi.body(this, "Coze · 陪伴对话", 13f).apply {
            setPadding(dp(10), dp(9), dp(10), dp(9))
            background = MoodUi.rounded("#F5F7F4", 16, this@ChatActivity)
            contentDescription = "当前对话服务：Coze"
        }
        val send = TextView(this).apply {
            text = "↑"; textSize = 25f; gravity = Gravity.CENTER; setTextColor(Color.WHITE)
            background = MoodUi.rounded(MoodUi.PRIMARY, 22, this@ChatActivity)
            contentDescription = "发送"
            isClickable = true
        }
        actionRow.addView(MoodUi.body(this, "", 1f), LinearLayout.LayoutParams(0, 1, 1f))
        actionRow.addView(providerLabel)
        actionRow.addView(MoodUi.space(this, 8), LinearLayout.LayoutParams(dp(8), 1))
        actionRow.addView(send, LinearLayout.LayoutParams(dp(44), dp(44)))
        composer.addView(input, LinearLayout.LayoutParams(-1, -2))
        composer.addView(actionRow, LinearLayout.LayoutParams(-1, -2))
        root.addView(composer); setContentView(root)
        ViewCompat.requestApplyInsets(root)

        send.setOnClickListener {
            val message = input.text.toString().trim(); if (message.isEmpty()) return@setOnClickListener
            addBubble(message, true); input.setText(""); send.isEnabled = false
            val pending = addBubble("正在认真听你说…", false)
            Thread {
                val result = runCatching { gateway.reply(EventStore(this).latest(), message) }
                runOnUiThread {
                    result.onSuccess {
                        pending.text = it.text
                        addModelMetadata(it)
                    }.onFailure {
                        pending.text = "暂时无法连接模型：${it.message ?: "网络异常"}"
                    }
                    send.isEnabled = true
                    scroll.post { scroll.fullScroll(ScrollView.FOCUS_DOWN) }
                }
            }.start()
        }
    }

    private fun confirmExitConversation() {
        AlertDialog.Builder(this)
            .setTitle("退出当前对话？")
            .setMessage("退出后，本次 Coze 对话上下文将结束；下次进入会创建新的对话。")
            .setNegativeButton("继续对话", null)
            .setPositiveButton("退出") { _, _ ->
                chatPreferences.edit()
                    .remove("coze_conversation_id")
                    .putString("client_id", UUID.randomUUID().toString())
                    .putBoolean("coze_start_new_conversation", true)
                    .apply()
                finish()
            }
            .show()
    }

    private fun addModelMetadata(reply: ChatReply) {
        val text = "bot_id: ${reply.botId ?: "—"}\nconversation_id: ${reply.conversationId ?: "—"}"
        val line = LinearLayout(this).apply {
            gravity = Gravity.START
            setPadding(dp(4), 0, 0, dp(7))
        }
        line.addView(MoodUi.body(this, text, 10f).apply { setLineSpacing(0f, 1.05f) })
        messages.addView(line)
    }

    private fun createGateway() = CozeGateway(
        clientId = clientId,
        conversationId = chatPreferences.getString("coze_conversation_id", null),
        startNewConversation = chatPreferences.getBoolean("coze_start_new_conversation", false),
        onConversationId = { id ->
            chatPreferences.edit()
                .putString("coze_conversation_id", id)
                .remove("coze_start_new_conversation")
                .apply()
        },
    )

    private fun addBubble(text: String, fromUser: Boolean): TextView {
        val line = LinearLayout(this).apply { gravity = if (fromUser) Gravity.END else Gravity.START; setPadding(0, dp(5), 0, dp(5)) }
        val bubble = TextView(this).apply {
            this.text = text; textSize = 16f; setLineSpacing(0f, 1.15f)
            maxWidth = (resources.displayMetrics.widthPixels * .76f).toInt()
            // Enable Android's standard long-press selection and copy toolbar.
            setTextIsSelectable(true)
            setTextColor(Color.parseColor(if (fromUser) "#FFFFFF" else MoodUi.INK))
            setPadding(dp(16), dp(12), dp(16), dp(12))
            background = MoodUi.rounded(if (fromUser) MoodUi.PRIMARY else MoodUi.SURFACE, 19, this@ChatActivity)
        }
        line.addView(bubble, LinearLayout.LayoutParams(-2, -2))
        messages.addView(line)
        scroll.post { scroll.fullScroll(ScrollView.FOCUS_DOWN) }
        return bubble
    }
}
