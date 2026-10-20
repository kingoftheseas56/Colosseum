package com.nuvio.engine

import java.io.File
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class NuvioEngineConfigTest {
    private fun config(
        mode: NuvioUploadMode = NuvioUploadMode.Unlimited,
        limit: Long = 0,
    ) = NuvioEngineConfig(
        dataDirectory = File("data"),
        cacheDirectory = File("cache"),
        uploadMode = mode,
        uploadLimitBytesPerSecond = limit,
    )

    @Test
    fun defaultConfigurationIsValid() {
        val configuration = config()
        configuration.validate()
        assertEquals(NuvioTorrentProfile.Balanced, configuration.torrentProfile)
    }

    @Test
    fun uploadModesEnforceTheirRateContract() {
        config(NuvioUploadMode.Disabled, 0).validate()
        config(NuvioUploadMode.Unlimited, 0).validate()
        config(NuvioUploadMode.Limited, 1).validate()
        assertFailsWith<IllegalArgumentException> {
            config(NuvioUploadMode.Limited, 0).validate()
        }
        assertFailsWith<IllegalArgumentException> {
            config(NuvioUploadMode.Unlimited, 1024).validate()
        }
        assertFailsWith<IllegalArgumentException> {
            config(NuvioUploadMode.Disabled, 1024).validate()
        }
    }

    @Test
    fun numericRangesAreValidatedBeforeNativeCalls() {
        assertFailsWith<IllegalArgumentException> {
            config().copy(memoryCacheCapacityBytes = -1).validate()
        }
        assertFailsWith<IllegalArgumentException> {
            config().copy(diskCacheCapacityBytes = -1).validate()
        }
        assertFailsWith<IllegalArgumentException> {
            config().copy(listenPort = 65_536).validate()
        }
        assertFailsWith<IllegalArgumentException> {
            config().copy(streamInactivityTimeoutMilliseconds = -1).validate()
        }
        assertFailsWith<IllegalArgumentException> {
            config().copy(warmTorrentTimeoutMilliseconds = -1).validate()
        }
    }

    @Test
    fun nativePathsRejectEmbeddedNulAndOversizeUtf8() {
        assertFailsWith<IllegalArgumentException> {
            config().copy(dataDirectory = File("bad\u0000path")).validate()
        }
        val oversized = "é".repeat(8193)
        assertTrue(oversized.toByteArray().size > 16 * 1024)
        assertFailsWith<IllegalArgumentException> {
            config().copy(cacheDirectory = File(oversized)).validate()
        }
    }

    @Test
    fun explicitTlsBundleMustBeARegularNonEmptyFile() {
        assertFailsWith<IllegalArgumentException> {
            config().copy(tlsCaBundle = File("missing-ca.pem")).validate()
        }
        val empty = kotlin.io.path.createTempFile().toFile()
        try {
            assertFailsWith<IllegalArgumentException> {
                config().copy(tlsCaBundle = empty).validate()
            }
        } finally {
            empty.delete()
        }
    }

    @Test
    fun streamProgressRatiosAreBounded() {
        val stats = NuvioStreamStats(
            fileIndex = 0,
            fileSize = 100,
            contiguousReadyBytes = 25,
            verifiedFileBytes = 150,
            deliveredBytes = 10,
        )
        assertEquals(0.25f, stats.bufferProgress)
        assertEquals(1f, stats.fileProgress)
        assertEquals(0f, stats.copy(fileSize = 0).bufferProgress)
    }
}
