package com.moodanchor.app

import android.content.Context

class EventStore(context: Context) {
    private val prefs = context.getSharedPreferences("mood_events", Context.MODE_PRIVATE)
    fun save(score: Float, threshold: Float, quality: String): MoodEvent {
        val event = MoodEvent(System.currentTimeMillis(), System.currentTimeMillis(), score, threshold, quality)
        prefs.edit().putString(event.id.toString(), "${event.timestamp}|${event.score}|${event.threshold}|${event.quality}").apply()
        return event
    }

    fun savePendingWatchEvent(score: Float): MoodEvent = save(score, WATCH_THRESHOLD, "pending_watch")

    fun confirmLatestWatchEvent(score: Float): MoodEvent {
        val pending = all().firstOrNull { it.quality == "pending_watch" }
        return if (pending == null) {
            // The watch confirmation is authoritative even if a Data packet was
            // missed while the phone was reconnecting.
            save(score, WATCH_THRESHOLD, "confirmed_watch")
        } else {
            replace(pending.copy(score = score, quality = "confirmed_watch"))
        }
    }

    fun markLatestWatchEventFalsePositive() {
        all().firstOrNull { it.quality == "pending_watch" }?.let {
            replace(it.copy(quality = "false_positive_watch"))
        }
    }

    private fun replace(event: MoodEvent): MoodEvent {
        prefs.edit().putString(event.id.toString(), "${event.timestamp}|${event.score}|${event.threshold}|${event.quality}").apply()
        return event
    }
    fun latest(): MoodEvent? = prefs.all.keys.maxByOrNull { it.toLong() }?.let { id ->
        prefs.getString(id, null)?.split("|")?.let { p -> MoodEvent(id.toLong(), p[0].toLong(), p[1].toFloat(), p[2].toFloat(), p[3]) }
    }

    fun all(): List<MoodEvent> = prefs.all.mapNotNull { (key, value) ->
        val p = (value as? String)?.split("|") ?: return@mapNotNull null
        runCatching { MoodEvent(key.toLong(), p[0].toLong(), p[1].toFloat(), p[2].toFloat(), p[3]) }.getOrNull()
    }.sortedByDescending { it.timestamp }

    private companion object {
        const val WATCH_THRESHOLD = 0.68f
    }
}
