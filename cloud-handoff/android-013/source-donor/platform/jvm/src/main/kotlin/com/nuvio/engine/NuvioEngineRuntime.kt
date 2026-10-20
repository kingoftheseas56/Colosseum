package com.nuvio.engine

import com.nuvio.engine.internal.JnaNativeLibrary
import com.nuvio.engine.internal.JnaNuvioNativeApi
import com.nuvio.engine.internal.NuvioNativeApi
import java.io.File
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers

public class NuvioEngineRuntime private constructor(
    internal val nativeApi: NuvioNativeApi,
    public val version: String,
    public val protocolBackendVersion: String,
) {
    public fun create(
        config: NuvioEngineConfig,
        dispatcher: CoroutineDispatcher = Dispatchers.IO,
    ): NuvioEngine = NuvioEngine.create(nativeApi, config, dispatcher)

    public companion object {
        public const val supportedApiVersion: Int = 3

        @JvmStatic
        public fun load(nativeLibrary: File): NuvioEngineRuntime {
            require(nativeLibrary.isFile) { "native engine library does not exist: $nativeLibrary" }
            val file = nativeLibrary.canonicalFile
            val api = JnaNuvioNativeApi(JnaNativeLibrary.load(file))
            return create(api)
        }

        internal fun create(api: NuvioNativeApi): NuvioEngineRuntime {
            val apiVersion = api.apiVersion()
            require(apiVersion == supportedApiVersion) {
                "unsupported native engine API version $apiVersion; expected $supportedApiVersion"
            }
            val version = api.engineVersion()
            require(version.isNotBlank()) { "native engine version is empty" }
            val backendVersion = api.backendVersion()
            require(backendVersion.isNotBlank() && backendVersion != "unavailable") {
                "native engine protocol backend is unavailable"
            }
            return NuvioEngineRuntime(api, version, backendVersion)
        }
    }
}
