package com.nuvio.engine.internal

import com.sun.jna.Library
import com.sun.jna.Native
import com.sun.jna.Pointer
import com.sun.jna.ptr.LongByReference
import com.sun.jna.ptr.PointerByReference
import java.io.File

internal interface JnaNativeLibrary : Library {
    fun nuvio_engine_api_version(): Int
    fun nuvio_engine_version_string(): Pointer?
    fun nuvio_engine_protocol_backend_version(): Pointer?
    fun nuvio_engine_status_message(status: Int): Pointer?
    fun nuvio_engine_config_init_sized(config: NativeConfig, structSize: Int)
    fun nuvio_engine_torrent_request_init_sized(request: NativeTorrentRequest, structSize: Int)
    fun nuvio_engine_event_init_sized(event: NativeEventStructure, structSize: Int)
    fun nuvio_engine_file_init_sized(file: NativeFileStructure, structSize: Int)
    fun nuvio_engine_stream_request_init_sized(request: NativeStreamRequest, structSize: Int)
    fun nuvio_engine_stats_init_sized(stats: NativeStatsStructure, structSize: Int)
    fun nuvio_engine_stream_stats_init_sized(stats: NativeStreamStatsStructure, structSize: Int)
    fun nuvio_engine_create(config: NativeConfig, engine: PointerByReference): Int
    fun nuvio_engine_destroy(engine: Pointer?)
    fun nuvio_engine_add_torrent(
        engine: Pointer?,
        request: NativeTorrentRequest,
        requestId: LongByReference,
    ): Int
    fun nuvio_engine_poll_event(engine: Pointer?, event: NativeEventStructure): Int
    fun nuvio_engine_get_file_count(
        engine: Pointer?,
        torrentId: Pointer?,
        fileCount: SizeTByReference,
    ): Int
    fun nuvio_engine_get_file(
        engine: Pointer?,
        torrentId: Pointer?,
        fileIndex: SizeT,
        file: NativeFileStructure,
    ): Int
    fun nuvio_engine_prepare_stream(
        engine: Pointer?,
        request: NativeStreamRequest,
        requestId: LongByReference,
    ): Int
    fun nuvio_engine_remove_torrent(
        engine: Pointer?,
        torrentId: Pointer?,
        requestId: LongByReference,
    ): Int
    fun nuvio_engine_stop_stream(
        engine: Pointer?,
        streamId: Pointer?,
        requestId: LongByReference,
    ): Int
    fun nuvio_engine_get_stats(engine: Pointer?, stats: NativeStatsStructure): Int
    fun nuvio_engine_get_stream_stats(
        engine: Pointer?,
        streamId: Pointer?,
        stats: NativeStreamStatsStructure,
    ): Int
    fun nuvio_engine_reclaim_disk_cache(
        engine: Pointer?,
        targetBytes: Long,
        requestId: LongByReference,
    ): Int

    companion object {
        fun load(file: File): JnaNativeLibrary = Native.load(
            file.absolutePath,
            JnaNativeLibrary::class.java,
            mapOf(Library.OPTION_STRING_ENCODING to Charsets.UTF_8.name()),
        )
    }
}
