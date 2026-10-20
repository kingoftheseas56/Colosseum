package com.nuvio.engine

import java.io.File
import java.net.HttpURLConnection
import java.net.URI
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.io.path.createTempDirectory
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue
import kotlinx.coroutines.delay
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.yield
import org.junit.jupiter.api.Assumptions.assumeTrue

class NuvioEngineNativeIntegrationTest {
    @Test
    fun packagedLibraryStreamsSeededTorrentAcrossItsFullLifecycle() = runBlocking<Unit> {
        val libraryPath = System.getProperty("nuvio.engine.testLibrary").orEmpty()
        assumeTrue(libraryPath.isNotBlank(), "nuvio.engine.testLibrary was not provided")
        val library = File(libraryPath)
        assumeTrue(library.isFile, "native test library does not exist")

        val root = createTempDirectory("nuvio-engine-jvm-").toFile()
        val dataDirectory = File(root, "data")
        val cacheDirectory = File(root, "cache")
        val payload = File(cacheDirectory, "payload/$TORRENT_ID/test.bin")
        payload.parentFile.mkdirs()
        payload.writeBytes(CONTENT)

        val runtime = NuvioEngineRuntime.load(library)
        assertEquals("0.1.5", runtime.version)
        assertTrue(runtime.protocolBackendVersion.contains("2.0.12"))
        val engine = runtime.create(
            NuvioEngineConfig(
                dataDirectory = dataDirectory,
                cacheDirectory = cacheDirectory,
                memoryCacheCapacityBytes = 1024 * 1024,
                diskCacheCapacityBytes = 1024 * 1024,
                uploadMode = NuvioUploadMode.Disabled,
                warmTorrentTimeoutMilliseconds = 50,
            ),
        )

        try {
            val torrentId = withTimeout(10_000) { engine.addTorrent(torrentData()) }
            assertEquals(TORRENT_ID, torrentId)
            assertEquals(
                listOf(NuvioTorrentFile(0, 0, 4, "test.bin", false)),
                engine.files(torrentId),
            )

            val firstStream = withTimeout(10_000) {
                engine.prepareStream(torrentId, fileIndex = 0)
            }
            assertEquals(torrentId, firstStream.torrentId)
            assertEquals(0, firstStream.fileIndex)
            assertEquals(4, firstStream.fileSize)

            val head = request(firstStream.url, "HEAD")
            assertEquals(200, head.status)
            assertEquals("4", head.contentLength)
            assertTrue(head.body.isEmpty())

            val partial = request(firstStream.url, range = "bytes=1-2")
            assertEquals(206, partial.status)
            assertEquals("bytes 1-2/4", partial.contentRange)
            assertContentEquals("es".toByteArray(), partial.body)

            val full = request(firstStream.url)
            assertEquals(200, full.status)
            assertContentEquals(CONTENT, full.body)

            val denied = request(
                URI(firstStream.url).let {
                    "http://127.0.0.1:${it.port}/stream/${"0".repeat(64)}"
                },
            )
            assertEquals(404, denied.status)

            val stats = awaitStats(engine) {
                it.memoryCacheHits > 0 && it.activeHttpRequests == 0
            }
            assertEquals(1, stats.activeTorrents)
            assertEquals(1, stats.activeStreams)
            assertEquals(0, stats.pendingPieceReads)
            assertEquals(4, stats.memoryCacheUsedBytes)
            assertEquals(1, stats.memoryCacheEntries)
            assertTrue(stats.memoryCacheMisses > 0)

            val streamStats = awaitStreamStats(engine, firstStream.id) {
                it.contiguousReadyBytes == 4L && it.deliveredBytes >= 6
            }
            assertEquals(4, streamStats.verifiedFileBytes)
            assertEquals(1f, streamStats.bufferProgress)
            assertEquals(1f, streamStats.fileProgress)

            withTimeout(10_000) { engine.stopStream(firstStream.id) }
            assertEquals(404, request(firstStream.url).status)
            val stoppedStats = awaitStats(engine) {
                it.activeStreams == 0 && it.quiescedTorrents == 1
            }
            assertEquals(1, stoppedStats.warmTorrents)

            val warmStream = withTimeout(10_000) {
                engine.prepareStream(torrentId, fileIndex = 0)
            }
            assertNotEquals(firstStream.id, warmStream.id)
            assertContentEquals(CONTENT, request(warmStream.url).body)
            awaitStats(engine) { it.warmTorrents == 0 && it.quiescedTorrents == 0 }

            val protectedReclaim = withTimeout(10_000) { engine.reclaimDiskCache() }
            assertTrue(protectedReclaim.message.orEmpty().contains("protected"))
            assertTrue(payload.isFile)
            assertEquals(4, engine.currentStats().diskCacheProtectedBytes)

            withTimeout(10_000) { engine.removeTorrent(torrentId) }
            assertEquals(404, request(warmStream.url).status)
            val reclaimed = withTimeout(10_000) { engine.reclaimDiskCache() }
            assertTrue(reclaimed.message.orEmpty().contains("target reached"))
            awaitStats(engine) { it.diskCacheUsedBytes == 0L }
            assertFalse(payload.exists())
        } finally {
            engine.close()
            root.deleteRecursively()
        }
    }

