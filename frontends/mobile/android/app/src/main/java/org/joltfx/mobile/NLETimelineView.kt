package org.joltfx.mobile

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.view.MotionEvent
import android.view.View
import org.json.JSONObject
import kotlin.math.roundToInt

/** Touch clip selection/moving and ruler seeking; timing edits remain native. */
class NLETimelineView(context: Context) : View(context) {
    var state = JSONObject()
        set(value) { field = value; requestLayout(); invalidate() }
    var frame = 0.0
        set(value) { field = value; invalidate() }
    var selectedTrack = 0
    var selectedClip = 0
    var onSelect: (Int, Int) -> Unit = { _, _ -> }
    var onSeek: (Double) -> Unit = {}
    var onEdit: (String, Int, Int, Int, Double) -> Unit = { _, _, _, _, _ -> }
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private var downX = 0f
    private var dragTrack = -1
    private var dragClip = -1
    private var dragStart = 0.0
    private var dragLength = 0.0
    private var mode = 0
    private var moved = false
    private fun scale() = (width - 90).coerceAtLeast(1) / state.optDouble("duration", 300.0).coerceAtLeast(30.0)
    override fun onMeasure(w: Int, h: Int) { setMeasuredDimension(MeasureSpec.getSize(w), 30 + (state.optJSONArray("tracks")?.length() ?: 1) * 48) }
    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(Color.rgb(22, 28, 38)); paint.textSize = 20f
        val tracks = state.optJSONArray("tracks") ?: return
        for (i in 0 until tracks.length()) {
            val track = tracks.getJSONObject(i); val y = 30f + (tracks.length() - 1 - i) * 48f
            paint.color = Color.WHITE; canvas.drawText("${if (track.getBoolean("muted")) "M " else ""}$i: ${track.getString("name")}", 4f, y + 28, paint)
            val clips = track.getJSONArray("clips")
            for (c in 0 until clips.length()) {
                val clip = clips.getJSONObject(c); val x = 90f + (clip.getDouble("start") * scale()).toFloat()
                val end = x + (clip.getDouble("length") * scale()).toFloat()
                paint.color = if (!clip.getBoolean("enabled")) Color.DKGRAY else if (i == selectedTrack && c == selectedClip) Color.rgb(55, 140, 185) else Color.rgb(40, 90, 125)
                canvas.drawRect(x, y, end, y + 42, paint)
                canvas.save(); canvas.clipRect(x, y, end, y + 42); paint.color = Color.WHITE
                canvas.drawText(clip.getString("name"), x + 3, y + 27, paint); canvas.restore()
            }
        }
        paint.color = Color.YELLOW; canvas.drawRect(90f + (frame * scale()).toFloat(), 0f, 92f + (frame * scale()).toFloat(), height.toFloat(), paint)
        paint.color = Color.WHITE; canvas.drawText("Frame ${frame.toInt()}", 90f, 22f, paint)
    }
    override fun performClick(): Boolean { super.performClick(); return true }
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val tracks = state.optJSONArray("tracks") ?: return false
        val at = ((event.x - 90) / scale()).coerceAtLeast(0.0).roundToInt().toDouble()
        val track = (tracks.length() - 1 - ((event.y - 30) / 48).toInt()).coerceIn(0, (tracks.length() - 1).coerceAtLeast(0))
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                parent.requestDisallowInterceptTouchEvent(true); downX = event.x; moved = false; dragTrack = -1
                if (event.y < 30 || tracks.length() == 0) { onSeek(at); return true }
                val clips = tracks.getJSONObject(track).getJSONArray("clips")
                for (c in clips.length() - 1 downTo 0) {
                    val clip = clips.getJSONObject(c); val start = clip.getDouble("start"); val length = clip.getDouble("length")
                    if (at >= start && at < start + length) {
                        selectedTrack = track; selectedClip = c; onSelect(track, c); onSeek(at)
                        dragTrack = track; dragClip = c; dragStart = start; dragLength = length
                        mode = if (event.x - (90 + start * scale()) < 12) 1 else if (90 + (start + length) * scale() - event.x < 12) 2 else 0
                        invalidate(); break
                    }
                }
            }
            MotionEvent.ACTION_MOVE -> { moved = moved || kotlin.math.abs(event.x - downX) > 8 }
            MotionEvent.ACTION_UP -> {
                performClick(); parent.requestDisallowInterceptTouchEvent(false)
                if (dragTrack >= 0 && moved) {
                    val delta = ((event.x - downX) / scale()).roundToInt().toDouble()
                    when (mode) {
                        1 -> onEdit("clip.trim", dragTrack, dragClip, (dragStart + delta).toInt(), dragLength - delta)
                        2 -> onEdit("clip.trim", dragTrack, dragClip, dragStart.toInt(), dragLength + delta)
                        else -> onEdit("clip.move", dragTrack, dragClip, track, (dragStart + delta).coerceAtLeast(0.0))
                    }
                }
                dragTrack = -1
            }
            MotionEvent.ACTION_CANCEL -> { dragTrack = -1; parent.requestDisallowInterceptTouchEvent(false) }
        }
        return true
    }
}
