package com.nuvio.engine.internal

import com.sun.jna.IntegerType
import com.sun.jna.Native
import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.ptr.ByReference

internal abstract class NativeStructure : Structure() {
    fun offsetOf(field: String): Int = fieldOffset(field)
}

@Structure.FieldOrder(
    "structSize",
    "dataDirectory",
    "cacheDirectory",
    "memoryCacheCapacityBytes",
    "diskCacheCapacityBytes",
    "listenPort",
    "reserved0",
    "uploadMode",
    "uploadLimitBytesPerSecond",
    "streamInactivityTimeoutMilliseconds",
    "reserved1",
    "warmTorrentTimeoutMilliseconds",
    "reserved2",
    "tlsCaBundlePath",
    "torrentProfile",
    "reserved3",
)
internal class NativeConfig : NativeStructure() {
    @JvmField var structSize: Int = 0
    @JvmField var dataDirectory: Pointer? = null
    @JvmField var cacheDirectory: Pointer? = null
    @JvmField var memoryCacheCapacityBytes: Long = 0
    @JvmField var diskCacheCapacityBytes: Long = 0
    @JvmField var listenPort: Short = 0
    @JvmField var reserved0: Short = 0
    @JvmField var uploadMode: Int = 0
    @JvmField var uploadLimitBytesPerSecond: Long = 0
    @JvmField var streamInactivityTimeoutMilliseconds: Int = 0
    @JvmField var reserved1: Int = 0
    @JvmField var warmTorrentTimeoutMilliseconds: Int = 0
    @JvmField var reserved2: Int = 0
    @JvmField var tlsCaBundlePath: Pointer? = null
    @JvmField var torrentProfile: Int = 0
    @JvmField var reserved3: Int = 0
}

@Structure.FieldOrder(
    "structSize",
    "magnetUri",
    "sourceType",
    "reserved0",
    "torrentData",
    "torrentDataSize",
)
internal class NativeTorrentRequest : NativeStructure() {
    @JvmField var structSize: Int = 0
    @JvmField var magnetUri: Pointer? = null
    @JvmField var sourceType: Int = 0
    @JvmField var reserved0: Int = 0
    @JvmField var torrentData: Pointer? = null
    @JvmField var torrentDataSize: SizeT = SizeT()
}

@Structure.FieldOrder(
    "structSize",
    "type",
    "sequence",
    "requestId",
    "droppedEvents",
    "torrentId",
    "message",
    "fileIndex",
    "reserved0",
    "fileSize",
    "streamId",
    "streamUrl",
)
internal class NativeEventStructure : NativeStructure() {
    @JvmField var structSize: Int = 0
    @JvmField var type: Int = 0
    @JvmField var sequence: Long = 0
    @JvmField var requestId: Long = 0
    @JvmField var droppedEvents: Long = 0
    @JvmField var torrentId: ByteArray = ByteArray(65)
    @JvmField var message: ByteArray = ByteArray(256)
    @JvmField var fileIndex: Int = 0
    @JvmField var reserved0: Int = 0
    @JvmField var fileSize: Long = 0
    @JvmField var streamId: ByteArray = ByteArray(65)
    @JvmField var streamUrl: ByteArray = ByteArray(512)
}

@Structure.FieldOrder(
    "structSize",
    "torrentId",
    "fileIndex",
    "reserved0",
    "filenameHint",
)
internal class NativeStreamRequest : NativeStructure() {
    @JvmField var structSize: Int = 0
    @JvmField var torrentId: Pointer? = null
    @JvmField var fileIndex: Int = 0
    @JvmField var reserved0: Int = 0
    @JvmField var filenameHint: Pointer? = null
}

