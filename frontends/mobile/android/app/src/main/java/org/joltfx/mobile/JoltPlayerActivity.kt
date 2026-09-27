package org.joltfx.mobile

import android.app.Activity
import android.os.Bundle
import android.view.Choreographer
import android.view.MotionEvent
import android.view.SurfaceView

class JoltPlayerActivity : Activity(), Choreographer.FrameCallback {
    private var nativeHandle = 0L
    private var lastFrameNanos = 0L
    private var touchStartX = 0f
    private lateinit var surface: SurfaceView

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
        setContentView(surface)
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
        if (nativeHandle != 0L) nativeRender(nativeHandle, elapsed)
        Choreographer.getInstance().postFrameCallback(this)
    }

    private external fun nativeCreate(width: Int, height: Int, duration: Double): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeTap(handle: Long): Int
    private external fun nativeSwipe(handle: Long, pixels: Double): Int
    private external fun nativePinch(handle: Long, factor: Double): Int
    private external fun nativeRender(handle: Long, elapsed: Double): Int
}
