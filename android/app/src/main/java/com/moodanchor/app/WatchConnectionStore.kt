package com.moodanchor.app

import android.content.Context

data class WatchInfo(
    val connected: Boolean,
    val name: String,
    val address: String,
    val rssi: Int?,
    val battery: Int?,
    val gsr: Int? = null,
    val heartRate: Int? = null,
    val spo2: Int? = null,
)

class WatchConnectionStore(context: Context) {
    private val prefs = context.getSharedPreferences("watch_connection", Context.MODE_PRIVATE)
    fun current() = WatchInfo(
        prefs.getBoolean("connected", false),
        prefs.getString("name", "黄山派手表") ?: "黄山派手表",
        prefs.getString("address", "尚未获取") ?: "尚未获取",
        if (prefs.contains("rssi")) prefs.getInt("rssi", 0) else null,
        if (prefs.contains("battery")) prefs.getInt("battery", 0) else null,
        if (prefs.contains("gsr")) prefs.getInt("gsr", 0) else null,
        if (prefs.contains("heart_rate")) prefs.getInt("heart_rate", 0) else null,
        if (prefs.contains("spo2")) prefs.getInt("spo2", 0) else null,
    )
    fun connected(name: String, address: String, rssi: Int, battery: Int?) {
        prefs.edit().putBoolean("connected", true).putString("name", name).putString("address", address).putInt("rssi", rssi).apply()
        battery?.let { prefs.edit().putInt("battery", it).apply() }
    }
    fun telemetry(gsr: Int?, heartRate: Int?, spo2: Int?, battery: Int?) {
        prefs.edit().apply {
            gsr?.let { putInt("gsr", it) }
            heartRate?.let { putInt("heart_rate", it) }
            spo2?.let { putInt("spo2", it) }
            battery?.let { putInt("battery", it) }
        }.apply()
    }
    fun disconnected() = prefs.edit().putBoolean("connected", false).apply()
}
