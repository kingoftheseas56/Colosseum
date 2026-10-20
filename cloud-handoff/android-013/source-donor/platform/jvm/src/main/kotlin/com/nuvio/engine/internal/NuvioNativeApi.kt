package com.nuvio.engine.internal

import com.nuvio.engine.NuvioEngineConfig
import com.nuvio.engine.NuvioEngineStats
import com.nuvio.engine.NuvioStreamStats
import com.nuvio.engine.NuvioTorrentFile

internal data class NativeCreateResult(val status: Int, val handle: Long)
internal data class NativeCommandResult(val status: Int, val requestId: Long)
internal data class NativeFilesResult(val status: Int, val files: List<NuvioTorrentFile>)
internal data class NativeStatsResult(val status: Int, val stats: NuvioEngineStats)
internal data class NativeStreamStatsResult(val status: Int, val stats: NuvioStreamStats)

internal data class NativeEvent(
    val type: Int,
    val sequence: Long,
    val requestId: Long,
    val droppedEvents: Long,
    val torrentId: String,
    val message: String,
    val fileIndex: Int,
    val fileSize: Long,
    val streamId: String,
    val streamUrl: String,
)

internal interface NuvioNativeApi {
    fun apiVersion(): Int
    fun engineVersion(): String
    fun backendVersion(): String
    fun statusMessage(status: Int): String
    fun create(config: NuvioEngineConfig): NativeCreateResult
    fun destroy(handle: Long)
    fun addMagnet(handle: Long, magnetUri: String): NativeCommandResult
    fun addTorrent(handle: Long, torrentData: ByteArray): NativeCommandResult
    fun pollEvent(handle: Long): NativeEvent?
    fun files(handle: Long, torrentId: String): NativeFilesResult
    fun prepareStream(
        handle: Long,
        torrentId: String,
        fileIndex: Int?,
        filenameHint: String?,
    ): NativeCommandResult
    fun stopStream(handle: Long, streamId: String): NativeCommandResult
    fun removeTorrent(handle: Long, torrentId: String): NativeCommandResult
    fun stats(handle: Long): NativeStatsResult
    fun streamStats(handle: Long, streamId: String): NativeStreamStatsResult
    fun reclaimDiskCache(handle: Long, targetBytes: Long): NativeCommandResult
}
