package com.moodanchor.app

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.content.Intent
import android.net.Uri
import androidx.core.content.ContextCompat

/**
 * Plays a user-selected local audio URI only when an active Bluetooth headset
 * is an output. Android then routes playback to the system-selected headset.
 */
object BluetoothAudioAlert {
    private const val PREFS = "bluetooth_audio_alert"
    private const val KEY_ENABLED = "enabled"
    private const val KEY_URI = "audio_uri"

    fun enabled(context: Context): Boolean = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        .getBoolean(KEY_ENABLED, false)

    fun selectedUri(context: Context): Uri? = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        .getString(KEY_URI, null)?.let(Uri::parse)

    fun save(context: Context, enabled: Boolean, uri: Uri?) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putBoolean(KEY_ENABLED, enabled)
            .apply {
                if (uri == null) remove(KEY_URI) else putString(KEY_URI, uri.toString())
            }.apply()
    }

    fun hasBluetoothHeadset(context: Context): Boolean {
        val manager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        return manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS).any { device ->
            when (device.type) {
                AudioDeviceInfo.TYPE_BLUETOOTH_A2DP,
                AudioDeviceInfo.TYPE_BLUETOOTH_SCO,
                AudioDeviceInfo.TYPE_BLE_HEADSET -> true
                else -> false
            }
        }
    }

    /** Starts private playback through the system media session. */
    fun playForConfirmedEvent(context: Context): Boolean {
        val uri = selectedUri(context) ?: return false
        if (!enabled(context) || !hasBluetoothHeadset(context)) return false
        return start(context, uri)
    }

    /** Preview ignores the enable switch, but never falls back to the phone speaker. */
    fun preview(context: Context, uri: Uri?): Boolean {
        if (uri == null || !hasBluetoothHeadset(context)) return false
        return start(context, uri)
    }

    fun stop(context: Context) {
        context.startService(Intent(context, MoodAudioPlaybackService::class.java)
            .setAction(MoodAudioPlaybackService.ACTION_STOP))
    }

    private fun start(context: Context, uri: Uri): Boolean = try {
        ContextCompat.startForegroundService(
            context,
            Intent(context, MoodAudioPlaybackService::class.java)
                .setAction(MoodAudioPlaybackService.ACTION_PLAY)
                .putExtra(MoodAudioPlaybackService.EXTRA_URI, uri.toString()),
        )
        true
    } catch (_: Exception) {
        false
    }
}
