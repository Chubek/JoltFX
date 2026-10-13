package org.joltfx.mobile

import java.io.ByteArrayInputStream
import java.io.IOException
import java.io.InputStream
import java.nio.file.Files

// Runs on a host JVM as well as through the Android storage wrapper.
fun main() {
    val directory = Files.createTempDirectory("joltfx-document-test").toFile()
    try {
        val bytes = "🎨 رنگ\n(import jolt.math)".toByteArray(Charsets.UTF_8)
        val first = DocumentFiles.importFile(directory, "../../unsafe/🎨.jolt", ByteArrayInputStream(bytes), bytes.size.toLong())
        check(first.parentFile.canonicalFile == directory.canonicalFile)
        check(first.extension == "jolt")
        check(first.readBytes().contentEquals(bytes))
        val second = DocumentFiles.importFile(directory, "../../unsafe/🎨.jolt", ByteArrayInputStream(bytes), 4096)
        check(first != second) // Referenced media must never be overwritten by a new import.
        val before = directory.list()!!.toSet()
        check(runCatching { DocumentFiles.importFile(directory, "huge.wav", ByteArrayInputStream(bytes), 2) }.isFailure)
        check(directory.list()!!.toSet() == before)
        check(runCatching { DocumentFiles.importFile(directory, "cancel.wav", ByteArrayInputStream(bytes), 1024) { true } }.isFailure)
        check(directory.list()!!.toSet() == before)
        var closed = false
        val broken = object : InputStream() {
            override fun read(): Int = throw IOException("provider disconnected")
            override fun close() { closed = true }
        }
        check(runCatching { DocumentFiles.importFile(directory, "broken.ply", broken, 1024) }.isFailure)
        check(closed && directory.list()!!.toSet() == before)
        val closeFailure = object : ByteArrayInputStream(bytes) {
            override fun close() = throw IOException("provider close failed")
        }
        check(runCatching { DocumentFiles.importFile(directory, "broken.ply", closeFailure, 1024) }.isFailure)
        check(directory.list()!!.toSet() == before)
        val empty = DocumentFiles.importFile(directory, "..", ByteArrayInputStream(byteArrayOf()), 1)
        check(empty.length() == 0L && empty.extension == "bin")
        println("Android storage: UTF-8, unique paths, exact bounds, failure cleanup and stream ownership passed")
    } finally { directory.deleteRecursively() }
}
