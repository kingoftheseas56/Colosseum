package com.nuvio.engine.internal

import com.nuvio.engine.NuvioEngineConfig
import com.nuvio.engine.NuvioEngineException
import com.nuvio.engine.NuvioEngineStats
import com.nuvio.engine.NuvioStreamStats
import com.nuvio.engine.NuvioTorrentFile
import com.sun.jna.Memory
import com.sun.jna.Pointer
import com.sun.jna.ptr.LongByReference
import com.sun.jna.ptr.PointerByReference

internal class JnaNuvioNativeApi(
    private val library: JnaNativeLibrary,
) : NuvioNativeApi {
    override fun apiVersion(): Int = library.nuvio_engine_api_version()

    override fun engineVersion(): String = nativeString(library.nuvio_engine_version_string())

    override fun backendVersion(): String =
        nativeString(library.nuvio_engine_protocol_backend_version())

    override fun statusMessage(status: Int): String =
        nativeString(library.nuvio_engine_status_message(status))

    override fun create(config: NuvioEngineConfig): NativeCreateResult {
        val dataDirectory = nativeString(config.dataDirectory.absolutePath)
        val cacheDirectory = nativeString(config.cacheDirectory.absolutePath)
        val tlsCaBundle = config.tlsCaBundle?.let { nativeString(it.absolutePath) }
        val nativeConfig = NativeConfig()
        library.nuvio_engine_config_init_sized(nativeConfig, nativeConfig.size())
        nativeConfig.apply {
            this.dataDirectory = dataDirectory
            this.cacheDirectory = cacheDirectory
            memoryCacheCapacityBytes = config.memoryCacheCapacityBytes
            diskCacheCapacityBytes = config.diskCacheCapacityBytes
            listenPort = config.listenPort.toShort()
            uploadMode = config.uploadMode.nativeValue
            uploadLimitBytesPerSecond = config.uploadLimitBytesPerSecond
            streamInactivityTimeoutMilliseconds = config.streamInactivityTimeoutMilliseconds
            warmTorrentTimeoutMilliseconds = config.warmTorrentTimeoutMilliseconds
            tlsCaBundlePath = tlsCaBundle
            torrentProfile = config.torrentProfile.nativeValue
            write()
        }
        val output = PointerByReference()
        val status = library.nuvio_engine_create(nativeConfig, output)
        return NativeCreateResult(status, output.value?.let(Pointer::nativeValue) ?: 0L)
    }

    override fun destroy(handle: Long) {
        library.nuvio_engine_destroy(pointer(handle))
    }

    override fun addMagnet(handle: Long, magnetUri: String): NativeCommandResult {
        val magnet = nativeString(magnetUri)
        val request = NativeTorrentRequest()
        library.nuvio_engine_torrent_request_init_sized(request, request.size())
        request.apply {
            sourceType = TORRENT_SOURCE_MAGNET
            this.magnetUri = magnet
            write()
        }
        return command { requestId ->
            library.nuvio_engine_add_torrent(pointer(handle), request, requestId)
        }
    }

    override fun addTorrent(handle: Long, torrentData: ByteArray): NativeCommandResult {
        val data = Memory(torrentData.size.toLong()).apply {
            write(0, torrentData, 0, torrentData.size)
        }
        val request = NativeTorrentRequest()
        library.nuvio_engine_torrent_request_init_sized(request, request.size())
        request.apply {
            sourceType = TORRENT_SOURCE_DATA
            this.torrentData = data
            torrentDataSize = SizeT(torrentData.size.toLong())
            write()
        }
        return command { requestId ->
            library.nuvio_engine_add_torrent(pointer(handle), request, requestId)
        }
    }

    override fun pollEvent(handle: Long): NativeEvent? {
        val event = NativeEventStructure()
        library.nuvio_engine_event_init_sized(event, event.size())
        val status = library.nuvio_engine_poll_event(pointer(handle), event)
        if (status == STATUS_NO_EVENT) {
            return null
        }
        checkStatus(status)
        event.read()
        return NativeEvent(
            type = event.type,
            sequence = event.sequence,
            requestId = event.requestId,
            droppedEvents = event.droppedEvents,
            torrentId = decode(event.torrentId),
            message = decode(event.message),
            fileIndex = event.fileIndex,
            fileSize = event.fileSize,
            streamId = decode(event.streamId),
            streamUrl = decode(event.streamUrl),
        )
    }

    override fun files(handle: Long, torrentId: String): NativeFilesResult {
        val id = nativeString(torrentId)
        val count = SizeTByReference()
        var status = library.nuvio_engine_get_file_count(pointer(handle), id, count)
        if (status != STATUS_OK) {
            return NativeFilesResult(status, emptyList())
        }
        val fileCount = count.getValue()
        if (fileCount > MAXIMUM_JVM_FILE_COUNT) {
            throw NuvioEngineException(-1, "native file count exceeds the JVM safety limit")
        }
        val files = ArrayList<NuvioTorrentFile>(fileCount.toInt())
        repeat(fileCount.toInt()) { index ->
            val file = NativeFileStructure()
            library.nuvio_engine_file_init_sized(file, file.size())
            status = library.nuvio_engine_get_file(pointer(handle), id, SizeT(index.toLong()), file)
            if (status != STATUS_OK) {
                return NativeFilesResult(status, emptyList())
            }
            file.read()
            files += NuvioTorrentFile(
                index = file.index,
                offset = file.offset,
                size = file.size,
                path = decode(file.path),
                pathTruncated = file.pathTruncated.toInt() != 0,
            )
        }
        return NativeFilesResult(STATUS_OK, files)
    }

    override fun prepareStream(
        handle: Long,
        torrentId: String,
        fileIndex: Int?,
        filenameHint: String?,
    ): NativeCommandResult {
        val id = nativeString(torrentId)
        val hint = filenameHint?.let(::nativeString)
        val request = NativeStreamRequest()
        library.nuvio_engine_stream_request_init_sized(request, request.size())
        request.apply {
            this.torrentId = id
            fileIndex?.let { this.fileIndex = it }
            this.filenameHint = hint
            write()
        }
        return command { requestId ->
            library.nuvio_engine_prepare_stream(pointer(handle), request, requestId)
        }
    }

    override fun stopStream(handle: Long, streamId: String): NativeCommandResult {
        val id = nativeString(streamId)
        return command { requestId ->
            library.nuvio_engine_stop_stream(pointer(handle), id, requestId)
        }
    }

    override fun removeTorrent(handle: Long, torrentId: String): NativeCommandResult {
        val id = nativeString(torrentId)
        return command { requestId ->
            library.nuvio_engine_remove_torrent(pointer(handle), id, requestId)
        }
    }

    override fun stats(handle: Long): NativeStatsResult {
        val nativeStats = NativeStatsStructure()
        library.nuvio_engine_stats_init_sized(nativeStats, nativeStats.size())
        val status = library.nuvio_engine_get_stats(pointer(handle), nativeStats)
        nativeStats.read()
        return NativeStatsResult(
            status,
            NuvioEngineStats(
                activeTorrents = nativeStats.activeTorrents,
                activeStreams = nativeStats.activeStreams,
                activeHttpRequests = nativeStats.activeHttpRequests,
                connectedPeers = nativeStats.connectedPeers,
                connectedSeeds = nativeStats.connectedSeeds,
                knownPeers = nativeStats.knownPeers,
                connectCandidates = nativeStats.connectCandidates,
                interestedPeers = nativeStats.interestedPeers,
                unchokedPeers = nativeStats.unchokedPeers,
                downloadingPeers = nativeStats.downloadingPeers,
                snubbedPeers = nativeStats.snubbedPeers,
                pendingBlockRequests = nativeStats.pendingBlockRequests,
                targetBlockRequests = nativeStats.targetBlockRequests,
                timedOutBlockRequests = nativeStats.timedOutBlockRequests,
                connectingPeers = nativeStats.connectingPeers,
                handshakingPeers = nativeStats.handshakingPeers,
                targetPiecePeers = nativeStats.targetPiecePeers,
                targetPieceUnchokedPeers = nativeStats.targetPieceUnchokedPeers,
                targetPieceDownloadingPeers = nativeStats.targetPieceDownloadingPeers,
                offTargetDownloadingPeers = nativeStats.offTargetDownloadingPeers,
                trackerReplyEvents = nativeStats.trackerReplyEvents,
                trackerErrorEvents = nativeStats.trackerErrorEvents,
                dhtReplyEvents = nativeStats.dhtReplyEvents,
                trackerPeersReturned = nativeStats.trackerPeersReturned,
                dhtPeersReturned = nativeStats.dhtPeersReturned,
                peerConnectEvents = nativeStats.peerConnectEvents,
                peerDisconnectEvents = nativeStats.peerDisconnectEvents,
                peerDisconnectTimeouts = nativeStats.peerDisconnectTimeouts,
                peerDisconnectConnectFailures = nativeStats.peerDisconnectConnectFailures,
                peerDisconnectRedundant = nativeStats.peerDisconnectRedundant,
                peerDisconnectTurnover = nativeStats.peerDisconnectTurnover,
                peerDisconnectOther = nativeStats.peerDisconnectOther,
                torrentFinishedEvents = nativeStats.torrentFinishedEvents,
                pendingPieceReads = nativeStats.pendingPieceReads,
                downloadRateBytesPerSecond = nativeStats.downloadRateBytesPerSecond,
                uploadRateBytesPerSecond = nativeStats.uploadRateBytesPerSecond,
                totalPayloadDownloadBytes = nativeStats.totalPayloadDownloadBytes,
                totalPayloadUploadBytes = nativeStats.totalPayloadUploadBytes,
                memoryCacheCapacityBytes = nativeStats.memoryCacheCapacityBytes,
                memoryCacheUsedBytes = nativeStats.memoryCacheUsedBytes,
                memoryCacheHits = nativeStats.memoryCacheHits,
                memoryCacheMisses = nativeStats.memoryCacheMisses,
                memoryCacheEvictions = nativeStats.memoryCacheEvictions,
                memoryCacheEntries = nativeStats.memoryCacheEntries,
                warmTorrents = nativeStats.warmTorrents,
                quiescedTorrents = nativeStats.quiescedTorrents,
                diskCacheCapacityBytes = nativeStats.diskCacheCapacityBytes,
                diskCacheUsedBytes = nativeStats.diskCacheUsedBytes,
                diskCacheProtectedBytes = nativeStats.diskCacheProtectedBytes,
                diskCacheEvictions = nativeStats.diskCacheEvictions,
                diskCacheReclaimedBytes = nativeStats.diskCacheReclaimedBytes,
                diskCacheOverBudget = nativeStats.diskCacheOverBudget.toInt() != 0,
            ),
        )
    }

    override fun streamStats(handle: Long, streamId: String): NativeStreamStatsResult {
        val id = nativeString(streamId)
        val nativeStats = NativeStreamStatsStructure()
        library.nuvio_engine_stream_stats_init_sized(nativeStats, nativeStats.size())
        val status = library.nuvio_engine_get_stream_stats(pointer(handle), id, nativeStats)
        nativeStats.read()
        return NativeStreamStatsResult(
            status,
            NuvioStreamStats(
                fileIndex = nativeStats.fileIndex,
                fileSize = nativeStats.fileSize,
                contiguousReadyBytes = nativeStats.contiguousReadyBytes,
                verifiedFileBytes = nativeStats.verifiedFileBytes,
                deliveredBytes = nativeStats.deliveredBytes,
                activeDemands = nativeStats.activeDemands,
                scheduledPieces = nativeStats.scheduledPieces,
                blockingPieces = nativeStats.blockingPieces,
                primaryBlockingPiece = nativeStats.primaryBlockingPiece,
                secondaryBlockingPiece = nativeStats.secondaryBlockingPiece,
                lastReadyPiece = nativeStats.lastReadyPiece,
                primaryDemandStart = nativeStats.primaryDemandStart,
                primaryDemandEnd = nativeStats.primaryDemandEnd,
                secondaryDemandStart = nativeStats.secondaryDemandStart,
                secondaryDemandEnd = nativeStats.secondaryDemandEnd,
                scheduleRevision = nativeStats.scheduleRevision,
            ),
        )
    }

    override fun reclaimDiskCache(handle: Long, targetBytes: Long): NativeCommandResult =
        command { requestId ->
            library.nuvio_engine_reclaim_disk_cache(pointer(handle), targetBytes, requestId)
        }

    private fun command(call: (LongByReference) -> Int): NativeCommandResult {
        val requestId = LongByReference()
        val status = call(requestId)
        return NativeCommandResult(status, requestId.value)
    }

    private fun checkStatus(status: Int) {
        if (status != STATUS_OK) {
            throw NuvioEngineException(status, statusMessage(status))
        }
    }

    private companion object {
        const val STATUS_OK = 0
        const val STATUS_NO_EVENT = 7
        const val TORRENT_SOURCE_MAGNET = 0
        const val TORRENT_SOURCE_DATA = 1
        const val MAXIMUM_JVM_FILE_COUNT = 1_000_000L

        fun pointer(handle: Long): Pointer? = if (handle == 0L) null else Pointer(handle)

        fun nativeString(text: String): Memory {
            val bytes = text.toByteArray(Charsets.UTF_8)
            return Memory(bytes.size.toLong() + 1).apply {
                write(0, bytes, 0, bytes.size)
                setByte(bytes.size.toLong(), 0)
            }
        }

        fun nativeString(pointer: Pointer?): String =
            pointer?.getString(0, Charsets.UTF_8.name()).orEmpty()

        fun decode(bytes: ByteArray): String {
            val length = bytes.indexOf(0).let { if (it < 0) bytes.size else it }
            return String(bytes, 0, length, Charsets.UTF_8)
        }
    }
}
