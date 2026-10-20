package com.nuvio.engine.internal

import com.sun.jna.Native
import kotlin.test.Test
import kotlin.test.assertEquals

class JnaNativeLayoutTest {
    @Test
    fun stableAbiStructuresMatch64BitCLayout() {
        assertEquals(8, Native.POINTER_SIZE)
        assertEquals(8, Native.SIZE_T_SIZE)
        assertEquals(88, NativeConfig().size())
        assertEquals(40, NativeTorrentRequest().size())
        assertEquals(960, NativeEventStructure().size())
        assertEquals(32, NativeStreamRequest().size())
        assertEquals(1056, NativeFileStructure().size())
        assertEquals(328, NativeStatsStructure().size())
        assertEquals(104, NativeStreamStatsStructure().size())
    }

    @Test
    fun criticalAbiOffsetsMatchCAlignment() {
        assertEquals(8, NativeConfig().offsetOf("dataDirectory"))
        assertEquals(40, NativeConfig().offsetOf("listenPort"))
        assertEquals(72, NativeConfig().offsetOf("tlsCaBundlePath"))
        assertEquals(24, NativeTorrentRequest().offsetOf("torrentData"))
        assertEquals(368, NativeEventStructure().offsetOf("fileSize"))
        assertEquals(376, NativeEventStructure().offsetOf("streamId"))
        assertEquals(32, NativeFileStructure().offsetOf("path"))
        assertEquals(248, NativeStatsStructure().offsetOf("trackerPeersReturned"))
        assertEquals(64, NativeStreamStatsStructure().offsetOf("primaryDemandStart"))
    }
}
