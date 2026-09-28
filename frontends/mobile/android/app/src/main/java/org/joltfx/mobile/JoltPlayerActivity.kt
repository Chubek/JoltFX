package org.joltfx.mobile

import android.app.Activity
import android.os.Bundle
import android.view.Choreographer
import android.view.MotionEvent
import android.view.SurfaceView
import android.graphics.Bitmap
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.ImageView
import android.widget.Button
import android.widget.EditText
import android.widget.TextView

class JoltPlayerActivity : Activity(), Choreographer.FrameCallback {
    private var nativeHandle = 0L
    private var lastFrameNanos = 0L
    private var touchStartX = 0f
    private lateinit var surface: SurfaceView
    private lateinit var preview: ImageView
    private lateinit var status: TextView
    private val bitmap = Bitmap.createBitmap(320, 180, Bitmap.Config.ARGB_8888)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        System.loadLibrary("jfx_android_jni")
        surface = SurfaceView(this)
        surface.setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> touchStartX = event.x
                MotionEvent.ACTION_UP -> {
                    val distance = event.x - touchStartX
                    if (kotlin.math.abs(distance) < 12f) nativeTap(nativeHandle)
                    else nativeSwipe(nativeHandle, distance.toDouble())
                }
            }
            true
        }
        val content = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        preview = ImageView(this)
        status = TextView(this)
        content.addView(preview); content.addView(status)
        fun heading(title: String) { content.addView(TextView(this).apply { text = title; textSize = 20f }) }
        fun field(label: String, initial: String): EditText {
            content.addView(TextView(this).apply { text = label })
            return EditText(this).apply { setText(initial); content.addView(this) }
        }
        fun action(label: String, run: () -> Unit) { content.addView(Button(this).apply { text = label; setOnClickListener {
            try { run() } catch (e: Exception) { status.text = e.message }
        } }) }
        fun edit(op: String, a: Int = 0, b: Int = 0, c: Int = 0, value: Double = 0.0, text: String = "") {
            val result = nativeEdit(nativeHandle, op, a, b, c, value, text)
            status.text = if (result == 0) "" else "Edit could not be applied ($result)"
        }
        heading("NLE timeline")
        val track = field("Track index", "0"); val clip = field("Clip index", "0")
        val start = field("Start frame", "0"); val length = field("Length in frames", "90")
        val path = field("Media file path", "")
        action("Play / pause") { nativeTap(nativeHandle) }
        action("Preview sequence") { edit("sequence") }
        action("Add track") { edit("track.add", text = "Video") }
        action("Add video clip") { edit("clip.add", track.text.toString().toInt(), 5, start.text.toString().toInt(), length.text.toString().toDouble(), path.text.toString()) }
        action("Add solid clip") { edit("clip.add", track.text.toString().toInt(), 0, start.text.toString().toInt(), length.text.toString().toDouble()) }
        action("Trim clip") { edit("clip.trim", track.text.toString().toInt(), clip.text.toString().toInt(), start.text.toString().toInt(), length.text.toString().toDouble()) }
        heading("Layer Effects")
        val effect = field("Effect index", "0"); val kind = field("Effect kind", "exposure")
        val parameter = field("Parameter name", "stops"); val amount = field("Value", "0")
        fun stack(op: String, value: Double = 0.0, text: String = "") { edit(op, track.text.toString().toInt(), clip.text.toString().toInt(), effect.text.toString().toInt(), value, text) }
        action("Add effect") { stack("effect.add", text = kind.text.toString()) }
        action("Set parameter") { stack("effect.param", amount.text.toString().toDouble(), parameter.text.toString()) }
        action("Enable") { stack("effect.enabled", 1.0) }
        action("Bypass") { stack("effect.enabled", 0.0) }
        action("Remove effect") { stack("effect.remove") }
        heading("Color Grading")
        val lut = field("LUT file path", ""); val mix = field("LUT mix", "1")
        action("Add LUT effect") { stack("effect.add", text = "lut") }
        action("Load LUT into selected effect") { stack("effect.path", text = lut.text.toString()) }
        action("Set LUT mix") { stack("effect.param", mix.text.toString().toDouble(), "mix") }
        action("Add Lift / Gamma / Gain") { stack("effect.add", text = "lift_gamma_gain") }
        heading("Node Compositing")
        val node = field("Node index", "0"); val from = field("Source node index", "0"); val port = field("Input port", "0")
        val nodeKind = field("Node kind", "exposure")
        action("Preview graph") { edit("graph") }
        action("Add node") { edit("node.add", text = nodeKind.text.toString()) }
        action("Connect") { edit("node.connect", from.text.toString().toInt(), node.text.toString().toInt(), port.text.toString().toInt()) }
        action("Set output") { edit("node.output", node.text.toString().toInt()) }
        action("Set node parameter") { edit("node.param", node.text.toString().toInt(), value = amount.text.toString().toDouble(), text = parameter.text.toString()) }
        action("Delete node") { edit("node.remove", node.text.toString().toInt()) }
        setContentView(ScrollView(this).apply { addView(content) })
    }

    override fun onResume() {
        super.onResume()
        if (nativeHandle == 0L) nativeHandle = nativeCreate(1280, 720, 60.0)
        lastFrameNanos = 0L
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun onPause() {
        Choreographer.getInstance().removeFrameCallback(this)
        super.onPause()
    }

    override fun onDestroy() {
        if (nativeHandle != 0L) nativeDestroy(nativeHandle)
        nativeHandle = 0L
        super.onDestroy()
    }

    override fun doFrame(frameTimeNanos: Long) {
        val elapsed = if (lastFrameNanos == 0L) 0.0 else
            (frameTimeNanos - lastFrameNanos).toDouble() / 1_000_000_000.0
        lastFrameNanos = frameTimeNanos
        if (nativeHandle != 0L) {
            nativeRender(nativeHandle, elapsed)
            nativePixels(nativeHandle)?.let { pixels ->
                bitmap.setPixels(pixels, 0, 320, 0, 0, 320, 180)
                preview.setImageBitmap(bitmap)
            }
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    private external fun nativeEdit(handle: Long, op: String, a: Int, b: Int, c: Int, value: Double, text: String): Int
    private external fun nativePixels(handle: Long): IntArray?
    private external fun nativeCreate(width: Int, height: Int, duration: Double): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeTap(handle: Long): Int
    private external fun nativeSwipe(handle: Long, pixels: Double): Int
    private external fun nativePinch(handle: Long, factor: Double): Int
    private external fun nativeRender(handle: Long, elapsed: Double): Int
}