@Structure.FieldOrder(
    "structSize",
    "index",
    "offset",
    "size",
    "pathTruncated",
    "reserved0",
    "path",
)
internal class NativeFileStructure : NativeStructure() {
    @JvmField var structSize: Int = 0
    @JvmField var index: Int = 0
    @JvmField var offset: Long = 0
    @JvmField var size: Long = 0
    @JvmField var pathTruncated: Byte = 0
    @JvmField var reserved0: ByteArray = ByteArray(7)
    @JvmField var path: ByteArray = ByteArray(1024)
}

@Structure.FieldOrder(
    "structSize",
    "activeTorrents",
    "activeStreams",
    "activeHttpRequests",
    "connectedPeers",
    "connectedSeeds",
    "pendingPieceReads",
    "reserved0",
    "downloadRateBytesPerSecond",
    "uploadRateBytesPerSecond",
    "totalPayloadDownloadBytes",
    "totalPayloadUploadBytes",
    "memoryCacheCapacityBytes",
    "memoryCacheUsedBytes",
    "memoryCacheHits",
    "memoryCacheMisses",
    "memoryCacheEvictions",
    "memoryCacheEntries",
    "warmTorrents",
    "quiescedTorrents",
    "diskCacheCapacityBytes",
    "diskCacheUsedBytes",
    "diskCacheProtectedBytes",
    "diskCacheEvictions",
    "diskCacheReclaimedBytes",
    "diskCacheOverBudget",
    "reserved1",
    "knownPeers",
    "connectCandidates",
    "interestedPeers",
    "unchokedPeers",
    "downloadingPeers",
    "snubbedPeers",
    "pendingBlockRequests",
    "targetBlockRequests",
    "timedOutBlockRequests",
    "connectingPeers",
    "handshakingPeers",
    "targetPiecePeers",
    "targetPieceUnchokedPeers",
    "targetPieceDownloadingPeers",
    "offTargetDownloadingPeers",
    "trackerReplyEvents",
    "trackerErrorEvents",
    "dhtReplyEvents",
    "reserved2",
    "trackerPeersReturned",
    "dhtPeersReturned",
    "peerConnectEvents",
    "peerDisconnectEvents",
    "peerDisconnectTimeouts",
    "peerDisconnectConnectFailures",
    "peerDisconnectRedundant",
    "peerDisconnectTurnover",
    "peerDisconnectOther",
    "torrentFinishedEvents",
)
internal class NativeStatsStructure : NativeStructure() {
    @JvmField var structSize: Int = 0
    @JvmField var activeTorrents: Int = 0
    @JvmField var activeStreams: Int = 0
    @JvmField var activeHttpRequests: Int = 0
    @JvmField var connectedPeers: Int = 0
    @JvmField var connectedSeeds: Int = 0
    @JvmField var pendingPieceReads: Int = 0
    @JvmField var reserved0: Int = 0
    @JvmField var downloadRateBytesPerSecond: Long = 0
    @JvmField var uploadRateBytesPerSecond: Long = 0
    @JvmField var totalPayloadDownloadBytes: Long = 0
    @JvmField var totalPayloadUploadBytes: Long = 0
    @JvmField var memoryCacheCapacityBytes: Long = 0
    @JvmField var memoryCacheUsedBytes: Long = 0
    @JvmField var memoryCacheHits: Long = 0
    @JvmField var memoryCacheMisses: Long = 0
    @JvmField var memoryCacheEvictions: Long = 0
    @JvmField var memoryCacheEntries: Long = 0
    @JvmField var warmTorrents: Int = 0
    @JvmField var quiescedTorrents: Int = 0
    @JvmField var diskCacheCapacityBytes: Long = 0
    @JvmField var diskCacheUsedBytes: Long = 0
    @JvmField var diskCacheProtectedBytes: Long = 0
    @JvmField var diskCacheEvictions: Long = 0
    @JvmField var diskCacheReclaimedBytes: Long = 0
    @JvmField var diskCacheOverBudget: Byte = 0
    @JvmField var reserved1: ByteArray = ByteArray(7)
    @JvmField var knownPeers: Int = 0
    @JvmField var connectCandidates: Int = 0
    @JvmField var interestedPeers: Int = 0
    @JvmField var unchokedPeers: Int = 0
    @JvmField var downloadingPeers: Int = 0
    @JvmField var snubbedPeers: Int = 0
    @JvmField var pendingBlockRequests: Int = 0
    @JvmField var targetBlockRequests: Int = 0
    @JvmField var timedOutBlockRequests: Int = 0
    @JvmField var connectingPeers: Int = 0
    @JvmField var handshakingPeers: Int = 0
    @JvmField var targetPiecePeers: Int = 0
    @JvmField var targetPieceUnchokedPeers: Int = 0
    @JvmField var targetPieceDownloadingPeers: Int = 0
    @JvmField var offTargetDownloadingPeers: Int = 0
    @JvmField var trackerReplyEvents: Int = 0
    @JvmField var trackerErrorEvents: Int = 0
    @JvmField var dhtReplyEvents: Int = 0
    @JvmField var reserved2: Int = 0
    @JvmField var trackerPeersReturned: Long = 0
    @JvmField var dhtPeersReturned: Long = 0
    @JvmField var peerConnectEvents: Long = 0
    @JvmField var peerDisconnectEvents: Long = 0
    @JvmField var peerDisconnectTimeouts: Long = 0
    @JvmField var peerDisconnectConnectFailures: Long = 0
    @JvmField var peerDisconnectRedundant: Long = 0
    @JvmField var peerDisconnectTurnover: Long = 0
    @JvmField var peerDisconnectOther: Long = 0
    @JvmField var torrentFinishedEvents: Long = 0
}

