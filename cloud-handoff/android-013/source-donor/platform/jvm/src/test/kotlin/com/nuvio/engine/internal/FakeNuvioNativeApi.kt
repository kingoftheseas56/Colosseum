package com.nuvio.engine.internal

import com.nuvio.engine.NuvioEngineConfig
import com.nuvio.engine.NuvioEngineStats
import com.nuvio.engine.NuvioStreamStats
import com.nuvio.engine.NuvioTorrentFile
import java.util.ArrayDeque
import java.util.concurrent.atomic.AtomicLong

internal class FakeNuvioNativeApi : NuvioNativeApi {
    var reportedApiVersion = 3
    var reportedEngineVersion = "test-engine"
    var reportedBackendVersion = "test-backend"
    var createResult = NativeCreateResult(0, 42)
    var commandStatus = 0
    var filesResult = NativeFilesResult(0, emptyList())
    var statsResult = NativeStatsResult(0, NuvioEngineStats())
    var streamStatsResult = NativeStreamStatsResult(
        0,
        NuvioStreamStats(0, 0, 0, 0, 0),
    )
    var pollError: Throwable? = null
    var createdConfig: NuvioEngineConfig? = null
    val destroyedHandles = mutableListOf<Long>()
    val submittedMagnets = mutableListOf<String>()
    val submittedTorrents = mutableListOf<ByteArray>()
    val preparedStreams = mutableListOf<PreparedStreamCall>()
    val stoppedStreams = mutableListOf<String>()
    val removedTorrents = mutableListOf<String>()
    val reclaimTargets = mutableListOf<Long>()
    private val nextRequestId = AtomicLong(1)
    private val events = ArrayDeque<NativeEvent>()

    data class PreparedStreamCall(
        val torrentId: String,
        val fileIndex: Int?,
        val filenameHint: String?,
    )

    override fun apiVersion(): Int = reportedApiVersion

    override fun engineVersion(): String = reportedEngineVersion

    override fun backendVersion(): String = reportedBackendVersion

    override fun statusMessage(status: Int): String = "status-$status"

    override fun create(config: NuvioEngineConfig): NativeCreateResult {
        createdConfig = config
        return createResult
    }

    override fun destroy(handle: Long) {
        destroyedHandles += handle
    }

    override fun addMagnet(handle: Long, magnetUri: String): NativeCommandResult {
        submittedMagnets += magnetUri
        return command()
    }

    override fun addTorrent(handle: Long, torrentData: ByteArray): NativeCommandResult {
        submittedTorrents += torrentData.copyOf()
        return command()
    }

    override fun pollEvent(handle: Long): NativeEvent? {
        pollError?.let { throw it }
        return synchronized(events) { events.pollFirst() }
    }

    override fun files(handle: Long, torrentId: String): NativeFilesResult = filesResult

    override fun prepareStream(
        handle: Long,
        torrentId: String,
        fileIndex: Int?,
        filenameHint: String?,
    ): NativeCommandResult {
        preparedStreams += PreparedStreamCall(torrentId, fileIndex, filenameHint)
        return command()
    }

    override fun stopStream(handle: Long, streamId: String): NativeCommandResult {
        stoppedStreams += streamId
        return command()
    }

    override fun removeTorrent(handle: Long, torrentId: String): NativeCommandResult {
        removedTorrents += torrentId
        return command()
    }

    override fun stats(handle: Long): NativeStatsResult = statsResult

    override fun streamStats(handle: Long, streamId: String): NativeStreamStatsResult =
        streamStatsResult

    override fun reclaimDiskCache(handle: Long, targetBytes: Long): NativeCommandResult {
        reclaimTargets += targetBytes
        return command()
    }

    fun enqueue(event: NativeEvent) {
        synchronized(events) { events.addLast(event) }
    }

    fun nextRequestId(): Long = nextRequestId.get()

    private fun command(): NativeCommandResult =
        NativeCommandResult(commandStatus, nextRequestId.getAndIncrement())
}

internal fun nativeEvent(
    type: Int,
    requestId: Long,
    sequence: Long = requestId,
    droppedEvents: Long = 0,
    torrentId: String = "",
    message: String = "",
    fileIndex: Int = -1,
    fileSize: Long = 0,
    streamId: String = "",
    streamUrl: String = "",
): NativeEvent = NativeEvent(
    type,
    sequence,
    requestId,
    droppedEvents,
    torrentId,
    message,
    fileIndex,
    fileSize,
    streamId,
    streamUrl,
)
