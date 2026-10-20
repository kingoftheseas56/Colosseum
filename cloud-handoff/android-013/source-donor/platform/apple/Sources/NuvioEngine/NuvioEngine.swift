import CNuvioEngine
import Dispatch
import Foundation

public actor NuvioEngine {
    private struct PendingCommand {
        let expectedType: NuvioEventType
        let continuation: CheckedContinuation<NuvioEvent, Error>
    }

    private var handleAddress: UInt
    private var pollingTask: Task<Void, Never>?
    private var pollingFailure: Error?
    private var pendingCommands: [UInt64: PendingCommand] = [:]
    private var eventSubscribers: [UUID: AsyncStream<NuvioEvent>.Continuation] = [:]
    private var statsSubscribers: [UUID: AsyncStream<NuvioEngineStats>.Continuation] = [:]
    private var latestStats = NuvioEngineStats()
    private var lastDroppedEvents: UInt64 = 0
    private var nextStatsSampleNanoseconds: UInt64 = 0

    private init(handle: OpaquePointer) {
        handleAddress = UInt(bitPattern: handle)
    }

    deinit {
        pollingTask?.cancel()
        if let handle = OpaquePointer(bitPattern: handleAddress) {
            nuvio_engine_destroy(handle)
        }
    }

    public nonisolated static var version: String {
        String(cString: nuvio_engine_version_string())
    }

    public nonisolated static var protocolBackendVersion: String {
        String(cString: nuvio_engine_protocol_backend_version())
    }

    public static func create(
        configuration: NuvioEngineConfiguration
    ) async throws -> NuvioEngine {
        let configuration = try configuration.validated()
        let address = try await Task.detached(priority: .userInitiated) {
            try createNativeHandle(configuration: configuration)
        }.value
        if Task.isCancelled {
            if let pointer = OpaquePointer(bitPattern: address) {
                nuvio_engine_destroy(pointer)
            }
            throw CancellationError()
        }
        guard let pointer = OpaquePointer(bitPattern: address) else {
            throw NuvioEngineError(message: "native engine returned an empty handle")
        }
        let engine = NuvioEngine(handle: pointer)
        await engine.startPolling()
        return engine
    }

    public func eventStream() throws -> AsyncStream<NuvioEvent> {
        try ensureOpen()
        let identifier = UUID()
        let (stream, continuation) = AsyncStream.makeStream(
            of: NuvioEvent.self,
            bufferingPolicy: .bufferingNewest(128)
        )
        continuation.onTermination = { [weak self] _ in
            Task { await self?.removeEventSubscriber(identifier) }
        }
        eventSubscribers[identifier] = continuation
        return stream
    }

    public func statsStream() throws -> AsyncStream<NuvioEngineStats> {
        try ensureOpen()
        let identifier = UUID()
        let (stream, continuation) = AsyncStream.makeStream(
            of: NuvioEngineStats.self,
            bufferingPolicy: .bufferingNewest(1)
        )
        continuation.onTermination = { [weak self] _ in
            Task { await self?.removeStatsSubscriber(identifier) }
        }
        statsSubscribers[identifier] = continuation
        continuation.yield(latestStats)
        return stream
    }

    public func addMagnet(_ magnetURI: String) async throws -> String {
        try validateNativeString(magnetURI, name: "magnet URI", limit: maximumMagnetBytes)
        let requestID = try magnetURI.withCString { magnetPointer in
            var request = nuvio_engine_torrent_request()
            nuvio_engine_torrent_request_init_sized(
                &request,
                UInt32(MemoryLayout<nuvio_engine_torrent_request>.size)
            )
            request.magnet_uri = magnetPointer
            request.source_type = 0
            return try submit { handle, requestID in
                nuvio_engine_add_torrent(handle, &request, &requestID)
            }
        }
        let event = try await awaitCommand(
            requestID: requestID,
            expectedType: .torrentMetadataReady
        )
        guard let torrentID = event.torrentID else {
            throw NuvioEngineError(message: "metadata-ready event omitted the torrent ID")
        }
        return torrentID
    }

    public func addTorrent(_ torrentData: Data) async throws -> String {
        guard !torrentData.isEmpty else {
            throw NuvioEngineError(message: "torrent data must not be empty")
        }
        guard torrentData.count <= maximumTorrentBytes else {
            throw NuvioEngineError(message: "torrent data exceeds the 4 MiB native limit")
        }
        let requestID = try torrentData.withUnsafeBytes { bytes in
            guard let baseAddress = bytes.baseAddress else {
                throw NuvioEngineError(message: "torrent data did not expose storage")
            }
            var request = nuvio_engine_torrent_request()
            nuvio_engine_torrent_request_init_sized(
                &request,
                UInt32(MemoryLayout<nuvio_engine_torrent_request>.size)
            )
            request.source_type = 1
            request.torrent_data = baseAddress.assumingMemoryBound(to: UInt8.self)
            request.torrent_data_size = bytes.count
            return try submit { handle, requestID in
                nuvio_engine_add_torrent(handle, &request, &requestID)
            }
        }
        let event = try await awaitCommand(
            requestID: requestID,
            expectedType: .torrentMetadataReady
        )
        guard let torrentID = event.torrentID else {
            throw NuvioEngineError(message: "metadata-ready event omitted the torrent ID")
        }
        return torrentID
    }

    public func files(for torrentID: String) throws -> [NuvioTorrentFile] {
        try validateTorrentID(torrentID)
        let handle = try openHandle()
        return try torrentID.withCString { torrentPointer in
            var count = 0
            try checkNativeStatus(nuvio_engine_get_file_count(handle, torrentPointer, &count))
            var files: [NuvioTorrentFile] = []
            files.reserveCapacity(count)
            for position in 0..<count {
                var nativeFile = nuvio_engine_file()
                nuvio_engine_file_init_sized(
                    &nativeFile,
                    UInt32(MemoryLayout<nuvio_engine_file>.size)
                )
                try checkNativeStatus(
                    nuvio_engine_get_file(handle, torrentPointer, position, &nativeFile)
                )
                let path = fixedCString(&nativeFile.path, capacity: 1024)
                files.append(NuvioTorrentFile(
                    index: nativeFile.index,
                    offset: nativeFile.offset,
                    size: nativeFile.size,
                    path: path,
                    pathWasTruncated: nativeFile.path_truncated != 0
                ))
            }
            return files
        }
    }

    public func prepareStream(
        torrentID: String,
        fileIndex: UInt32? = nil,
        filenameHint: String? = nil
    ) async throws -> NuvioStream {
        try validateTorrentID(torrentID)
        if let filenameHint {
            try validateNativeString(
                filenameHint,
                name: "filename hint",
                limit: maximumFilenameHintBytes
            )
        }
        let requestID = try torrentID.withCString { torrentPointer in
            try withOptionalCString(filenameHint) { hintPointer in
                var request = nuvio_engine_stream_request()
                nuvio_engine_stream_request_init_sized(
                    &request,
                    UInt32(MemoryLayout<nuvio_engine_stream_request>.size)
                )
                request.torrent_id = torrentPointer
                request.file_index = fileIndex ?? UInt32.max
                request.filename_hint = hintPointer
                return try submit { handle, requestID in
                    nuvio_engine_prepare_stream(handle, &request, &requestID)
                }
            }
        }
        let event = try await awaitCommand(requestID: requestID, expectedType: .streamPrepared)
        guard
            let streamID = event.streamID,
            let streamURL = event.streamURL,
            let resolvedTorrentID = event.torrentID,
            let resolvedFileIndex = event.fileIndex
        else {
            throw NuvioEngineError(message: "stream-prepared event omitted required fields")
        }
        return NuvioStream(
            id: streamID,
            url: streamURL,
            torrentID: resolvedTorrentID,
            fileIndex: resolvedFileIndex,
            fileSize: event.fileSize
        )
    }

    public func stopStream(_ streamID: String) async throws {
        try validateStreamID(streamID)
        let requestID = try streamID.withCString { streamPointer in
            try submit { handle, requestID in
                nuvio_engine_stop_stream(handle, streamPointer, &requestID)
            }
        }
        _ = try await awaitCommand(requestID: requestID, expectedType: .streamStopped)
    }

    public func removeTorrent(_ torrentID: String) async throws {
        try validateTorrentID(torrentID)
        let requestID = try torrentID.withCString { torrentPointer in
            try submit { handle, requestID in
                nuvio_engine_remove_torrent(handle, torrentPointer, &requestID)
            }
        }
        _ = try await awaitCommand(requestID: requestID, expectedType: .torrentRemoved)
    }

    @discardableResult
    public func reclaimDiskCache(targetBytes: UInt64 = 0) async throws -> NuvioEvent {
        let requestID = try submit { handle, requestID in
            nuvio_engine_reclaim_disk_cache(handle, targetBytes, &requestID)
        }
        return try await awaitCommand(
            requestID: requestID,
            expectedType: .diskCacheReclaimed
        )
    }

    public func currentStats() throws -> NuvioEngineStats {
        try readStats()
    }

    public func currentStreamStats(_ streamID: String) throws -> NuvioStreamStats {
        try validateStreamID(streamID)
        let handle = try openHandle()
        return try streamID.withCString { streamPointer in
            var nativeStats = nuvio_engine_stream_stats()
            nuvio_engine_stream_stats_init_sized(
                &nativeStats,
                UInt32(MemoryLayout<nuvio_engine_stream_stats>.size)
            )
            try checkNativeStatus(
                nuvio_engine_get_stream_stats(handle, streamPointer, &nativeStats)
            )
            return NuvioStreamStats(
                fileIndex: nativeStats.file_index,
                fileSize: nativeStats.file_size,
                contiguousReadyBytes: nativeStats.contiguous_ready_bytes,
                verifiedFileBytes: nativeStats.verified_file_bytes,
                deliveredBytes: nativeStats.delivered_bytes
            )
        }
    }

    public func preloadStream(
        _ stream: NuvioStream,
        minimumContiguousBytes: UInt64
    ) async throws -> NuvioStreamStats {
        guard minimumContiguousBytes <= maximumPreloadBytes else {
            throw NuvioEngineError(message: "minimum contiguous bytes exceeds 64 MiB")
        }
        try validateStreamID(stream.id)
        let target = min(minimumContiguousBytes, stream.fileSize)
        if target == 0 {
            return try currentStreamStats(stream.id)
        }
        guard
            stream.url.scheme == "http",
            stream.url.host == "127.0.0.1",
            let port = stream.url.port,
            (1...65_535).contains(port),
            stream.url.path == "/stream/\(stream.id)"
        else {
            throw NuvioEngineError(message: "stream URL is not the matching loopback endpoint")
        }
        var request = URLRequest(url: stream.url)
        request.setValue("bytes=0-\(target - 1)", forHTTPHeaderField: "Range")
        request.timeoutInterval = 35
        let (data, response) = try await URLSession.shared.data(for: request)
        guard
            let http = response as? HTTPURLResponse,
            http.statusCode == 206 || http.statusCode == 200,
            UInt64(data.count) >= target
        else {
            throw NuvioEngineError(message: "loopback preload ended before its target")
        }
        for _ in 0..<40 {
            let stats = try currentStreamStats(stream.id)
            if stats.contiguousReadyBytes >= target {
                return stats
            }
            try await Task.sleep(nanoseconds: 25_000_000)
        }
        throw NuvioEngineError(
            message: "verified preload target was not reflected in stream stats"
        )
    }

    public func shutdown() async {
        pollingTask?.cancel()
        pollingTask = nil
        let closedError = NuvioEngineError(message: "NuvioEngine is closed")
        failPending(with: closedError)
        finishSubscribers()
        guard let handle = OpaquePointer(bitPattern: handleAddress) else {
            return
        }
        handleAddress = 0
        let address = UInt(bitPattern: handle)
        await Task.detached(priority: .utility) {
            if let pointer = OpaquePointer(bitPattern: address) {
                nuvio_engine_destroy(pointer)
            }
        }.value
    }

    private func startPolling() {
        guard pollingTask == nil, handleAddress != 0 else {
            return
        }
        pollingTask = Task.detached(priority: .utility) { [weak self] in
            while !Task.isCancelled {
                guard let self else { return }
                await self.pollEventsAndStats()
                do {
                    try await Task.sleep(nanoseconds: 20_000_000)
                } catch {
                    return
                }
            }
        }
    }

    private func pollEventsAndStats() {
        guard let handle = OpaquePointer(bitPattern: handleAddress), pollingFailure == nil else {
            return
        }
        while true {
            var nativeEvent = nuvio_engine_event()
            nuvio_engine_event_init_sized(
                &nativeEvent,
                UInt32(MemoryLayout<nuvio_engine_event>.size)
            )
            let status = nuvio_engine_poll_event(handle, &nativeEvent)
            if status == statusNoEvent {
                break
            }
            guard status == statusOK else {
                failPolling(with: nativeError(status))
                return
            }
            do {
                process(try makeEvent(from: &nativeEvent))
            } catch {
                failPolling(with: error)
                return
            }
        }

        let now = DispatchTime.now().uptimeNanoseconds
        if now >= nextStatsSampleNanoseconds {
            if let stats = try? readStats() {
                latestStats = stats
                for continuation in statsSubscribers.values {
                    continuation.yield(stats)
                }
            }
            nextStatsSampleNanoseconds = now &+ 1_000_000_000
        }
    }

    private func process(_ event: NuvioEvent) {
        if event.droppedEvents > lastDroppedEvents {
            lastDroppedEvents = event.droppedEvents
            failPending(with: NuvioEngineError(
                message: "native event queue dropped \(event.droppedEvents) event(s); resynchronize state"
            ))
        }
        for continuation in eventSubscribers.values {
            continuation.yield(event)
        }
        guard event.requestID != 0, let pending = pendingCommands[event.requestID] else {
            return
        }
        if event.type == .torrentError {
            pendingCommands.removeValue(forKey: event.requestID)
            pending.continuation.resume(throwing: NuvioEngineError(
                message: event.message ?? "torrent operation failed"
            ))
        } else if event.type == pending.expectedType {
            pendingCommands.removeValue(forKey: event.requestID)
            pending.continuation.resume(returning: event)
        }
    }

    private func submit(
        _ body: (OpaquePointer, inout UInt64) -> UInt32
    ) throws -> UInt64 {
        let handle = try openHandle()
        var requestID: UInt64 = 0
        try checkNativeStatus(body(handle, &requestID))
        guard requestID != 0 else {
            throw NuvioEngineError(message: "native command returned an empty request ID")
        }
        return requestID
    }

    private func awaitCommand(
        requestID: UInt64,
        expectedType: NuvioEventType
    ) async throws -> NuvioEvent {
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                if Task.isCancelled {
                    continuation.resume(throwing: CancellationError())
                    return
                }
                pendingCommands[requestID] = PendingCommand(
                    expectedType: expectedType,
                    continuation: continuation
                )
            }
        } onCancel: {
            Task { await self.cancelPending(requestID: requestID) }
        }
    }

    private func cancelPending(requestID: UInt64) {
        guard let pending = pendingCommands.removeValue(forKey: requestID) else {
            return
        }
        pending.continuation.resume(throwing: CancellationError())
    }

    private func makeEvent(from nativeEvent: inout nuvio_engine_event) throws -> NuvioEvent {
        guard let type = NuvioEventType(rawValue: nativeEvent.type) else {
            throw NuvioEngineError(message: "unknown native event type \(nativeEvent.type)")
        }
        let streamURLText = optionalFixedCString(&nativeEvent.stream_url, capacity: 512)
        let streamURL: URL?
        if let streamURLText {
            guard let parsedURL = URL(string: streamURLText) else {
                throw NuvioEngineError(message: "native event contained an invalid stream URL")
            }
            streamURL = parsedURL
        } else {
            streamURL = nil
        }
        return NuvioEvent(
            type: type,
            sequence: nativeEvent.sequence,
            requestID: nativeEvent.request_id,
            droppedEvents: nativeEvent.dropped_events,
            torrentID: optionalFixedCString(&nativeEvent.torrent_id, capacity: 65),
            message: optionalFixedCString(&nativeEvent.message, capacity: 256),
            fileIndex: nativeEvent.file_index == UInt32.max ? nil : nativeEvent.file_index,
            fileSize: nativeEvent.file_size,
            streamID: optionalFixedCString(&nativeEvent.stream_id, capacity: 65),
            streamURL: streamURL
        )
    }

    private func readStats() throws -> NuvioEngineStats {
        let handle = try openHandle()
        var stats = nuvio_engine_stats()
        nuvio_engine_stats_init_sized(
            &stats,
            UInt32(MemoryLayout<nuvio_engine_stats>.size)
        )
        try checkNativeStatus(nuvio_engine_get_stats(handle, &stats))
        return NuvioEngineStats(
            activeTorrents: stats.active_torrents,
            activeStreams: stats.active_streams,
            activeHTTPRequests: stats.active_http_requests,
            connectedPeers: stats.connected_peers,
            connectedSeeds: stats.connected_seeds,
            knownPeers: stats.known_peers,
            connectCandidates: stats.connect_candidates,
            interestedPeers: stats.interested_peers,
            unchokedPeers: stats.unchoked_peers,
            downloadingPeers: stats.downloading_peers,
            snubbedPeers: stats.snubbed_peers,
            pendingBlockRequests: stats.pending_block_requests,
            targetBlockRequests: stats.target_block_requests,
            timedOutBlockRequests: stats.timed_out_block_requests,
            pendingPieceReads: stats.pending_piece_reads,
            downloadRateBytesPerSecond: stats.download_rate_bytes_per_second,
            uploadRateBytesPerSecond: stats.upload_rate_bytes_per_second,
            totalPayloadDownloadBytes: stats.total_payload_download_bytes,
            totalPayloadUploadBytes: stats.total_payload_upload_bytes,
            memoryCacheCapacityBytes: stats.memory_cache_capacity_bytes,
            memoryCacheUsedBytes: stats.memory_cache_used_bytes,
            memoryCacheHits: stats.memory_cache_hits,
            memoryCacheMisses: stats.memory_cache_misses,
            memoryCacheEvictions: stats.memory_cache_evictions,
            memoryCacheEntries: stats.memory_cache_entries,
            warmTorrents: stats.warm_torrents,
            quiescedTorrents: stats.quiesced_torrents,
            diskCacheCapacityBytes: stats.disk_cache_capacity_bytes,
            diskCacheUsedBytes: stats.disk_cache_used_bytes,
            diskCacheProtectedBytes: stats.disk_cache_protected_bytes,
            diskCacheEvictions: stats.disk_cache_evictions,
            diskCacheReclaimedBytes: stats.disk_cache_reclaimed_bytes,
            diskCacheIsOverBudget: stats.disk_cache_over_budget != 0
        )
    }

    private func openHandle() throws -> OpaquePointer {
        try ensureOpen()
        guard let handle = OpaquePointer(bitPattern: handleAddress) else {
            throw NuvioEngineError(message: "NuvioEngine is closed")
        }
        return handle
    }

    private func ensureOpen() throws {
        if let pollingFailure {
            throw pollingFailure
        }
        guard handleAddress != 0 else {
            throw NuvioEngineError(message: "NuvioEngine is closed")
        }
    }

    private func failPolling(with error: Error) {
        pollingFailure = error
        failPending(with: error)
    }

    private func failPending(with error: Error) {
        let commands = pendingCommands.values
        pendingCommands.removeAll()
        for command in commands {
            command.continuation.resume(throwing: error)
        }
    }

    private func finishSubscribers() {
        for continuation in eventSubscribers.values {
            continuation.finish()
        }
        eventSubscribers.removeAll()
        for continuation in statsSubscribers.values {
            continuation.finish()
        }
        statsSubscribers.removeAll()
    }

    private func removeEventSubscriber(_ identifier: UUID) {
        eventSubscribers.removeValue(forKey: identifier)
    }

    private func removeStatsSubscriber(_ identifier: UUID) {
        statsSubscribers.removeValue(forKey: identifier)
    }
}