@Structure.FieldOrder(
    "structSize",
    "fileIndex",
    "fileSize",
    "contiguousReadyBytes",
    "verifiedFileBytes",
    "deliveredBytes",
    "activeDemands",
    "scheduledPieces",
    "blockingPieces",
    "primaryBlockingPiece",
    "secondaryBlockingPiece",
    "lastReadyPiece",
    "primaryDemandStart",
    "primaryDemandEnd",
    "secondaryDemandStart",
    "secondaryDemandEnd",
    "scheduleRevision",
)
internal class NativeStreamStatsStructure : NativeStructure() {
    @JvmField var structSize: Int = 0
    @JvmField var fileIndex: Int = 0
    @JvmField var fileSize: Long = 0
    @JvmField var contiguousReadyBytes: Long = 0
    @JvmField var verifiedFileBytes: Long = 0
    @JvmField var deliveredBytes: Long = 0
    @JvmField var activeDemands: Int = 0
    @JvmField var scheduledPieces: Int = 0
    @JvmField var blockingPieces: Int = 0
    @JvmField var primaryBlockingPiece: Int = 0
    @JvmField var secondaryBlockingPiece: Int = 0
    @JvmField var lastReadyPiece: Int = 0
    @JvmField var primaryDemandStart: Long = 0
    @JvmField var primaryDemandEnd: Long = 0
    @JvmField var secondaryDemandStart: Long = 0
    @JvmField var secondaryDemandEnd: Long = 0
    @JvmField var scheduleRevision: Long = 0
}

internal class SizeT @JvmOverloads constructor(value: Long = 0) :
    IntegerType(Native.SIZE_T_SIZE, value, true) {
    override fun toByte(): Byte = toLong().toByte()
    override fun toShort(): Short = toLong().toShort()
}

internal class SizeTByReference @JvmOverloads constructor(value: Long = 0) :
    ByReference(Native.SIZE_T_SIZE) {
    init {
        setValue(value)
    }

    fun getValue(): Long =
        if (Native.SIZE_T_SIZE == Long.SIZE_BYTES) pointer.getLong(0) else pointer.getInt(0).toLong() and 0xffff_ffffL

    private fun setValue(value: Long) {
        if (Native.SIZE_T_SIZE == Long.SIZE_BYTES) {
            pointer.setLong(0, value)
        } else {
            pointer.setInt(0, value.toInt())
        }
    }
}
