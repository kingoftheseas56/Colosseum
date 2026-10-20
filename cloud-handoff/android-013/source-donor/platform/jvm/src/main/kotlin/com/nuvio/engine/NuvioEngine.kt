package com.nuvio.engine

import com.nuvio.engine.internal.NativeCommandResult
import com.nuvio.engine.internal.NativeEvent
import com.nuvio.engine.internal.NuvioNativeApi
import java.io.Closeable
import java.net.HttpURLConnection
import java.net.URI
import java.util.concurrent.ConcurrentHashMap
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.runInterruptible
import kotlinx.coroutines.withContext

public class NuvioEngine private constructor(
    private val nativeApi: NuvioNativeApi,
    initialHandle: Long,
    private val dispatcher: CoroutineDispatcher,
) : Closeable {
    private data class PendingCommand(
        val expectedType: NuvioEventType,
        val completion: CompletableDeferred<NuvioEvent>,
    )

    private val nativeLock = Any()
    private var nativeHandle = initialHandle
    @Volatile private var pollFailure: Throwable? = null
    private val scopeJob = SupervisorJob()
    private val scope = CoroutineScope(scopeJob + dispatcher)
    private val pendingCommands = ConcurrentHashMap<Long, PendingCommand>()
    private val mutableEvents = MutableSharedFlow<NuvioEvent>(
        extraBufferCapacity = 128,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    private val mutableStats = MutableStateFlow(NuvioEngineStats())
    private var lastDroppedEvents = 0L

    public val events: Flow<NuvioEvent> = mutableEvents.asSharedFlow()
    public val stats: StateFlow<NuvioEngineStats> = mutableStats.asStateFlow()

    private val eventLoop = scope.launch {
        try {
            var nextStatsSampleNanos = 0L
            while (currentCoroutineContext().isActive) {
                while (true) {
                    val event = synchronized(nativeLock) {
                        if (nativeHandle == 0L) null else nativeApi.pollEvent(nativeHandle)
                    } ?: break
                    processEvent(event)
                }
                val now = System.nanoTime()
                if (now >= nextStatsSampleNanos) {
                    sampleStats()
                    nextStatsSampleNanos = now + STATS_SAMPLE_INTERVAL_NANOSECONDS
                }
                delay(EVENT_POLL_INTERVAL_MILLISECONDS)
            }
        } catch (error: Throwable) {
            if (error !is CancellationException) {
                pollFailure = error
                failPending(error)
            }
        }
    }

    public suspend fun addMagnet(magnetUri: String): String {
        validateNativeText(magnetUri, "magnet URI", MAXIMUM_MAGNET_BYTES, allowEmpty = false)
        val event = awaitCommand(NuvioEventType.TorrentMetadataReady) { handle ->
            nativeApi.addMagnet(handle, magnetUri)
        }
        return event.torrentId
            ?: throw NuvioEngineException(-1, "metadata-ready event did not contain a torrent ID")
    }

    public suspend fun addTorrent(torrentData: ByteArray): String {
        require(torrentData.isNotEmpty()) { "torrent data must not be empty" }
        require(torrentData.size <= MAXIMUM_TORRENT_BYTES) {
            "torrent data exceeds the 4 MiB native limit"
        }
        val event = awaitCommand(NuvioEventType.TorrentMetadataReady) { handle ->
            nativeApi.addTorrent(handle, torrentData)
        }
        return event.torrentId
            ?: throw NuvioEngineException(-1, "metadata-ready event did not contain a torrent ID")
    }

    public suspend fun files(torrentId: String): List<NuvioTorrentFile> = withContext(dispatcher) {
        validateTorrentId(torrentId)
        val result = synchronized(nativeLock) {
            ensureOpen()
            nativeApi.files(nativeHandle, torrentId)
        }
        checkStatus(result.status)
        result.files
    }

    public suspend fun prepareStream(
        torrentId: String,
        fileIndex: Int? = null,
        filenameHint: String? = null,
        minimumContiguousBytes: Long = 0L,
    ): NuvioStream {
        validateTorrentId(torrentId)
        require(fileIndex == null || fileIndex >= 0) { "file index must be non-negative" }
        filenameHint?.let {
            validateNativeText(it, "filename hint", MAXIMUM_FILENAME_HINT_BYTES, allowEmpty = true)
        }
        require(minimumContiguousBytes in 0L..MAXIMUM_PRELOAD_BYTES) {
            "minimum contiguous bytes must be between 0 and 64 MiB"
        }
        val event = awaitCommand(NuvioEventType.StreamPrepared) { handle ->
            nativeApi.prepareStream(handle, torrentId, fileIndex, filenameHint)
        }
        val stream = NuvioStream(
            id = event.streamId
                ?: throw NuvioEngineException(-1, "prepared event did not contain a stream ID"),
            url = event.streamUrl
                ?: throw NuvioEngineException(-1, "prepared event did not contain a stream URL"),
            torrentId = event.torrentId ?: torrentId,
            fileIndex = event.fileIndex
                ?: throw NuvioEngineException(-1, "prepared event did not contain a file index"),
            fileSize = event.fileSize,
        )
        validateStream(stream)
        if (minimumContiguousBytes > 0L) {
            try {
                preloadStream(stream, minimumContiguousBytes)
            } catch (error: Throwable) {
                runCatching { stopStream(stream.id) }
                throw error
            }
        }
        return stream
    }

    public suspend fun currentStats(): NuvioEngineStats = withContext(dispatcher) {
        val result = synchronized(nativeLock) {
            ensureOpen()
            nativeApi.stats(nativeHandle)
        }
        checkStatus(result.status)
        result.stats
    }

    public suspend fun currentStreamStats(streamId: String): NuvioStreamStats =
        withContext(dispatcher) {
            validateStreamId(streamId)
            val result = synchronized(nativeLock) {
                ensureOpen()
                nativeApi.streamStats(nativeHandle, streamId)
            }
            checkStatus(result.status)
            result.stats
        }

    public suspend fun preloadStream(
        stream: NuvioStream,
        minimumContiguousBytes: Long,
    ): NuvioStreamStats {
        validateStream(stream)
        require(minimumContiguousBytes in 0L..MAXIMUM_PRELOAD_BYTES) {
            "minimum contiguous bytes must be between 0 and 64 MiB"
        }
        val target = minimumContiguousBytes.coerceAtMost(stream.fileSize)
        if (target == 0L) {
            return currentStreamStats(stream.id)
        }
        val uri = validateStreamUrl(stream)
        runInterruptible(dispatcher) {
            val connection = uri.toURL().openConnection() as HttpURLConnection
            try {
                connection.connectTimeout = PRELOAD_CONNECT_TIMEOUT_MILLISECONDS
                connection.readTimeout = PRELOAD_READ_TIMEOUT_MILLISECONDS
                connection.setRequestProperty("Range", "bytes=0-${target - 1}")
                val status = connection.responseCode
                if (status != HttpURLConnection.HTTP_OK && status != HttpURLConnection.HTTP_PARTIAL) {
                    throw NuvioEngineException(-1, "loopback preload returned HTTP $status")
                }
                connection.inputStream.use { input ->
                    val buffer = ByteArray(64 * 1024)
                    var remaining = target
                    while (remaining > 0L) {
                        val read = input.read(buffer, 0, minOf(buffer.size.toLong(), remaining).toInt())
                        if (read < 0) {
                            throw NuvioEngineException(-1, "loopback preload ended before its target")
                        }
                        remaining -= read
                    }
                }
            } finally {
                connection.disconnect()
            }
        }
        repeat(PRELOAD_STATS_ATTEMPTS) {
            val streamStats = currentStreamStats(stream.id)
            if (streamStats.contiguousReadyBytes >= target) {
                return streamStats
            }
            delay(PRELOAD_STATS_DELAY_MILLISECONDS)
        }
        throw NuvioEngineException(-1, "verified preload target was not reflected in stream stats")
    }

    public suspend fun stopStream(streamId: String): NuvioEvent {
        validateStreamId(streamId)
        return awaitCommand(NuvioEventType.StreamStopped) { handle ->
            nativeApi.stopStream(handle, streamId)
        }
    }

    public suspend fun removeTorrent(torrentId: String): NuvioEvent {
        validateTorrentId(torrentId)
        return awaitCommand(NuvioEventType.TorrentRemoved) { handle ->
            nativeApi.removeTorrent(handle, torrentId)
        }
    }

    public suspend fun reclaimDiskCache(targetBytes: Long = 0L): NuvioEvent {
        require(targetBytes >= 0L) { "disk cache target must be non-negative" }
        return awaitCommand(NuvioEventType.DiskCacheReclaimed) { handle ->
            nativeApi.reclaimDiskCache(handle, targetBytes)
        }
    }

    public suspend fun shutdown() {
        withContext(Dispatchers.IO) {
            close()
        }
    }

    override fun close() {
        scope.cancel()
        val handle = synchronized(nativeLock) {
            val current = nativeHandle
            nativeHandle = 0L
            current
        }
        if (handle == 0L) {
            return
        }
        val closed = IllegalStateException("NuvioEngine is closed")
        failPending(closed)
        nativeApi.destroy(handle)
    }

    private suspend fun awaitCommand(
        expectedType: NuvioEventType,
        submit: (Long) -> NativeCommandResult,
    ): NuvioEvent {
        val pending = PendingCommand(expectedType, CompletableDeferred())
        val requestId = synchronized(nativeLock) {
            ensureOpen()
            val result = submit(nativeHandle)
            checkStatus(result.status)
            check(result.requestId != 0L) { "native engine returned an empty request ID" }
            check(pendingCommands.putIfAbsent(result.requestId, pending) == null) {
                "native engine reused request ID ${result.requestId}"
            }
            result.requestId
        }
        return try {
            pending.completion.await()
        } finally {
            pendingCommands.remove(requestId, pending)
        }
    }

    private fun processEvent(nativeEvent: NativeEvent) {
        val event = nativeEvent.toPublicEvent()
        if (event.droppedEvents > lastDroppedEvents) {
            val droppedSinceLastEvent = event.droppedEvents - lastDroppedEvents
            lastDroppedEvents = event.droppedEvents
            failPending(
                NuvioEngineException(
                    -1,
                    "native event queue dropped $droppedSinceLastEvent event(s); resynchronize state",
                ),
            )
        }
        mutableEvents.tryEmit(event)
        if (event.requestId == 0L) {
            return
        }
        val pending = pendingCommands[event.requestId] ?: return
        when {
            event.type == NuvioEventType.TorrentError -> pending.completion.completeExceptionally(
                NuvioEngineException(-1, event.message ?: "torrent operation failed"),
            )
            event.type == pending.expectedType -> pending.completion.complete(event)
        }
    }

    private fun sampleStats() {
        val result = synchronized(nativeLock) {
            if (nativeHandle == 0L) return
            nativeApi.stats(nativeHandle)
        }
        if (result.status == STATUS_OK) {
            mutableStats.value = result.stats
        }
    }

    private fun ensureOpen() {
        check(nativeHandle != 0L) { "NuvioEngine is closed" }
        pollFailure?.let { failure ->
            throw IllegalStateException("NuvioEngine event polling failed", failure)
        }
    }

    private fun failPending(error: Throwable) {
        pendingCommands.values.forEach { pending ->
            pending.completion.completeExceptionally(error)
        }
        pendingCommands.clear()
    }

    private fun checkStatus(status: Int) {
        if (status != STATUS_OK) {
            throw NuvioEngineException(status, nativeApi.statusMessage(status))
        }
    }

    public companion object {
        private const val STATUS_OK = 0
        private const val EVENT_POLL_INTERVAL_MILLISECONDS = 20L
        private const val STATS_SAMPLE_INTERVAL_NANOSECONDS = 1_000_000_000L
        private const val MAXIMUM_MAGNET_BYTES = 16 * 1024
        private const val MAXIMUM_TORRENT_BYTES = 4 * 1024 * 1024
        private const val MAXIMUM_FILENAME_HINT_BYTES = 4 * 1024
        private const val MAXIMUM_PRELOAD_BYTES = 64L * 1024L * 1024L
        private const val PRELOAD_CONNECT_TIMEOUT_MILLISECONDS = 15_000
        private const val PRELOAD_READ_TIMEOUT_MILLISECONDS = 35_000
        private const val PRELOAD_STATS_ATTEMPTS = 40
        private const val PRELOAD_STATS_DELAY_MILLISECONDS = 25L

        internal fun create(
            nativeApi: NuvioNativeApi,
            config: NuvioEngineConfig,
            dispatcher: CoroutineDispatcher,
        ): NuvioEngine {
            config.validate()
            val result = nativeApi.create(config)
            if (result.status != STATUS_OK) {
                throw NuvioEngineException(result.status, nativeApi.statusMessage(result.status))
            }
            check(result.handle != 0L) { "native engine returned an empty handle" }
            return NuvioEngine(nativeApi, result.handle, dispatcher)
        }

        private fun validateTorrentId(torrentId: String) {
            require((torrentId.length == 40 || torrentId.length == 64) && torrentId.all(Char::isHexDigit)) {
                "torrent ID must be a 40- or 64-character hexadecimal hash"
            }
        }

        private fun validateStreamId(streamId: String) {
            require(streamId.length == 64 && streamId.all(Char::isHexDigit)) {
                "stream ID must be a 64-character hexadecimal token"
            }
        }

        private fun validateNativeText(
            value: String,
            name: String,
            maximumBytes: Int,
            allowEmpty: Boolean,
        ) {
            require(allowEmpty || value.isNotEmpty()) { "$name must not be empty" }
            require('\u0000' !in value) { "$name must not contain NUL bytes" }
            require(value.toByteArray(Charsets.UTF_8).size <= maximumBytes) {
                "$name exceeds the $maximumBytes-byte native limit"
            }
        }

        private fun validateStream(stream: NuvioStream) {
            validateStreamId(stream.id)
            validateTorrentId(stream.torrentId)
            require(stream.fileIndex >= 0) { "stream file index must be non-negative" }
            require(stream.fileSize >= 0L) { "stream file size must be non-negative" }
            validateStreamUrl(stream)
        }

        private fun validateStreamUrl(stream: NuvioStream): URI {
            val uri = URI(stream.url)
            require(
                uri.scheme == "http" &&
                    uri.host == "127.0.0.1" &&
                    uri.port in 1..65_535 &&
                    uri.rawUserInfo == null &&
                    uri.rawQuery == null &&
                    uri.rawFragment == null,
            ) {
                "stream URL must be an IPv4 loopback HTTP endpoint"
            }
            require(uri.rawPath == "/stream/${stream.id}") {
                "stream URL token does not match stream ID"
            }
            return uri
        }
    }
}

private fun Char.isHexDigit(): Boolean =
    this in '0'..'9' || this in 'a'..'f' || this in 'A'..'F'

private fun NativeEvent.toPublicEvent(): NuvioEvent = NuvioEvent(
    type = NuvioEventType.fromNative(type),
    sequence = sequence,
    requestId = requestId,
    droppedEvents = droppedEvents,
    torrentId = torrentId.ifEmpty { null },
    message = message.ifEmpty { null },
    fileIndex = fileIndex.takeIf { it >= 0 },
    fileSize = fileSize,
    streamId = streamId.ifEmpty { null },
    streamUrl = streamUrl.ifEmpty { null },
)
