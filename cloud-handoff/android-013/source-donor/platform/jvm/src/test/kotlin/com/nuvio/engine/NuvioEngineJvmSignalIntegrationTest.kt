package com.nuvio.engine

import java.io.File
import java.net.HttpURLConnection
import java.net.URI
import java.security.MessageDigest
import kotlin.io.path.createTempDirectory
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.jupiter.api.Assumptions.assumeTrue

class NuvioEngineJvmSignalIntegrationTest {
    @Test
    fun largeTorrentReadsPreserveJvmSafepoints() = runBlocking<Unit> {
        val libraryPath = System.getProperty("nuvio.engine.testLibrary").orEmpty()
        assumeTrue(libraryPath.isNotBlank(), "nuvio.engine.testLibrary was not provided")
        val library = File(libraryPath)
        assumeTrue(library.isFile, "native test library does not exist")

        val fixture = torrentFixture(ByteArray(1024 * 1024) { it.toByte() })
        val root = createTempDirectory("nuvio-engine-jvm-signal-").toFile()
        val dataDirectory = File(root, "data")
        val cacheDirectory = File(root, "cache")
        val payload = File(cacheDirectory, "payload/${fixture.id}/mapped.bin")
        payload.parentFile.mkdirs()
        payload.writeBytes(fixture.content)
        val engine = NuvioEngineRuntime.load(library).create(
            NuvioEngineConfig(
                dataDirectory = dataDirectory,
                cacheDirectory = cacheDirectory,
                memoryCacheCapacityBytes = 2 * 1024 * 1024,
                diskCacheCapacityBytes = 2 * 1024 * 1024,
                uploadMode = NuvioUploadMode.Disabled,
            ),
        )

        try {
            val torrentId = withTimeout(10_000) { engine.addTorrent(fixture.torrentData) }
            assertEquals(fixture.id, torrentId)
            val stream = withTimeout(10_000) { engine.prepareStream(torrentId, fileIndex = 0) }
            assertContentEquals(fixture.content, request(stream.url))
            forceJvmSafepoints()
            withTimeout(10_000) { engine.stopStream(stream.id) }
        } finally {
            engine.close()
            root.deleteRecursively()
        }
    }

    private fun request(url: String): ByteArray {
        val connection = URI(url).toURL().openConnection() as HttpURLConnection
        return try {
            connection.connectTimeout = 5_000
            connection.readTimeout = 10_000
            connection.setRequestProperty("Connection", "close")
            assertEquals(200, connection.responseCode)
            connection.inputStream.use { it.readBytes() }
        } finally {
            connection.disconnect()
        }
    }

    private fun forceJvmSafepoints() {
        repeat(8) {
            System.gc()
            System.runFinalization()
            Thread.sleep(10)
        }
    }

    private fun torrentFixture(content: ByteArray): TorrentFixture {
        val pieceLength = 16 * 1024
        val pieces = pieceHashes(content, pieceLength)
        val info = "d6:lengthi${content.size}e4:name10:mapped.bin12:piece lengthi${pieceLength}e6:pieces${pieces.size}:"
            .toByteArray() + pieces + "e".toByteArray()
        return TorrentFixture(
            id = sha1(info).joinToString("") { "%02x".format(it) },
            content = content,
            torrentData = "d4:info".toByteArray() + info + "e".toByteArray(),
        )
    }

    private fun pieceHashes(content: ByteArray, pieceLength: Int): ByteArray {
        val pieceCount = (content.size + pieceLength - 1) / pieceLength
        val hashes = ByteArray(pieceCount * 20)
        repeat(pieceCount) { pieceIndex ->
            val start = pieceIndex * pieceLength
            val end = minOf(content.size, start + pieceLength)
            sha1(content.copyOfRange(start, end)).copyInto(hashes, pieceIndex * 20)
        }
        return hashes
    }

    private fun sha1(value: ByteArray): ByteArray =
        MessageDigest.getInstance("SHA-1").digest(value)

    private data class TorrentFixture(
        val id: String,
        val content: ByteArray,
        val torrentData: ByteArray,
    )
}
