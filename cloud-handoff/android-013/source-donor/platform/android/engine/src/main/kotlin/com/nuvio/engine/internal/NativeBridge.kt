package com.nuvio.engine.internal

internal object NativeBridge {
    init {
        System.loadLibrary("nuvio_engine")
    }

    external fun nativeCreate(
        dataDirectory: String,
        cacheDirectory: String,
        memoryCacheCapacityBytes: Long,
        diskCacheCapacityBytes: Long,
        torrentProfile: Int,
        listenPort: Int,
        uploadMode: Int,
        uploadLimitBytesPerSecond: Long,
        streamInactivityTimeoutMilliseconds: Int,
        warmTorrentTimeoutMilliseconds: Int,
        tlsCaBundlePath: String,
    ): LongArray

    external fun nativeDestroy(handle: Long)
    external fun nativeAddMagnet(handle: Long, magnetUri: String): LongArray
    external fun nativeAddTorrentData(handle: Long, torrentData: ByteArray): LongArray
    external fun nativePollEvent(handle: Long): NativeEventPayload?
    external fun nativeGetFiles(handle: Long, torrentId: String): NativeFilesPayload
    external fun nativePrepareStream(
        handle: Long,
        torrentId: String,
        fileIndex: Int,
        filenameHint: String?,
    ): LongArray
    external fun nativeStopStream(handle: Long, streamId: String): LongArray
    external fun nativeRemoveTorrent(handle: Long, torrentId: String): LongArray
    external fun nativeGetStats(handle: Long): LongArray
    external fun nativeGetStreamStats(handle: Long, streamId: String): LongArray
    external fun nativeReclaimDiskCache(handle: Long, targetBytes: Long): LongArray
    external fun nativeStatusMessage(status: Int): String
    external fun nativeEngineVersion(): String
    external fun nativeBackendVersion(): String
}

internal class NativeEventPayload(
    @JvmField val type: Int,
    @JvmField val sequence: Long,
    @JvmField val requestId: Long,
    @JvmField val droppedEvents: Long,
    @JvmField val torrentId: String,
    @JvmField val message: String,
    @JvmField val fileIndex: Int,
    @JvmField val fileSize: Long,
    @JvmField val streamId: String,
    @JvmField val streamUrl: String,
)

internal class NativeFilePayload(
    @JvmField val index: Int,
    @JvmField val offset: Long,
    @JvmField val size: Long,
    @JvmField val pathTruncated: Boolean,
    @JvmField val path: String,
)

internal class NativeFilesPayload(
    @JvmField val status: Int,
    @JvmField val files: Array<NativeFilePayload>,
)
