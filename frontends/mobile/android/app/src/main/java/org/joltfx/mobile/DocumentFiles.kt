package org.joltfx.mobile

import java.io.File
import java.io.InputStream
import java.io.OutputStream

/** Provider names are hints for decoders, never filesystem paths. */
internal object DocumentFiles {
    const val PROJECT_LIMIT = 8L * 1024 * 1024
    const val ASSET_LIMIT = 64L * 1024 * 1024

    /** Takes ownership of input; publishes only a fully copied, closed file. */
    fun importFile(directory: File, displayName: String, input: InputStream, limit: Long,
        cancelled: () -> Boolean = { false }): File {
        var file: File? = null
        try {
            input.use { source ->
                check(directory.isDirectory || directory.mkdirs()) { "Unable to create import directory" }
                val extension = displayName.substringAfterLast('.', "").lowercase()
                    .takeIf { it.matches(Regex("[a-z0-9]{1,12}")) } ?: "bin"
                val target = File.createTempFile("jfx-", ".$extension", directory)
                file = target
                target.outputStream().use { copyBounded(source, it, limit, cancelled) }
            }
            return checkNotNull(file)
        } catch (failure: Exception) {
            file?.delete()
            throw failure
        }
    }

    fun copyBounded(input: InputStream, output: OutputStream, limit: Long, cancelled: () -> Boolean = { false }) {
        require(limit >= 0)
        val buffer = ByteArray(64 * 1024)
        var total = 0L
        while (true) {
            check(!cancelled()) { "File transfer cancelled" }
            val count = input.read(buffer)
            if (count < 0) break
            check(count.toLong() <= limit - total) { "File exceeds the $limit byte import limit" }
            output.write(buffer, 0, count)
            total += count
        }
    }
}
