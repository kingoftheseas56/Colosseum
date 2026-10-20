package com.nuvio.engine

import com.nuvio.engine.internal.FakeNuvioNativeApi
import com.nuvio.engine.internal.NativeFilesResult
import com.nuvio.engine.internal.NativeStatsResult
import com.nuvio.engine.internal.NativeStreamStatsResult
import com.nuvio.engine.internal.nativeEvent
import java.io.File
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue
import kotlinx.coroutines.async
import kotlinx.coroutines.supervisorScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest

@OptIn(ExperimentalCoroutinesApi::class)
class NuvioEngineLifecycleTest {
    private val torrentId = "1".repeat(40)
    private val streamId = "2".repeat(64)

    @Test
    fun commandsCorrelateByRequestIdAndIgnoreIntermediateEvents() = runTest {
        val api = FakeNuvioNativeApi()
        val engine = createEngine(api)
        try {
            val first = async { engine.addMagnet("magnet:?xt=urn:btih:first") }
            val second = async { engine.addMagnet("magnet:?xt=urn:btih:second") }
            runCurrent()
            assertEquals(listOf("magnet:?xt=urn:btih:first", "magnet:?xt=urn:btih:second"), api.submittedMagnets)
            api.enqueue(nativeEvent(type = 1, requestId = 1, torrentId = torrentId))
            api.enqueue(nativeEvent(type = 2, requestId = 2, torrentId = "3".repeat(40)))
            api.enqueue(nativeEvent(type = 2, requestId = 1, torrentId = torrentId))
            advanceTimeBy(20)
            runCurrent()
            assertEquals(torrentId, first.await())
            assertEquals("3".repeat(40), second.await())
        } finally {
            engine.close()
        }
    }

    @Test
    fun torrentDataAndPreparedStreamValuesRoundTrip() = runTest {
        val api = FakeNuvioNativeApi()
        val engine = createEngine(api)
        try {
            val bytes = byteArrayOf(1, 2, 3)
            val added = async { engine.addTorrent(bytes) }
            runCurrent()
            api.enqueue(nativeEvent(type = 2, requestId = 1, torrentId = torrentId))
            advanceTimeBy(20)
            runCurrent()
            assertEquals(torrentId, added.await())
            assertContentEquals(bytes, api.submittedTorrents.single())

            val prepared = async { engine.prepareStream(torrentId, 7, "movie.mkv") }
            runCurrent()
            api.enqueue(
                nativeEvent(
                    type = 4,
                    requestId = 2,
                    torrentId = torrentId,
                    fileIndex = 7,
                    fileSize = 1024,
                    streamId = streamId,
                    streamUrl = "http://127.0.0.1:12345/stream/$streamId",
                ),
            )
            advanceTimeBy(20)
            runCurrent()
            val stream = prepared.await()
            assertEquals(streamId, stream.id)
            assertEquals(7, stream.fileIndex)
            assertEquals(1024, stream.fileSize)
            assertEquals(
                FakeNuvioNativeApi.PreparedStreamCall(torrentId, 7, "movie.mkv"),
                api.preparedStreams.single(),
            )
        } finally {
            engine.close()
        }
    }

    @Test
    fun torrentErrorsFailOnlyTheirCorrelatedCommand() = runTest {
        val api = FakeNuvioNativeApi()
        val engine = createEngine(api)
        try {
            supervisorScope {
                val failed = async { engine.addMagnet("magnet:?xt=urn:btih:failed") }
                val successful = async { engine.addMagnet("magnet:?xt=urn:btih:successful") }
                runCurrent()
                api.enqueue(nativeEvent(type = 3, requestId = 1, message = "metadata failed"))
                api.enqueue(nativeEvent(type = 2, requestId = 2, torrentId = torrentId))
                advanceTimeBy(20)
                runCurrent()
                val error = assertFailsWith<NuvioEngineException> { failed.await() }
                assertEquals("metadata failed", error.message)
                assertEquals(torrentId, successful.await())
            }
        } finally {
            engine.close()
        }
    }

    @Test
    fun droppedNativeEventsFailAllPendingCommands() = runTest {
        val api = FakeNuvioNativeApi()
        val engine = createEngine(api)
        try {
            supervisorScope {
                val first = async { engine.addMagnet("magnet:?xt=urn:btih:first") }
                val second = async { engine.addMagnet("magnet:?xt=urn:btih:second") }
                runCurrent()
                api.enqueue(nativeEvent(type = 1, requestId = 0, droppedEvents = 3))
                advanceTimeBy(20)
                runCurrent()
                assertFailsWith<NuvioEngineException> { first.await() }
                assertFailsWith<NuvioEngineException> { second.await() }
            }
        } finally {
            engine.close()
        }
    }