    @Test
    fun protectedDiskCachePressureRemainsNonfatalThroughJvmBinding() = runBlocking<Unit> {
        val libraryPath = System.getProperty("nuvio.engine.testLibrary").orEmpty()
        assumeTrue(libraryPath.isNotBlank(), "nuvio.engine.testLibrary was not provided")
        val library = File(libraryPath)
        assumeTrue(library.isFile, "native test library does not exist")

        val root = createTempDirectory("nuvio-engine-cache-pressure-").toFile()
        val dataDirectory = File(root, "data")
        val cacheDirectory = File(root, "cache")
        val payload = File(cacheDirectory, "payload/$TORRENT_ID/test.bin")
        payload.parentFile.mkdirs()
        payload.writeBytes(CONTENT)
        val engine = NuvioEngineRuntime.load(library).create(
            NuvioEngineConfig(
                dataDirectory = dataDirectory,
                cacheDirectory = cacheDirectory,
                memoryCacheCapacityBytes = 1024 * 1024,
                diskCacheCapacityBytes = 3,
                uploadMode = NuvioUploadMode.Disabled,
                streamInactivityTimeoutMilliseconds = 0,
            ),
        )
        val errors = CopyOnWriteArrayList<NuvioEvent>()
        val collector = launch {
            engine.events.collect { event ->
                if (event.type == NuvioEventType.TorrentError) errors += event
            }
        }

        try {
            yield()
            val torrentId = withTimeout(10_000) { engine.addTorrent(torrentData()) }
            val stream = withTimeout(10_000) { engine.prepareStream(torrentId, fileIndex = 0) }
            assertContentEquals(CONTENT, request(stream.url).body)

            val stats = awaitStats(engine) {
                it.diskCacheOverBudget &&
                    it.diskCacheUsedBytes == CONTENT.size.toLong() &&
                    it.diskCacheProtectedBytes == CONTENT.size.toLong()
            }
            assertEquals(3L, stats.diskCacheCapacityBytes)
            delay(250)
            assertTrue(errors.isEmpty(), "protected cache pressure emitted $errors")

            withTimeout(10_000) { engine.stopStream(stream.id) }
        } finally {
            collector.cancelAndJoin()
            engine.close()
            root.deleteRecursively()
        }
    }

    private suspend fun awaitStats(
        engine: NuvioEngine,
        predicate: (NuvioEngineStats) -> Boolean,
    ): NuvioEngineStats = withTimeout(5_000) {
        while (true) {
            val stats = engine.currentStats()
            if (predicate(stats)) {
                return@withTimeout stats
            }
            delay(25)
        }
        error("unreachable")
    }

    private suspend fun awaitStreamStats(
        engine: NuvioEngine,
        streamId: String,
        predicate: (NuvioStreamStats) -> Boolean,
    ): NuvioStreamStats = withTimeout(5_000) {
        while (true) {
            val stats = engine.currentStreamStats(streamId)
            if (predicate(stats)) {
                return@withTimeout stats
            }
            delay(25)
        }
        error("unreachable")
    }

    private fun request(
        url: String,
        method: String = "GET",
        range: String? = null,
    ): HttpResult {
        val connection = URI(url).toURL().openConnection() as HttpURLConnection
        return try {
            connection.requestMethod = method
            connection.connectTimeout = 5_000
            connection.readTimeout = 5_000
            connection.setRequestProperty("Connection", "close")
            range?.let { connection.setRequestProperty("Range", it) }
            val status = connection.responseCode
            val body = if (method == "HEAD") {
                byteArrayOf()
            } else {
                (if (status >= 400) connection.errorStream else connection.inputStream)
                    ?.use { it.readBytes() }
                    ?: byteArrayOf()
            }
            HttpResult(
                status,
                connection.getHeaderField("Content-Length"),
                connection.getHeaderField("Content-Range"),
                body,
            )
        } finally {
            connection.disconnect()
        }
    }

    private fun torrentData(): ByteArray =
        "d4:infod6:lengthi4e4:name8:test.bin12:piece lengthi16384e6:pieces20:"
            .toByteArray() + PIECE_HASH + "ee".toByteArray()

    private data class HttpResult(
        val status: Int,
        val contentLength: String?,
        val contentRange: String?,
        val body: ByteArray,
    )

    private companion object {
        const val TORRENT_ID = "13ade5f13f4e7ce4021a3cc82f72e504b9ef35ac"
        val CONTENT = "test".toByteArray()
        val PIECE_HASH = byteArrayOf(
            0xa9.toByte(), 0x4a, 0x8f.toByte(), 0xe5.toByte(), 0xcc.toByte(),
            0xb1.toByte(), 0x9b.toByte(), 0xa6.toByte(), 0x1c, 0x4c,
            0x08, 0x73, 0xd3.toByte(), 0x91.toByte(), 0xe9.toByte(),
            0x87.toByte(), 0x98.toByte(), 0x2f, 0xbb.toByte(), 0xd3.toByte(),
        )
    }
}
