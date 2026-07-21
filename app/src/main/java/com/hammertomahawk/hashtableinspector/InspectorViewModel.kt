package com.hammertomahawk.hashtableinspector

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel

internal class InspectorViewModel : ViewModel() {
    private var session: NativeHashSession? = NativeHashSession.create()

    var frames by mutableStateOf(requireSession().timeline())
        private set
    var selectedFrameId by mutableStateOf(frames.lastOrNull()?.frameId)
        private set
    var outcome by mutableStateOf("Native session created")
        private set
    var quarantined by mutableStateOf(false)
        private set

    val selectedFrame: InspectionFrame?
        get() = frames.firstOrNull { it.frameId == selectedFrameId }

    val viewingLive: Boolean
        get() = selectedFrameId != null && selectedFrameId == frames.lastOrNull()?.frameId

    fun put(key: String, value: Long) = consume("Put") {
        requireSession().put(key, value)
    }

    fun lookup(key: String) = consume("Lookup") {
        requireSession().lookup(key)
    }

    fun delete(key: String) = consume("Delete") {
        requireSession().delete(key)
    }

    fun validate() = consume("Validate") {
        requireSession().validate()
    }

    fun corruptSize() = consume("Corrupt size") {
        requireSession().corruptSize()
    }

    fun restore() = consume("Restore") {
        requireSession().restore()
    }

    fun reset() {
        try {
            closeSession()
            val replacement = NativeHashSession.create()
            session = replacement
            frames = replacement.timeline()
            selectedFrameId = frames.lastOrNull()?.frameId
            quarantined = false
            outcome = "Native session reset"
        } catch (error: RuntimeException) {
            outcome = "Reset failed: ${error.message ?: error.javaClass.simpleName}"
        }
    }

    fun runDeterministicDemo() {
        reset()
        put("a", 1)
        put("b", 2)
        lookup("a")
        lookup("missing")
        put("a", 11)
        delete("b")
        put("b", 2)
        put("c", 3)
        put("d", 4)
        validate()
        corruptSize()
        validate()
        outcome = "Demo complete: collision and rehash are in history; size corruption is active"
    }

    fun selectFrame(frameId: Long) {
        if (frames.any { it.frameId == frameId }) {
            selectedFrameId = frameId
        }
    }

    fun selectLive() {
        selectedFrameId = frames.lastOrNull()?.frameId
    }

    fun reportInputError(message: String) {
        outcome = message
    }

    override fun onCleared() {
        closeSession()
    }

    private fun requireSession(): NativeHashSession =
        checkNotNull(session) { "Native session is unavailable" }

    private fun closeSession() {
        val current = session
        session = null
        current?.close()
    }

    private fun consume(
        label: String,
        operation: () -> NativeOperationResult,
    ) {
        try {
            val result = operation()
            if (result.frames.isNotEmpty()) {
                frames = (frames + result.frames).takeLast(64)
                selectedFrameId = frames.last().frameId
            }
            result.frames.forEach { frame ->
                when (frame.event) {
                    "size_corruption_applied" -> quarantined = true
                    "clean_state_restored" -> quarantined = false
                }
            }
            val value = result.value?.let { ", value=$it" }.orEmpty()
            outcome = "$label: ${result.status} / ${result.tableStatus}$value"
        } catch (error: RuntimeException) {
            outcome = "$label failed: ${error.message ?: error.javaClass.simpleName}"
        }
    }
}