    @Test
    fun queryAndLifecycleOperationsDelegateAndCloseOnce() = runTest {
        val api = FakeNuvioNativeApi().apply {
            filesResult = NativeFilesResult(
                0,
                listOf(NuvioTorrentFile(0, 0, 100, "movie.mkv", false)),
            )
            statsResult = NativeStatsResult(0, NuvioEngineStats(activeTorrents = 1))
            streamStatsResult = NativeStreamStatsResult(
                0,
                NuvioStreamStats(0, 100, 50, 75, 25),
            )
        }
        val engine = createEngine(api)
        assertEquals("movie.mkv", engine.files(torrentId).single().path)
        assertEquals(1, engine.currentStats().activeTorrents)
        assertEquals(0.5f, engine.currentStreamStats(streamId).bufferProgress)

        val stop = async { engine.stopStream(streamId) }
        runCurrent()
        api.enqueue(nativeEvent(type = 6, requestId = 1, torrentId = torrentId, streamId = streamId))
        advanceTimeBy(20)
        runCurrent()
        stop.await()

        val remove = async { engine.removeTorrent(torrentId) }
        runCurrent()
        api.enqueue(nativeEvent(type = 5, requestId = 2, torrentId = torrentId))
        advanceTimeBy(20)
        runCurrent()
        remove.await()

        val reclaim = async { engine.reclaimDiskCache(4096) }
        runCurrent()
        api.enqueue(nativeEvent(type = 7, requestId = 3, message = "target reached"))
        advanceTimeBy(20)
        runCurrent()
        assertEquals("target reached", reclaim.await().message)
        assertEquals(listOf(streamId), api.stoppedStreams)
        assertEquals(listOf(torrentId), api.removedTorrents)
        assertEquals(listOf(4096L), api.reclaimTargets)

        engine.close()
        engine.close()
        assertEquals(listOf(42L), api.destroyedHandles)
        assertFailsWith<IllegalStateException> { engine.files(torrentId) }
    }

    @Test
    fun stopAndRemoveReturnTheirCompletionEvents() = runTest {
        val api = FakeNuvioNativeApi()
        val engine = createEngine(api)
        try {
            val stop = async { engine.stopStream(streamId) }
            runCurrent()
            api.enqueue(nativeEvent(type = 6, requestId = 1, torrentId = torrentId, streamId = streamId))
            advanceTimeBy(20)
            runCurrent()
            val stopped = stop.await()
            assertEquals(NuvioEventType.StreamStopped, stopped.type)
            assertEquals(streamId, stopped.streamId)

            val remove = async { engine.removeTorrent(torrentId) }
            runCurrent()
            api.enqueue(nativeEvent(type = 5, requestId = 2, torrentId = torrentId))
            advanceTimeBy(20)
            runCurrent()
            val removed = remove.await()
            assertEquals(NuvioEventType.TorrentRemoved, removed.type)
            assertEquals(torrentId, removed.torrentId)
        } finally {
            engine.close()
        }
    }

    @Test
    fun invalidInputsNeverReachNativeCode() = runTest {
        val api = FakeNuvioNativeApi()
        val engine = createEngine(api)
        try {
            assertFailsWith<IllegalArgumentException> { engine.addMagnet("") }
            assertFailsWith<IllegalArgumentException> { engine.addMagnet("a\u0000b") }
            assertFailsWith<IllegalArgumentException> { engine.addTorrent(byteArrayOf()) }
            assertFailsWith<IllegalArgumentException> { engine.files("not-a-hash") }
            assertFailsWith<IllegalArgumentException> { engine.prepareStream(torrentId, -1) }
            assertFailsWith<IllegalArgumentException> {
                engine.prepareStream(torrentId, filenameHint = "a\u0000b")
            }
            assertFailsWith<IllegalArgumentException> { engine.stopStream("short") }
            assertFailsWith<IllegalArgumentException> { engine.reclaimDiskCache(-1) }
            assertTrue(api.submittedMagnets.isEmpty())
            assertTrue(api.submittedTorrents.isEmpty())
            assertTrue(api.preparedStreams.isEmpty())
        } finally {
            engine.close()
        }
    }

    @Test
    fun nativeCommandFailurePreservesStatusMessage() = runTest {
        val api = FakeNuvioNativeApi().apply { commandStatus = 6 }
        val engine = createEngine(api)
        try {
            val error = assertFailsWith<NuvioEngineException> {
                engine.addMagnet("magnet:?xt=urn:btih:test")
            }
            assertEquals(6, error.status)
            assertEquals("status-6", error.message)
        } finally {
            engine.close()
        }
    }

    @Test
    fun requestIdsCannotBeReusedWhilePending() = runTest {
        val api = FakeNuvioNativeApi()
        val engine = createEngine(api)
        try {
            val one = async { engine.addMagnet("magnet:?xt=urn:btih:one") }
            val two = async { engine.addMagnet("magnet:?xt=urn:btih:two") }
            runCurrent()
            assertNotEquals(api.nextRequestId(), 1L)
            api.enqueue(nativeEvent(type = 2, requestId = 1, torrentId = torrentId))
            api.enqueue(nativeEvent(type = 2, requestId = 2, torrentId = torrentId))
            advanceTimeBy(20)
            runCurrent()
            one.await()
            two.await()
        } finally {
            engine.close()
        }
    }

    private fun TestScope.createEngine(api: FakeNuvioNativeApi): NuvioEngine {
        val runtime = NuvioEngineRuntime.create(api)
        return runtime.create(
            NuvioEngineConfig(File("data"), File("cache")),
            StandardTestDispatcher(testScheduler),
        )
    }
}
