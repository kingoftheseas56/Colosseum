package com.nuvio.engine

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NuvioEngineInstrumentedTest {
    @Test
    fun createsEngineWithExportedAndroidTrustStore() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val root = File(context.noBackupFilesDir, "nuvio-engine-instrumented").apply {
            deleteRecursively()
            mkdirs()
        }
        val dataDirectory = File(root, "data")
        val cacheDirectory = File(root, "cache")

        val engine = NuvioEngine.create(
            NuvioEngineConfig(
                dataDirectory = dataDirectory,
                cacheDirectory = cacheDirectory,
                memoryCacheCapacityBytes = 1024 * 1024,
                diskCacheCapacityBytes = 0,
                uploadMode = NuvioUploadMode.Disabled,
                streamInactivityTimeoutMilliseconds = 0,
                warmTorrentTimeoutMilliseconds = 0,
            ),
        )
        try {
            assertEquals("0.1.5", NuvioEngine.version)
            assertTrue(NuvioEngine.protocolBackendVersion.startsWith("2.0.12"))
            val trustBundle = File(dataDirectory, "tls/android-ca.pem")
            assertTrue(trustBundle.isFile)
            assertTrue(trustBundle.length() > 0)
            assertTrue(trustBundle.readText().contains("-----BEGIN CERTIFICATE-----"))
        } finally {
            engine.close()
            root.deleteRecursively()
        }
    }
}
