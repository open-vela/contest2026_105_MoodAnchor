package com.moodanchor.app

import android.app.*
import android.content.*
import androidx.core.app.NotificationCompat

object EventNotifier {
    const val CHANNEL = "confirmed_event"
    fun notify(context: Context, event: MoodEvent) {
        val manager = context.getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(NotificationChannel(CHANNEL, "情绪关怀提醒", NotificationManager.IMPORTANCE_HIGH))
        val intent = Intent(context, ChatActivity::class.java).putExtra("event_id", event.id)
        val pending = PendingIntent.getActivity(context, event.id.toInt(), intent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val notification = NotificationCompat.Builder(context, CHANNEL)
            .setSmallIcon(android.R.drawable.ic_dialog_info).setContentTitle("是非钟检测到你可能需要休息")
            .setContentText("点击进入舒缓对话，花一点时间关照自己。")
            .setPriority(NotificationCompat.PRIORITY_HIGH).setAutoCancel(true).setContentIntent(pending).build()
        manager.notify(event.id.toInt(), notification)
    }
}
