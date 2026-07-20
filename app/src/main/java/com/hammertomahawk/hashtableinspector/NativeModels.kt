package com.hammertomahawk.hashtableinspector

import org.json.JSONArray
import org.json.JSONObject

internal const val INSPECTION_CONTRACT_VERSION = 1

internal data class NativeOperationResult(
    val operationId: Long,
    val status: String,
    val tableStatus: String,
    val value: Long?,
    val frames: List<InspectionFrame>,
)

internal data class InspectionFrame(
    val frameId: Long,
    val sequence: Long,
    val operationId: Long,
    val event: String,
    val status: Int,
    val subjectId: Long,
    val data: List<Long>,
    val snapshot: HashSnapshot,
)

internal data class HashSnapshot(
    val revision: Long,
    val reportedSize: Int,
    val entryCount: Int,
    val capacity: Int,
    val resizeAt: Int,
    val valid: Boolean,
    val violationCount: Int,
    val violation: String,
    val buckets: List<HashBucket>,
) {
    val loadFactor: Double
        get() = if (capacity == 0) 0.0 else entryCount.toDouble() / capacity
}

internal data class HashBucket(
    val index: Int,
    val entries: List<HashEntry>,
)

internal data class HashEntry(
    val id: Long,
    val hash: String,
    val key: String,
    val value: Long,
    val chainIndex: Int,
)

internal object NativeJson {
    fun operation(bytes: ByteArray): NativeOperationResult {
        val root = JSONObject(bytes.toString(Charsets.UTF_8))
        root.requireVersion()
        return NativeOperationResult(
            operationId = root.getLong("operationId"),
            status = root.getString("status"),
            tableStatus = root.getString("tableStatus"),
            value = if (root.isNull("value")) null else root.getLong("value"),
            frames = parseFrames(root.getJSONArray("frames")),
        )
    }

    fun timeline(bytes: ByteArray): List<InspectionFrame> {
        val root = JSONObject(bytes.toString(Charsets.UTF_8))
        root.requireVersion()
        return parseFrames(root.getJSONArray("frames"))
    }

    private fun JSONObject.requireVersion() {
        val version = getInt("version")
        require(version == INSPECTION_CONTRACT_VERSION) {
            "Unsupported native inspection contract: $version"
        }
    }

    private fun parseFrames(array: JSONArray): List<InspectionFrame> =
        List(array.length()) { index -> parseFrame(array.getJSONObject(index)) }

    private fun parseFrame(json: JSONObject): InspectionFrame {
        val dataJson = json.getJSONArray("data")
        require(dataJson.length() == 4) { "Native event data must have four values" }
        return InspectionFrame(
            frameId = json.getLong("frameId"),
            sequence = json.getLong("sequence"),
            operationId = json.getLong("operationId"),
            event = json.getString("event"),
            status = json.getInt("status"),
            subjectId = json.getLong("subjectId"),
            data = List(4) { index -> dataJson.getLong(index) },
            snapshot = parseSnapshot(json.getJSONObject("snapshot")),
        )
    }

    private fun parseSnapshot(json: JSONObject): HashSnapshot = HashSnapshot(
        revision = json.getLong("revision"),
        reportedSize = json.getInt("reportedSize"),
        entryCount = json.getInt("entryCount"),
        capacity = json.getInt("capacity"),
        resizeAt = json.getInt("resizeAt"),
        valid = json.getBoolean("valid"),
        violationCount = json.getInt("violationCount"),
        violation = json.getString("violation"),
        buckets = parseBuckets(json.getJSONArray("buckets")),
    )

    private fun parseBuckets(array: JSONArray): List<HashBucket> =
        List(array.length()) { index ->
            val json = array.getJSONObject(index)
            HashBucket(
                index = json.getInt("index"),
                entries = parseEntries(json.getJSONArray("entries")),
            )
        }

    private fun parseEntries(array: JSONArray): List<HashEntry> =
        List(array.length()) { index ->
            val json = array.getJSONObject(index)
            HashEntry(
                id = json.getLong("id"),
                hash = json.getString("hash"),
                key = json.getString("key"),
                value = json.getLong("value"),
                chainIndex = json.getInt("chainIndex"),
            )
        }
}
