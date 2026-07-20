package com.hammertomahawk.hashtableinspector

import java.io.Closeable

internal object NativeBridge {
    init {
        System.loadLibrary("hash_table_inspector")
    }

    external fun create(initialCapacity: Long, hashSeed: Long): Long
    external fun put(handle: Long, key: ByteArray, value: Long): ByteArray
    external fun lookup(handle: Long, key: ByteArray): ByteArray
    external fun delete(handle: Long, key: ByteArray): ByteArray
    external fun validate(handle: Long): ByteArray
    external fun corruptSize(handle: Long): ByteArray
    external fun restore(handle: Long): ByteArray
    external fun timeline(handle: Long): ByteArray
    external fun close(handle: Long)
}

internal class NativeHashSession private constructor(
    private var handle: Long,
) : Closeable {
    private val lock = Any()

    companion object {
        fun create(
            initialCapacity: Long = 4,
            hashSeed: Long = 0x0123456789abcdefL,
        ): NativeHashSession {
            val handle = NativeBridge.create(initialCapacity, hashSeed)
            check(handle != 0L) { "Native session creation returned a null handle" }
            return NativeHashSession(handle)
        }
    }

    fun put(key: String, value: Long): NativeOperationResult = withHandle {
        NativeJson.operation(NativeBridge.put(it, key.toDemoBytes(), value))
    }

    fun lookup(key: String): NativeOperationResult = withHandle {
        NativeJson.operation(NativeBridge.lookup(it, key.toDemoBytes()))
    }

    fun delete(key: String): NativeOperationResult = withHandle {
        NativeJson.operation(NativeBridge.delete(it, key.toDemoBytes()))
    }

    fun validate(): NativeOperationResult = withHandle {
        NativeJson.operation(NativeBridge.validate(it))
    }

    fun corruptSize(): NativeOperationResult = withHandle {
        NativeJson.operation(NativeBridge.corruptSize(it))
    }

    fun restore(): NativeOperationResult = withHandle {
        NativeJson.operation(NativeBridge.restore(it))
    }

    fun timeline(): List<InspectionFrame> = withHandle {
        NativeJson.timeline(NativeBridge.timeline(it))
    }

    override fun close() {
        synchronized(lock) {
            val current = handle
            if (current != 0L) {
                handle = 0L
                NativeBridge.close(current)
            }
        }
    }

    private inline fun <T> withHandle(block: (Long) -> T): T =
        synchronized(lock) {
            check(handle != 0L) { "Native session is closed" }
            block(handle)
        }

    private fun String.toDemoBytes(): ByteArray = toByteArray(Charsets.US_ASCII)
}
