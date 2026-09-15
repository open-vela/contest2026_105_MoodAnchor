package com.moodanchor.app

data class MoodEvent(val id: Long, val timestamp: Long, val score: Float, val threshold: Float, val quality: String)
