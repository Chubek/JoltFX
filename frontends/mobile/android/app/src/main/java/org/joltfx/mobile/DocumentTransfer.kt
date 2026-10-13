@file:Suppress("DEPRECATION") // Platform-managed I/O executor on the API 26+ dependency-free frontend.

package org.joltfx.mobile

import android.content.Context
import android.net.Uri
import android.os.AsyncTask
import android.provider.OpenableColumns
import java.io.File
import java.lang.ref.WeakReference

internal interface DocumentTransferReceiver {
    fun documentTransferred(task: DocumentTransfer, key: String, file: File?, name: String, error: Exception?)
}

/** Android owns the I/O worker. No Core/JNI calls or Activity references leave
 * the main thread; cancellation disposes unclaimed files after provider I/O. */
class DocumentTransfer internal constructor(context: Context, receiver: DocumentTransferReceiver,
    private val key: String, private val uri: Uri, private val snapshot: File? = null
) : AsyncTask<Void, Void, Unit>() {
    private val app = context.applicationContext
    private val receiver = WeakReference(receiver)
    private var imported: File? = null
    private var name = "asset.bin"
    private var failure: Exception? = null

    override fun doInBackground(vararg params: Void?): Unit {
        try {
            val resolver = app.contentResolver
            if (snapshot != null) {
                snapshot.inputStream().use { input ->
                    (resolver.openOutputStream(uri, "wt") ?: error("Unable to open destination")).use {
                        DocumentFiles.copyBounded(input, it, DocumentFiles.PROJECT_LIMIT) { isCancelled }
                    }
                }
            } else {
                resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                    if (it.moveToFirst()) name = it.getString(0) ?: name
                }
                if (isCancelled) return
                val input = resolver.openInputStream(uri) ?: error("Unable to open document")
                val project = key == "project"
                imported = DocumentFiles.importFile(if (project) app.cacheDir else File(app.filesDir, "imports"),
                    name, input, if (project) DocumentFiles.PROJECT_LIMIT else DocumentFiles.ASSET_LIMIT) { isCancelled }
            }
        } catch (error: Exception) { failure = error }
        finally { snapshot?.delete() }
    }

    override fun onPostExecute(result: Unit?) {
        val owner = receiver.get()
        if (owner == null) imported?.delete()
        else owner.documentTransferred(this, key, imported, name, failure)
    }

    override fun onCancelled(result: Unit?) {
        imported?.delete(); snapshot?.delete()
    }
}