private func createNativeHandle(configuration: NuvioEngineConfiguration) throws -> UInt {
    guard nuvio_engine_api_version() == 3 else {
        throw NuvioEngineError(message: "incompatible Nuvio Engine C ABI")
    }
    let upload = configuration.nativeUploadConfiguration
    let dataPath = configuration.dataDirectory.path
    let cachePath = configuration.cacheDirectory.path
    let caPath = configuration.tlsCABundle?.path

    return try dataPath.withCString { dataPointer in
        try cachePath.withCString { cachePointer in
            try withOptionalCString(caPath) { caPointer in
                var nativeConfiguration = nuvio_engine_config()
                nuvio_engine_config_init_sized(
                    &nativeConfiguration,
                    UInt32(MemoryLayout<nuvio_engine_config>.size)
                )
                nativeConfiguration.data_directory = dataPointer
                nativeConfiguration.cache_directory = cachePointer
                nativeConfiguration.memory_cache_capacity_bytes =
                    configuration.memoryCacheCapacityBytes
                nativeConfiguration.disk_cache_capacity_bytes =
                    configuration.diskCacheCapacityBytes
                nativeConfiguration.torrent_profile = configuration.torrentProfile.rawValue
                nativeConfiguration.listen_port = configuration.listenPort
                nativeConfiguration.upload_mode = upload.mode
                nativeConfiguration.upload_limit_bytes_per_second = upload.limit
                nativeConfiguration.stream_inactivity_timeout_milliseconds =
                    configuration.streamInactivityTimeoutMilliseconds
                nativeConfiguration.warm_torrent_timeout_milliseconds =
                    configuration.warmTorrentTimeoutMilliseconds
                nativeConfiguration.tls_ca_bundle_path = caPointer

                var handle: OpaquePointer?
                try checkNativeStatus(nuvio_engine_create(&nativeConfiguration, &handle))
                guard let handle else {
                    throw NuvioEngineError(message: "native engine returned an empty handle")
                }
                return UInt(bitPattern: handle)
            }
        }
    }
}
