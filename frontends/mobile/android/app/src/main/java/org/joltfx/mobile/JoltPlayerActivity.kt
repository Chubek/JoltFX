package org.joltfx.mobile

import android.app.Activity
import android.annotation.SuppressLint
import android.content.Context
import android.os.Bundle
import android.media.AudioTrack
import android.media.AudioFormat
import android.media.AudioAttributes
import android.view.Choreographer
import android.view.MotionEvent
import android.graphics.Bitmap
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.ImageView
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Spinner
import android.widget.ArrayAdapter
import android.text.TextWatcher
import android.text.Editable
import org.json.JSONArray
import org.json.JSONObject

private class PreviewImageView(context: Context) : ImageView(context) {
    override fun performClick(): Boolean { super.performClick(); return true }
}

class JoltPlayerActivity : Activity(), Choreographer.FrameCallback {
    private var nativeHandle = 0L
    private var lastFrameNanos = 0L
    private var touchStartX = 0f
    private lateinit var preview: PreviewImageView
    private lateinit var status: TextView
    private lateinit var timeline: NLETimelineView
    private var refreshNLE: () -> Unit = {}
    private var refreshGraph: () -> Unit = {}
    private var previewNode = -1
    private val invalidateColors = mutableListOf<() -> Unit>()
    private val bitmap = Bitmap.createBitmap(320, 180, Bitmap.Config.ARGB_8888)
    private var exportJob = 0L
    private var audioMixer = 0L
    private var audioTrack: AudioTrack? = null
    private var audioSample = 0L
    private var audioWritten = 0L
    private var audioPending: FloatArray? = null
    private var audioOffset = 0
    private var compositionActive = false
    private var scene3DActive = false
    private var refreshScene3D: () -> Unit = {}

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        System.loadLibrary("jfx_android_jni")
        val content = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        preview = PreviewImageView(this)
        preview.contentDescription = getString(R.string.preview_description)
        preview.setOnClickListener { nativeTap(nativeHandle) }
        preview.setOnTouchListener { view, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> touchStartX = event.x
                MotionEvent.ACTION_UP -> {
                    val distance = event.x - touchStartX
                    if (kotlin.math.abs(distance) < 12f) view.performClick()
                    else { stopAudio(); nativeSwipe(nativeHandle, distance.toDouble()) }
                }
            }
            true
        }
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
        fun edit(op: String, a: Int = 0, b: Int = 0, c: Int = 0, value: Double = 0.0, text: String = ""): Boolean {
            val result = nativeEdit(nativeHandle, op, a, b, c, value, text)
            status.text = if (result == 0) "" else "Edit could not be applied ($result)"
            if (result == 0) {
                stopAudio()
                previewNode = -1
                invalidateColors.forEach { it() }; refreshNLE(); refreshGraph(); refreshScene3D()
            }
            return result == 0
        }
        heading("NLE timeline")
        val track = field("Track index", "0"); val clip = field("Clip index", "0")
        val start = field("Start frame", "0"); val length = field("Length in frames", "90")
        val path = field("Media file path", "")
        val frame = field("Playhead frame", "0"); val destination = field("Destination track", "0")
        val slip = field("Slip delta (frames)", "0"); val name = field("Clip / track name", "")
        timeline = NLETimelineView(this); content.addView(timeline)
        refreshNLE = {
            if (nativeHandle != 0L) {
                timeline.state = JSONObject(nativeSequenceState(nativeHandle) ?: "{}")
                timeline.selectedTrack = track.text.toString().toIntOrNull() ?: 0
                timeline.selectedClip = clip.text.toString().toIntOrNull() ?: 0
            }
        }
        timeline.onSelect = { a, b -> track.setCommandNumber(a); clip.setCommandNumber(b); invalidateColors.forEach { it() } }
        timeline.onSeek = { at ->
            stopAudio()
            frame.setCommandNumber(at.toInt())
            val s = timeline.state; nativeSeek(nativeHandle, at * s.optDouble("fpsDen", 1.0) / s.optDouble("fpsNum", 30.0))
        }
        timeline.onEdit = { op, a, b, c, value ->
            val applied = edit(op, a, b, c, value)
            if (applied && op == "clip.move" && c != a) { track.setCommandNumber(c); clip.setCommandNumber(timeline.state.getJSONArray("tracks").getJSONObject(c).getJSONArray("clips").length() - 1) }
            refreshNLE()
        }
        action("Play / pause") { nativeTap(nativeHandle) }
        action("Preview sequence") { edit("sequence") }
        action("Add track") { edit("track.add", text = "Video") }
        action("Add video clip") { edit("clip.add", track.text.toString().toInt(), 5, start.text.toString().toInt(), length.text.toString().toDouble(), path.text.toString()) }
        action("Add audio clip") { edit("clip.add", track.text.toString().toInt(), 6, start.text.toString().toInt(), length.text.toString().toDouble(), path.text.toString()) }
        action("Add image clip") { edit("clip.add", track.text.toString().toInt(), 4, start.text.toString().toInt(), length.text.toString().toDouble(), path.text.toString()) }
        action("Add solid clip") { edit("clip.add", track.text.toString().toInt(), 0, start.text.toString().toInt(), length.text.toString().toDouble()) }
        action("Trim clip") { edit("clip.trim", track.text.toString().toInt(), clip.text.toString().toInt(), start.text.toString().toInt(), length.text.toString().toDouble()) }
        fun nle(op: String, c: Int = 0, value: Double = 0.0, text: String = "") { edit(op, track.text.toString().toInt(), clip.text.toString().toInt(), c, value, text) }
        heading("Audio mixing")
        val audioGain = field("Clip audio gain (0..16)", "1"); val audioPan = field("Stereo pan (-1..1)", "0")
        val fadeIn = field("Audio fade in frames", "0"); val fadeOut = field("Audio fade out frames", "0")
        val trackGain = field("Track audio gain (0..16)", "1")
        action("Apply clip gain") { nle("clip.audio.gain", value = audioGain.text.toString().toDouble()) }
        action("Apply stereo pan") { nle("clip.audio.pan", value = audioPan.text.toString().toDouble()) }
        action("Apply audio fade in") { nle("clip.audio.fade_in", value = fadeIn.text.toString().toDouble()) }
        action("Apply audio fade out") { nle("clip.audio.fade_out", value = fadeOut.text.toString().toDouble()) }
        action("Enable clip audio") { nle("clip.audio.enabled", value = 1.0) }
        action("Mute clip audio") { nle("clip.audio.enabled") }
        action("Apply track audio gain") { edit("track.audio.gain", track.text.toString().toInt(), value = trackGain.text.toString().toDouble()) }
        action("Seek frame") { timeline.onSeek(frame.text.toString().toDouble()) }
        action("Split at frame") { nle("clip.split", value = frame.text.toString().toDouble()) }
        action("Move clip") { nle("clip.move", destination.text.toString().toInt(), start.text.toString().toDouble()) }
        action("Duplicate clip") { nle("clip.duplicate", destination.text.toString().toInt(), start.text.toString().toDouble()) }
        action("Slip source") { nle("clip.slip", value = slip.text.toString().toDouble()) }
        action("Rename clip") { nle("clip.name", text = name.text.toString()) }
        action("Delete clip") { nle("clip.remove") }
        action("Ripple delete") { nle("clip.ripple_delete") }
        action("Insert gap") { edit("track.insert_gap", track.text.toString().toInt(), 0, start.text.toString().toInt(), length.text.toString().toDouble()) }
        action("Rename track") { edit("track.name", track.text.toString().toInt(), text = name.text.toString()) }
        action("Mute track") { edit("track.mute", track.text.toString().toInt(), value = 1.0) }
        action("Unmute track") { edit("track.mute", track.text.toString().toInt()) }
        action("Solo track") { edit("track.solo", track.text.toString().toInt(), value = 1.0) }
        action("Unsolo track") { edit("track.solo", track.text.toString().toInt()) }
        action("Move track") { edit("track.move", track.text.toString().toInt(), value = destination.text.toString().toDouble()) }
        action("Remove track") { edit("track.remove", track.text.toString().toInt()) }
        action("Undo") { edit("undo") }; action("Redo") { edit("redo") }
        val project = field("Project file path", "${filesDir}/project.jfx")
        action("Open project") { stopAudio(); if (nativeProjectFile(nativeHandle, project.text.toString(), false) != 0) throw IllegalArgumentException("Unable to open project"); previewNode = -1; invalidateColors.forEach { it() }; refreshNLE(); refreshGraph() }
        action("Save project") { if (nativeProjectFile(nativeHandle, project.text.toString(), true) != 0) throw IllegalArgumentException("Unable to save project") }
        val output = field("Frame export path (PPM)", "${filesDir}/frame.ppm")
        action("Export selected frame") { if (nativeWriteFrame(nativeHandle, kotlin.math.floor(timeline.frame + 1e-7).toLong(), output.text.toString()) != 0) throw IllegalArgumentException("Unable to export frame") }
        val videoOutput = field("Encoded video path (.mp4/.mov/.mkv)", "${filesDir}/sequence.mp4")
        val videoStart = field("Video start frame", "0"); val videoFrames = field("Video frame count (0: sequence)", "0")
        fun exportVideo(audio: Boolean) {
            check(exportJob == 0L) { "An export is already running" }
            val count = videoFrames.text.toString().toLong().let { if (it == 0L && compositionActive) 300L else it }
            exportJob = nativeExportBegin(nativeHandle, videoOutput.text.toString(), videoStart.text.toString().toLong(), count, audio)
        }
        action("Export video with mixed audio") { exportVideo(true) }
        action("Export silent video") { exportVideo(false) }
        action("Cancel video export") { if (exportJob != 0L) nativeExportDestroy(exportJob); exportJob = 0L }
        val rasterW = field("New sequence width", "320"); val rasterH = field("New sequence height", "180")
        val fpsNum = field("New sequence FPS numerator", "30"); val fpsDen = field("New sequence FPS denominator", "1")
        action("New empty sequence") { edit("sequence.new", rasterW.text.toString().toInt(), rasterH.text.toString().toInt(), fpsNum.text.toString().toInt(), fpsDen.text.toString().toDouble()) }
        heading("Layer Effects")
        val effect = field("Effect index", "0"); val kind = field("Effect kind", "invert")
        val parameter = field("Parameter name", "amount"); val amount = field("Value", "0")
        fun stack(op: String, value: Double = 0.0, text: String = "") { edit(op, track.text.toString().toInt(), clip.text.toString().toInt(), effect.text.toString().toInt(), value, text) }
        action("Add effect") { stack("effect.add", text = kind.text.toString()) }
        action("Set parameter") { stack("effect.param", amount.text.toString().toDouble(), parameter.text.toString()) }
        action("Enable") { stack("effect.enabled", 1.0) }
        action("Bypass") { stack("effect.enabled", 0.0) }
        action("Remove effect") { stack("effect.remove") }
        val colors = JSONArray(nativeColorCatalog() ?: "[]")
        fun colorSection(title: String, section: String, sectionId: Int) {
            heading(title)
            val operators = (0 until colors.length()).map { colors.getJSONObject(it) }.filter { it.getString("section") == section }
            val choose = Spinner(this).apply { adapter = ArrayAdapter(this@JoltPlayerActivity,
                android.R.layout.simple_spinner_dropdown_item, operators.map { it.getString("label") }) }
            content.addView(choose)
            val layer = field("$title layer index (0-based)", "0")
            val controls = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            invalidateColors.add { controls.removeAllViews() }
            fun colorEdit(action: String, value: Double = 0.0, text: String = "") {
                edit("$section.$action", track.text.toString().toInt(), clip.text.toString().toInt(), layer.text.toString().toInt(), value, text)
                controls.removeAllViews()
            }
            val invalidate = object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) { controls.removeAllViews() }
                override fun afterTextChanged(s: Editable?) {}
            }
            track.addTextChangedListener(invalidate); clip.addTextChangedListener(invalidate); layer.addTextChangedListener(invalidate)
            action("Add $title operator") { colorEdit("add", text = operators[choose.selectedItemPosition].getString("name")) }
            action("Show selected color layer controls") {
                controls.removeAllViews()
                val a = track.text.toString().toInt(); val b = clip.text.toString().toInt(); val c = layer.text.toString().toInt()
                val name = nativeColorKind(nativeHandle, a, b, c, sectionId)
                val op = operators.find { it.getString("name") == name } ?: throw IllegalArgumentException("Select an existing color layer")
                controls.addView(TextView(this).apply { text = op.getString("label") })
                val params = op.getJSONArray("params")
                for (i in 0 until params.length()) {
                    val param = params.getJSONObject(i)
                    controls.addView(TextView(this).apply { text = getString(R.string.parameter_range, param.getString("label"), param.getDouble("min").toString(), param.getDouble("max").toString()) })
                    val input = EditText(this).apply { setCommandNumber(nativeColorValue(nativeHandle, a, b, c, sectionId, i)) }
                    controls.addView(input)
                    controls.addView(Button(this).apply { text = getString(R.string.apply_parameter, param.getString("label")); setOnClickListener {
                        try { colorEdit("param", input.text.toString().toDouble(), param.getString("name")) }
                        catch (e: Exception) { status.text = e.message }
                    } })
                }
            }
            content.addView(controls)
            val lut = field("$title LUT file path", "")
            action("Load LUT into selected color layer") { colorEdit("path", text = lut.text.toString()) }
            action("Clear LUT") { colorEdit("path") }
            action("Enable color layer") { colorEdit("enabled", 1.0) }
            action("Bypass color layer") { colorEdit("enabled", 0.0) }
            action("Reset color parameters") { colorEdit("reset") }
            action("Remove color layer") { colorEdit("remove") }
            action("Move color layer up") { colorEdit("move", layer.text.toString().toDouble() - 1) }
            action("Move color layer down") { colorEdit("move", layer.text.toString().toDouble() + 1) }
        }
        colorSection("Color Calibration", "calibration", 1)
        colorSection("Color Grading", "grade", 2)
        heading("Node Compositing")
        val node = field("Selected node index (0-based)", "0")
        val catalog = JSONArray(nativeNodeCatalog() ?: "[]")
        val kinds = (0 until catalog.length()).map { catalog.getJSONObject(it) }
        val choose = Spinner(this).apply { adapter = ArrayAdapter(this@JoltPlayerActivity,
            android.R.layout.simple_spinner_dropdown_item, kinds.map { "${it.getString("category")} / ${it.getString("label")}" }) }
        content.addView(choose)
        val graphWidth = field("Composition width", "320"); val graphHeight = field("Composition height", "180")
        val graph = CompositionGraphView(this).apply { this.catalog = catalog }
        content.addView(graph, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, (320 * resources.displayMetrics.density).toInt()))
        val inspector = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        content.addView(inspector)
        fun inspectorField(label: String, initial: String): EditText {
            inspector.addView(TextView(this).apply { text = label })
            return EditText(this).apply { setText(initial); inspector.addView(this) }
        }
        fun inspectorAction(label: String, run: () -> Unit) { inspector.addView(Button(this).apply { text = label; setOnClickListener { try { run() } catch (e: Exception) { status.text = e.message } } }) }
        refreshGraph = {
            if (nativeHandle != 0L) {
                graph.state = JSONObject(nativeGraphState(nativeHandle) ?: "{}")
                compositionActive = graph.state.optBoolean("active")
                val nodes = graph.state.optJSONArray("nodes") ?: JSONArray()
                val n = node.text.toString().toIntOrNull() ?: 0
                graph.selected = n; inspector.removeAllViews()
                val selected = nodes.optJSONObject(n)
                val k = kinds.find { it.getString("name") == selected?.optString("kind") }
                if (selected != null && k != null) {
                    val label = inspectorField("Node label", selected.getString("label"))
                    inspectorAction("Rename node") { edit("node.label", n, text = label.text.toString()) }
                    val inputs = k.getJSONArray("inputs")
                    for (p in 0 until inputs.length()) {
                        val port = inputs.getJSONObject(p)
                        inspector.addView(TextView(this).apply { text = getString(R.string.port_type, port.getString("label"), port.getString("type")) })
                        val choices = mutableListOf("Disconnected"); val edges = mutableListOf<Pair<Int, Int>?>(null)
                        for (s in 0 until nodes.length()) if (s != n) {
                            val source = nodes.getJSONObject(s); val sk = kinds.find { it.getString("name") == source.getString("kind") }!!
                            val outputs = sk.getJSONArray("outputs")
                            for (o in 0 until outputs.length()) if (outputs.getJSONObject(o).getString("type") == port.getString("type")) {
                                choices.add("$s: ${source.getString("label")} / ${outputs.getJSONObject(o).getString("label")}"); edges.add(Pair(s, o))
                            }
                        }
                        val input = Spinner(this).apply { adapter = ArrayAdapter(this@JoltPlayerActivity, android.R.layout.simple_spinner_dropdown_item, choices) }
                        val edge = selected.getJSONArray("inputs").optJSONObject(p)
                        input.setSelection(if (edge == null) 0 else maxOf(0, edges.indexOf(Pair(edge.getInt("source"), edge.getInt("port")))))
                        inspector.addView(input)
                        inspectorAction("Apply ${port.getString("label")} connection") {
                            val e = edges[input.selectedItemPosition]
                            if (e == null) edit("node.disconnect", n, p) else edit("node.connect", e.first, n, p, e.second.toDouble())
                        }
                    }
                    val params = k.getJSONArray("params")
                    for (p in 0 until params.length()) {
                        val param = params.getJSONObject(p)
                        val input = inspectorField("${param.getString("label")} [${param.getDouble("min")}, ${param.getDouble("max")}]${if (param.getBoolean("integer")) " integer" else ""}", selected.getJSONArray("values").getDouble(p).toString())
                        inspectorAction("Apply ${param.getString("label")}") { edit("node.param", n, value = input.text.toString().toDouble(), text = param.getString("name")) }
                    }
                    val strings = k.getJSONArray("strings")
                    for (s in 0 until strings.length()) {
                        val input = inspectorField(strings.getString(s), selected.getJSONArray("strings").optString(s))
                        inspectorAction("Apply resource path") { edit("node.path", n, s, text = input.text.toString()) }
                        inspectorAction("Clear resource path") { edit("node.path", n, s) }
                    }
                } else inspector.addView(TextView(this).apply { text = getString(R.string.select_node) })
            }
        }
        graph.onSelect = { n -> node.setCommandNumber(n); if (previewNode >= 0) previewNode = n; refreshGraph() }
        graph.onEdit = { op, a, b, c, value, text -> edit(op, a, b, c, value, text) }
        node.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) { refreshGraph() }
            override fun afterTextChanged(s: Editable?) {}
        })
        action("Preview graph") { edit("graph") }
        action("New composition") { graph.cancel(); edit("graph.new", graphWidth.text.toString().toInt(), graphHeight.text.toString().toInt()); node.setText("0") }
        action("Set composition size") { edit("graph.size", graphWidth.text.toString().toInt(), graphHeight.text.toString().toInt()) }
        action("Add node") { if (edit("node.add", text = kinds[choose.selectedItemPosition].getString("name"))) node.setCommandNumber(graph.state.getJSONArray("nodes").length() - 1) }
        action("Set output") { if (edit("node.output", node.text.toString().toInt())) edit("graph") }
        action("Preview selected node") { if (edit("graph")) previewNode = node.text.toString().toInt() }
        action("Duplicate node") { if (edit("node.duplicate", node.text.toString().toInt())) node.setCommandNumber(graph.state.getJSONArray("nodes").length() - 1) }
        action("Reset node") { edit("node.reset", node.text.toString().toInt()) }
        action("Delete node") { edit("node.remove", node.text.toString().toInt()) }
        action("Fit nodes") { graph.fit() }
        val time = field("Composition preview seconds", "0")
        action("Seek composition time") { if (nativeSeek(nativeHandle, time.text.toString().toDouble()) != 0) throw IllegalArgumentException("Invalid time") }
        action("Export composition (PPM)") { if (nativeWriteGraph(nativeHandle, time.text.toString().toDouble(), output.text.toString()) != 0) throw IllegalArgumentException("Unable to export composition") }
        heading("3D Modeling & Animation")
        val sceneState = TextView(this); content.addView(sceneState)
        refreshScene3D = {
            if (nativeHandle != 0L) {
                val state = JSONObject(nativeScene3DState(nativeHandle) ?: "{}")
                scene3DActive = state.optBoolean("active")
                sceneState.text = "${state.optInt("fps",30)} FPS / ${state.optInt("frames",120)} frames\n${state.optJSONArray("objects") ?: JSONArray()}"
            }
        }
        action("Preview 3D workspace") { edit("3d") }
        action("New 3D scene") { edit("3d.new"); nativeSeek(nativeHandle, 0.0) }
        for (primitive in listOf("cube", "sphere", "plane")) action("Add $primitive") { edit("3d.add", text = primitive) }
        val object3D = field("3D object index", "0")
        val name3D = field("3D object name", "Mesh")
        action("Rename 3D object") { edit("3d.name", object3D.text.toString().toInt(), text = name3D.text.toString()) }
        action("Show 3D object") { edit("3d.visible", object3D.text.toString().toInt(), value = 1.0) }
        action("Hide 3D object") { edit("3d.visible", object3D.text.toString().toInt(), value = 0.0) }
        for ((channel, title) in listOf("Material R", "Material G", "Material B").withIndex()) {
            val input = field(title, listOf("0.32", "0.65", "0.9")[channel])
            action("Apply $title") { edit("3d.color", object3D.text.toString().toInt(), channel, value = input.text.toString().toDouble()) }
        }
        val frame3D = field("3D frame", "0")
        val fps3D = field("3D FPS", "30"); val duration3D = field("3D duration frames", "120")
        action("Set 3D clock") { edit("3d.clock", fps3D.text.toString().toInt(), duration3D.text.toString().toInt()) }
        action("Seek 3D frame") {
            val state = JSONObject(nativeScene3DState(nativeHandle) ?: "{}")
            if (edit("3d")) nativeSeek(nativeHandle, frame3D.text.toString().toDouble() / state.optDouble("fps",30.0))
        }
        for ((channel, title) in listOf("Position X", "Position Y", "Position Z", "Rotation X", "Rotation Y", "Rotation Z", "Scale X", "Scale Y", "Scale Z").withIndex()) {
            val input = field(title, if (channel >= 6) "1" else "0")
            action("Apply $title") { edit("3d.transform", object3D.text.toString().toInt(), channel, value = input.text.toString().toDouble()) }
            action("Key $title") { edit("3d.key", object3D.text.toString().toInt(), channel, frame3D.text.toString().toInt(), input.text.toString().toDouble()) }
            action("Remove $title key") { edit("3d.key_remove", object3D.text.toString().toInt(), channel, frame3D.text.toString().toInt()) }
        }
        val keyChannel = field("3D key channel (0..8)", "0"); val interpolation = field("3D interpolation (0 hold, 1 linear, 2 smooth)", "1")
        action("Set 3D key interpolation") { edit("3d.interpolation", object3D.text.toString().toInt(), keyChannel.text.toString().toInt(), frame3D.text.toString().toInt(), interpolation.text.toString().toDouble()) }
        val mesh = field("3D PLY path", "${filesDir}/mesh.ply")
        action("Import PLY") { edit("3d.import_ply", text = mesh.text.toString()) }
        action("Export PLY") { edit("3d.export_ply", object3D.text.toString().toInt(), text = mesh.text.toString()) }
        action("Subdivide mesh") { edit("3d.subdivide", object3D.text.toString().toInt()) }
        action("Align principal axes") { edit("3d.align", object3D.text.toString().toInt()) }
        action("Duplicate mesh") { edit("3d.duplicate", object3D.text.toString().toInt()) }
        action("Delete mesh") { edit("3d.remove", object3D.text.toString().toInt()) }
        val vertex3D = field("3D vertex index", "0"); val axis3D = field("Vertex axis (0..2)", "0"); val coordinate3D = field("Vertex coordinate", "0")
        action("Set vertex coordinate") { edit("3d.vertex", object3D.text.toString().toInt(), vertex3D.text.toString().toInt(), axis3D.text.toString().toInt(), coordinate3D.text.toString().toDouble()) }
        val mass3D = field("Rigid body mass (0 = static)", "0"); val bake3D = field("Physics bake frames", "60")
        action("Set rigid body mass") { edit("3d.mass", object3D.text.toString().toInt(), value = mass3D.text.toString().toDouble()) }
        action("Bake rigid-body animation") { edit("3d.bake", c = bake3D.text.toString().toInt()) }
        for ((channel, title) in listOf("Orbit yaw", "Orbit pitch", "Camera distance", "Target X", "Target Y", "Target Z", "Field of view").withIndex()) {
            val input = field(title, listOf("35","22","7","0","0","0","45")[channel])
            action("Apply $title") { edit("3d.camera", channel, value = input.text.toString().toDouble()) }
        }
        action("Export 3D frame (PPM)") {
            if (edit("3d") && nativeWriteFrame(nativeHandle, frame3D.text.toString().toLong(), output.text.toString()) != 0) throw IllegalArgumentException("Unable to export 3D frame")
        }
        setContentView(ScrollView(this).apply { addView(content) })
    }

    override fun onResume() {
        super.onResume()
        if (nativeHandle == 0L) nativeHandle = nativeCreate(320, 180, 60.0)
        refreshNLE()
        refreshGraph()
        refreshScene3D()
        lastFrameNanos = 0L
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun onPause() {
        Choreographer.getInstance().removeFrameCallback(this)
        stopAudio()
        if (exportJob != 0L) nativeExportDestroy(exportJob)
        exportJob = 0L
        super.onPause()
    }

    override fun onDestroy() {
        stopAudio()
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
            val s = timeline.state
            timeline.frame = nativeTime(nativeHandle) * s.optDouble("fpsNum", 30.0) / s.optDouble("fpsDen", 1.0)
            (if (previewNode >= 0) nativeGraphPixels(nativeHandle, previewNode) else nativePixels(nativeHandle))?.let { pixels ->
                bitmap.setPixels(pixels, 0, 320, 0, 0, 320, 180)
                preview.setImageBitmap(bitmap)
            } ?: run { status.text = getString(R.string.preview_error) }
            try { pumpAudio() } catch (e: Exception) { stopAudio(); status.text = e.message }
            if (exportJob != 0L) {
                val result = nativeExportStep(exportJob)
                status.text = if (result == 0) getString(R.string.video_export_progress, nativeExportCompleted(exportJob)) else getString(R.string.video_export_error, result)
                if (result != 0 || nativeExportState(exportJob) != 0) { nativeExportDestroy(exportJob); exportJob = 0L }
            }
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    private fun stopAudio() {
        audioTrack?.pause(); audioTrack?.flush(); audioTrack?.release(); audioTrack = null
        if (audioMixer != 0L) nativeAudioDestroy(audioMixer)
        audioMixer = 0L; audioWritten = 0L; audioPending = null; audioOffset = 0
    }
    private fun pumpAudio() {
        if (!nativePlaying(nativeHandle) || compositionActive || scene3DActive) { stopAudio(); return }
        val now = (nativeTime(nativeHandle) * 48000).toLong()
        if (audioTrack != null && kotlin.math.abs(audioSample - now) > 19200) stopAudio()
        if (audioTrack == null) {
            audioMixer = nativeAudioCreate(nativeHandle)
            audioTrack = AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MOVIE).build())
                .setAudioFormat(AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_FLOAT).setSampleRate(48000).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
                .setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(65536).build().also { it.play() }
            audioSample = now
        }
        val output = audioTrack ?: return
        val consumed = output.playbackHeadPosition.toLong() and 0xffffffffL
        if (audioWritten - consumed > 8192) return
        val pcm = audioPending ?: (nativeAudioRender(audioMixer, audioSample, 4096) ?: error("Audio mix failed")).also { audioPending = it }
        val written = output.write(pcm, audioOffset, pcm.size - audioOffset, AudioTrack.WRITE_NON_BLOCKING)
        check(written >= 0) { "Audio output failed ($written)" }
        audioOffset += written; audioWritten += written / 2; audioSample += written / 2
        if (audioOffset == pcm.size) { audioPending = null; audioOffset = 0 }
    }

    // Command fields are parsed by toDouble/toInt and sent to C: retain ASCII
    // decimal separators and full precision, independent of the display locale.
    @SuppressLint("SetTextI18n")
    private fun EditText.setCommandNumber(value: Number) { setText(value.toString()) }

    private external fun nativeEdit(handle: Long, op: String, a: Int, b: Int, c: Int, value: Double, text: String): Int
    private external fun nativeSequenceState(handle: Long): String?
    private external fun nativeSeek(handle: Long, seconds: Double): Int
    private external fun nativeTime(handle: Long): Double
    private external fun nativeProjectFile(handle: Long, path: String, save: Boolean): Int
    private external fun nativeWriteFrame(handle: Long, frame: Long, path: String): Int
    private external fun nativeColorCatalog(): String?
    private external fun nativeColorKind(handle: Long, a: Int, b: Int, c: Int, section: Int): String
    private external fun nativeColorValue(handle: Long, a: Int, b: Int, c: Int, section: Int, param: Int): Double
    private external fun nativePixels(handle: Long): IntArray?
    private external fun nativeGraphPixels(handle: Long, node: Int): IntArray?
    private external fun nativeNodeCatalog(): String?
    private external fun nativeGraphState(handle: Long): String?
    private external fun nativeScene3DState(handle: Long): String?
    private external fun nativeWriteGraph(handle: Long, seconds: Double, path: String): Int
    private external fun nativeCreate(width: Int, height: Int, duration: Double): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeTap(handle: Long): Int
    private external fun nativeSwipe(handle: Long, pixels: Double): Int
    private external fun nativePinch(handle: Long, factor: Double): Int
    private external fun nativeRender(handle: Long, elapsed: Double): Int
    private external fun nativePlaying(handle: Long): Boolean
    private external fun nativeExportBegin(handle: Long, path: String, start: Long, frames: Long, audio: Boolean): Long
    private external fun nativeExportStep(job: Long): Int
    private external fun nativeExportState(job: Long): Int
    private external fun nativeExportCompleted(job: Long): Long
    private external fun nativeExportDestroy(job: Long)
    private external fun nativeAudioCreate(handle: Long): Long
    private external fun nativeAudioRender(mixer: Long, sample: Long, frames: Int): FloatArray?
    private external fun nativeAudioDestroy(mixer: Long)
}
