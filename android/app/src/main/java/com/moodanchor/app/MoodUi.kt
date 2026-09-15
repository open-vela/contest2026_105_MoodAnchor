package com.moodanchor.app

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

object MoodUi {
    const val BACKGROUND = "#F5F7F4"
    const val SURFACE = "#FFFFFF"
    const val PRIMARY = "#416B61"
    const val PRIMARY_SOFT = "#DDEAE5"
    const val INK = "#20302C"
    const val MUTED = "#66756F"
    const val WARM = "#F1E9DD"

    fun Context.dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()
    fun rounded(color: String, radiusDp: Int, context: Context, strokeColor: String? = null) = GradientDrawable().apply {
        shape = GradientDrawable.RECTANGLE
        setColor(Color.parseColor(color))
        cornerRadius = context.dp(radiusDp).toFloat()
        strokeColor?.let { setStroke(context.dp(1), Color.parseColor(it)) }
    }
    fun title(context: Context, value: String, size: Float = 28f) = TextView(context).apply {
        text = value; textSize = size; setTextColor(Color.parseColor(INK)); setTypeface(typeface, Typeface.BOLD)
    }
    fun body(context: Context, value: String, size: Float = 15f) = TextView(context).apply {
        text = value; textSize = size; setTextColor(Color.parseColor(MUTED)); setLineSpacing(0f, 1.18f)
    }
    fun button(context: Context, label: String, filled: Boolean = true) = Button(context).apply {
        text = label; textSize = 16f; isAllCaps = false
        setTextColor(Color.parseColor(if (filled) "#FFFFFF" else PRIMARY))
        backgroundTintList = ColorStateList.valueOf(Color.parseColor(if (filled) PRIMARY else PRIMARY_SOFT))
        background = rounded(if (filled) PRIMARY else PRIMARY_SOFT, 18, context)
        minHeight = context.dp(54); stateListAnimator = null
    }
    fun card(context: Context, color: String = SURFACE) = LinearLayout(context).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(context.dp(22), context.dp(22), context.dp(22), context.dp(22))
        background = rounded(color, 24, context); elevation = context.dp(2).toFloat()
    }
    fun space(context: Context, height: Int) = View(context).apply { layoutParams = LinearLayout.LayoutParams(1, context.dp(height)) }
}
