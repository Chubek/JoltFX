package org.joltfx.mobile

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.max
import kotlin.math.min
import kotlin.math.hypot

/** Native graph state; the view owns selection and a single uncommitted drag. */
class CompositionGraphView(context: Context) : View(context) {
    var state = JSONObject()
        set(value) { field = value; invalidate() }
    var catalog = JSONArray()
    var selected = 0
        set(value) { field = value; invalidate() }
    var onSelect: (Int) -> Unit = {}
    var onEdit: (String, Int, Int, Int, Double, String) -> Unit = { _, _, _, _, _, _ -> }
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val wirePath = Path()
    private var zoom = resources.displayMetrics.density * .75f
    private var panX = 20f; private var panY = 20f
    private var downX = 0f; private var downY = 0f
    private var originalX = 0f; private var originalY = 0f
    private var atX = 0f; private var atY = 0f
    private var drag = -1; private var wire = -1; private var wirePort = 0
    private var panning = false
    private val scale = ScaleGestureDetector(context, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScale(detector: ScaleGestureDetector): Boolean {
            val x = (detector.focusX - panX) / zoom; val y = (detector.focusY - panY) / zoom
            zoom = (zoom * detector.scaleFactor).coerceIn(.1f, 4f)
            panX = detector.focusX - x * zoom; panY = detector.focusY - y * zoom
            cancel(); invalidate(); return true
        }
    })
    init { minimumHeight = (320 * resources.displayMetrics.density).toInt(); contentDescription = "Composition graph: drag nodes and ports, drag background to pan, pinch to zoom" }
    fun cancel() { drag = -1; wire = -1; panning = false }
    fun fit() {
        val nodes = state.optJSONArray("nodes") ?: return
        var minX = 0f; var minY = 0f; var maxX = 240f; var maxY = 160f
        for (n in 0 until nodes.length()) {
            val node = nodes.getJSONObject(n); val x = node.getDouble("x").toFloat(); val y = node.getDouble("y").toFloat()
            minX = min(minX, x); minY = min(minY, y); maxX = max(maxX, x + 210); maxY = max(maxY, y + 160)
        }
        zoom = min(width / (maxX - minX + 40), height / (maxY - minY + 40)).coerceIn(.1f, 3f)
        panX = 20 - minX * zoom; panY = 20 - minY * zoom; invalidate()
    }
    private fun kind(node: JSONObject): JSONObject? = (0 until catalog.length()).map { catalog.getJSONObject(it) }.find { it.getString("name") == node.getString("kind") }
    private data class Hit(val node: Int, val side: Int = -1, val port: Int = 0)
    private fun hit(x: Float, y: Float): Hit? {
        val nodes = state.optJSONArray("nodes") ?: return null
        for (n in nodes.length() - 1 downTo 0) {
            val node = nodes.getJSONObject(n); val k = kind(node) ?: continue
            val nx = node.getDouble("x").toFloat(); val ny = node.getDouble("y").toFloat()
            for (side in 0..1) {
                val ports = k.getJSONArray(if (side == 0) "inputs" else "outputs")
                for (p in 0 until ports.length()) if (hypot(x - nx - if (side == 1) 210 else 0, y - ny - 44 - p * 23) < 14 / zoom) return Hit(n, side, p)
            }
            val height = 55 + max(k.getJSONArray("inputs").length(), k.getJSONArray("outputs").length()) * 23
            if (x >= nx && x <= nx + 210 && y >= ny && y <= ny + height) return Hit(n)
        }
        return null
    }
    override fun onTouchEvent(event: MotionEvent): Boolean {
        parent.requestDisallowInterceptTouchEvent(true); scale.onTouchEvent(event)
        if (event.pointerCount > 1 || scale.isInProgress) { cancel(); return true }
        val x = (event.x - panX) / zoom; val y = (event.y - panY) / zoom
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = event.x; downY = event.y; val hit = hit(x, y)
                if (hit == null) panning = true
                else {
                    selected = hit.node; onSelect(selected)
                    if (hit.side == 1) { wire = hit.node; wirePort = hit.port; atX = x; atY = y }
                    else if (hit.side < 0) {
                        drag = hit.node; val node = state.getJSONArray("nodes").getJSONObject(drag)
                        originalX = node.getDouble("x").toFloat(); originalY = node.getDouble("y").toFloat(); atX = originalX; atY = originalY
                    }
                }
            }
            MotionEvent.ACTION_MOVE -> {
                if (panning) { panX += event.x - downX; panY += event.y - downY; downX = event.x; downY = event.y }
                if (drag >= 0) { atX = (originalX + (event.x - downX) / zoom).coerceIn(-1e6f, 1e6f); atY = (originalY + (event.y - downY) / zoom).coerceIn(-1e6f, 1e6f) }
                if (wire >= 0) { atX = x; atY = y }
            }
            MotionEvent.ACTION_UP -> {
                val d = drag; val w = wire; val port = wirePort; cancel()
                if (d >= 0 && (kotlin.math.abs(atX - originalX) > .01 || kotlin.math.abs(atY - originalY) > .01)) onEdit("node.position", d, 0, 0, atX.toDouble(), atY.toString())
                if (w >= 0) { val to = hit(x, y); if (to?.side == 0) onEdit("node.connect", w, to.node, to.port, port.toDouble(), "") }
                performClick(); parent.requestDisallowInterceptTouchEvent(false)
            }
            MotionEvent.ACTION_CANCEL -> { cancel(); parent.requestDisallowInterceptTouchEvent(false) }
        }
        invalidate(); return true
    }
    override fun performClick(): Boolean { super.performClick(); return true }
    private fun portColor(type: String) = Color.parseColor(when (type) { "image" -> "#64c3fa"; "color" -> "#e6965a"; else -> "#a0dc82" })
    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas); canvas.drawColor(Color.rgb(21, 28, 38)); val nodes = state.optJSONArray("nodes") ?: return
        canvas.save(); canvas.translate(panX, panY); canvas.scale(zoom, zoom)
        fun px(n: Int) = if (drag == n) atX else nodes.getJSONObject(n).getDouble("x").toFloat()
        fun py(n: Int) = if (drag == n) atY else nodes.getJSONObject(n).getDouble("y").toFloat()
        fun line(ax: Float, ay: Float, bx: Float, by: Float) {
            paint.style = Paint.Style.STROKE; paint.strokeWidth = 2f
            wirePath.reset(); wirePath.moveTo(ax, ay); wirePath.cubicTo(ax + 60, ay, bx - 60, by, bx, by)
            canvas.drawPath(wirePath, paint); paint.style = Paint.Style.FILL
        }
        for (n in 0 until nodes.length()) {
            val node = nodes.getJSONObject(n); val k = kind(node) ?: continue; val inputs = node.getJSONArray("inputs")
            for (p in 0 until inputs.length()) if (!inputs.isNull(p)) {
                val edge = inputs.getJSONObject(p); val source = edge.getInt("source")
                paint.color = portColor(k.getJSONArray("inputs").getJSONObject(p).getString("type"))
                line(px(source) + 210, py(source) + 44 + edge.getInt("port") * 23, px(n), py(n) + 44 + p * 23)
            }
        }
        for (n in 0 until nodes.length()) {
            val node = nodes.getJSONObject(n); val k = kind(node) ?: continue; val x = px(n); val y = py(n)
            val h = 55 + max(k.getJSONArray("inputs").length(), k.getJSONArray("outputs").length()) * 23
            paint.color = if (selected == n) Color.rgb(40, 106, 146) else Color.rgb(53, 65, 85); canvas.drawRect(x, y, x + 210, y + h, paint)
            if (!state.isNull("output") && state.optInt("output", -1) == n) { paint.color = Color.YELLOW; paint.style = Paint.Style.STROKE; canvas.drawRect(x, y, x + 210, y + h, paint); paint.style = Paint.Style.FILL }
            paint.color = Color.WHITE; paint.textSize = 12f; canvas.drawText("$n: ${node.getString("label")}".take(28), x + 8, y + 20, paint)
            for (side in 0..1) {
                val ports = k.getJSONArray(if (side == 0) "inputs" else "outputs")
                for (p in 0 until ports.length()) {
                    val port = ports.getJSONObject(p); val ax = x + if (side == 1) 210 else 0; val ay = y + 44 + p * 23
                    paint.color = portColor(port.getString("type")); canvas.drawCircle(ax, ay, 5f, paint)
                    paint.color = Color.WHITE; paint.textAlign = if (side == 1) Paint.Align.RIGHT else Paint.Align.LEFT
                    canvas.drawText(port.getString("label"), ax + if (side == 1) -10 else 10, ay + 4, paint); paint.textAlign = Paint.Align.LEFT
                }
            }
        }
        if (wire >= 0) { paint.color = Color.YELLOW; line(px(wire) + 210, py(wire) + 44 + wirePort * 23, atX, atY) }
        canvas.restore()
    }
}
