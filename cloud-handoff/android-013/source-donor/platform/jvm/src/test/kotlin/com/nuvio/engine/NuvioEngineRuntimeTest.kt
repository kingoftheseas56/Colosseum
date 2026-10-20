package com.nuvio.engine

import com.nuvio.engine.internal.FakeNuvioNativeApi
import com.nuvio.engine.internal.NativeCreateResult
import java.io.File
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class NuvioEngineRuntimeTest {
    @Test
    fun compatibleRuntimeExposesNativeVersions() {
        val api = FakeNuvioNativeApi()
        val runtime = NuvioEngineRuntime.create(api)
        assertEquals("test-engine", runtime.version)
        assertEquals("test-backend", runtime.protocolBackendVersion)
    }

    @Test
    fun incompatibleApiAndMissingBackendAreRejected() {
        assertFailsWith<IllegalArgumentException> {
            NuvioEngineRuntime.create(FakeNuvioNativeApi().apply { reportedApiVersion = 2 })
        }
        assertFailsWith<IllegalArgumentException> {
            NuvioEngineRuntime.create(FakeNuvioNativeApi().apply { reportedBackendVersion = "unavailable" })
        }
        assertFailsWith<IllegalArgumentException> {
            NuvioEngineRuntime.create(FakeNuvioNativeApi().apply { reportedEngineVersion = "" })
        }
    }

    @Test
    fun createFailureUsesNativeStatusAndDoesNotReturnAnEngine() {
        val api = FakeNuvioNativeApi().apply {
            createResult = NativeCreateResult(4, 0)
        }
        val runtime = NuvioEngineRuntime.create(api)
        val error = assertFailsWith<NuvioEngineException> {
            runtime.create(config())
        }
        assertEquals(4, error.status)
        assertEquals("status-4", error.message)
    }

    private fun config() = NuvioEngineConfig(File("data"), File("cache"))
}
