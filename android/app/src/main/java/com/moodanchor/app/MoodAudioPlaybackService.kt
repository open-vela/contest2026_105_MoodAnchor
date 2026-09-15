package com.moodanchor.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.media.AudioAttributes
import android.media.MediaMetadata
import android.media.MediaPlayer
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.net.Uri
import android.os.IBinder

/** Standard Android media session for the system playback control card. */
class MoodAudioPlaybackService : Service() {
    private var player: MediaPlayer? = null
    private lateinit var mediaSession: MediaSession

    override fun onCreate() {
        super.onCreate()
        createChannel()
        mediaSession = MediaSession(this, "MoodAnchorBell").apply {
            setMetadata(MediaMetadata.Builder()
                .putString(MediaMetadata.METADATA_KEY_TITLE, "是非钟正在播放音频")
                .putString(MediaMetadata.METADATA_KEY_ARTIST, "私密钟声 · 仅蓝牙耳机")
                .build())
            setCallback(object : MediaSession.Callback() {
                override fun onPlay() = resumePlayback()
                override fun onPause() = pausePlayback()
                override fun onStop() = stopPlayback()
            })
            isActive = true
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> stopPlayback()
            ACTION_PLAY -> intent.getStringExtra(EXTRA_URI)?.let { play(Uri.parse(it)) } ?: stopPlayback()
        }
        return START_NOT_STICKY
    }

    private fun play(uri: Uri) {
        stopPlayerOnly()
        startForeground(NOTIFICATION_ID, notification("正在准备钟声…"))
        try {
            player = MediaPlayer().apply {
                setAudioAttributes(AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build())
                setDataSource(this@MoodAudioPlaybackService, uri)
                setOnPreparedListener { mediaPlayer ->
                    mediaPlayer.start()
                    updatePlaybackState(PlaybackState.STATE_PLAYING)
                    notifyPlayback("是非钟正在播放音频")
                }
                setOnCompletionListener { stopPlayback() }
                setOnErrorListener { _, _, _ -> stopPlayback(); true }
                prepareAsync()
            }
            updatePlaybackState(PlaybackState.STATE_BUFFERING)
        } catch (_: Exception) {
            stopPlayback()
        }
    }

    private fun notification(content: String): Notification {
        val stopIntent = PendingIntent.getService(
            this, 0,
            Intent(this, MoodAudioPlaybackService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle("是非钟正在播放音频")
            .setContentText(content)
            .setOngoing(true)
            .addAction(Notification.Action.Builder(android.R.drawable.ic_menu_close_clear_cancel, "结束", stopIntent).build())
            .setStyle(Notification.MediaStyle().setMediaSession(mediaSession.sessionToken).setShowActionsInCompactView(0))
            .build()
    }

    private fun notifyPlayback(content: String) {
        getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, notification(content))
    }

    private fun updatePlaybackState(state: Int) {
        val actions = when (state) {
            PlaybackState.STATE_PLAYING -> PlaybackState.ACTION_PAUSE or PlaybackState.ACTION_STOP
            PlaybackState.STATE_PAUSED -> PlaybackState.ACTION_PLAY or PlaybackState.ACTION_STOP
            else -> PlaybackState.ACTION_STOP
        }
        mediaSession.setPlaybackState(PlaybackState.Builder()
            .setState(state, PlaybackState.PLAYBACK_POSITION_UNKNOWN, 1f)
            .setActions(actions)
            .build())
    }

    private fun pausePlayback() {
        player?.takeIf { it.isPlaying }?.pause() ?: return
        updatePlaybackState(PlaybackState.STATE_PAUSED)
        notifyPlayback("是非钟音频已暂停")
    }

    private fun resumePlayback() {
        val current = player ?: return
        if (!current.isPlaying) current.start()
        updatePlaybackState(PlaybackState.STATE_PLAYING)
        notifyPlayback("是非钟正在播放音频")
    }

    private fun stopPlayback() {
        stopPlayerOnly()
        updatePlaybackState(PlaybackState.STATE_STOPPED)
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun stopPlayerOnly() {
        player?.run {
            runCatching { stop() }
            release()
        }
        player = null
    }

    private fun createChannel() {
        val channel = NotificationChannel(CHANNEL_ID, "是非钟声播放", NotificationManager.IMPORTANCE_LOW).apply {
            description = "私密钟声的系统媒体播放控制"
        }
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    override fun onDestroy() {
        stopPlayerOnly()
        mediaSession.release()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    companion object {
        const val ACTION_PLAY = "com.moodanchor.app.action.PLAY_BELL"
        const val ACTION_STOP = "com.moodanchor.app.action.STOP_BELL"
        const val EXTRA_URI = "audio_uri"
        private const val CHANNEL_ID = "moodanchor_bell_playback"
        private const val NOTIFICATION_ID = 4102
    }
}
