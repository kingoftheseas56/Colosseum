import Foundation

public enum NuvioEventType: UInt32, Sendable, CaseIterable {
    case torrentAdded = 1
    case torrentMetadataReady = 2
    case torrentError = 3
    case streamPrepared = 4
    case torrentRemoved = 5
    case streamStopped = 6
    case diskCacheReclaimed = 7
}

public struct NuvioEvent: Sendable, Equatable {
    public let type: NuvioEventType
    public let sequence: UInt64
    public let requestID: UInt64
    public let droppedEvents: UInt64
    public let torrentID: String?
    public let message: String?
    public let fileIndex: UInt32?
    public let fileSize: UInt64
    public let streamID: String?
    public let streamURL: URL?
}

public struct NuvioTorrentFile: Sendable, Equatable {
    public let index: UInt32
    public let offset: UInt64
    public let size: UInt64
    public let path: String
    public let pathWasTruncated: Bool
}

public struct NuvioStream: Sendable, Equatable {
    public let id: String
    public let url: URL
    public let torrentID: String
    public let fileIndex: UInt32
    public let fileSize: UInt64
}

public struct NuvioStreamStats: Sendable, Equatable {
    public let fileIndex: UInt32
    public let fileSize: UInt64
    public let contiguousReadyBytes: UInt64
    public let verifiedFileBytes: UInt64
    public let deliveredBytes: UInt64

    public var bufferProgress: Double { ratio(contiguousReadyBytes, fileSize) }
    public var fileProgress: Double { ratio(verifiedFileBytes, fileSize) }
}

public struct NuvioEngineStats: Sendable, Equatable {
    public let activeTorrents: UInt32
    public let activeStreams: UInt32
    public let activeHTTPRequests: UInt32
    public let connectedPeers: UInt32
    public let connectedSeeds: UInt32
    public let knownPeers: UInt32
    public let connectCandidates: UInt32
    public let interestedPeers: UInt32
    public let unchokedPeers: UInt32
    public let downloadingPeers: UInt32
    public let snubbedPeers: UInt32
    public let pendingBlockRequests: UInt32
    public let targetBlockRequests: UInt32
    public let timedOutBlockRequests: UInt32
    public let pendingPieceReads: UInt32
    public let downloadRateBytesPerSecond: UInt64
    public let uploadRateBytesPerSecond: UInt64
    public let totalPayloadDownloadBytes: UInt64
    public let totalPayloadUploadBytes: UInt64
    public let memoryCacheCapacityBytes: UInt64
    public let memoryCacheUsedBytes: UInt64
    public let memoryCacheHits: UInt64
    public let memoryCacheMisses: UInt64
    public let memoryCacheEvictions: UInt64
    public let memoryCacheEntries: UInt64
    public let warmTorrents: UInt32
    public let quiescedTorrents: UInt32
    public let diskCacheCapacityBytes: UInt64
    public let diskCacheUsedBytes: UInt64
    public let diskCacheProtectedBytes: UInt64
    public let diskCacheEvictions: UInt64
    public let diskCacheReclaimedBytes: UInt64
    public let diskCacheIsOverBudget: Bool

    public init(
        activeTorrents: UInt32 = 0,
        activeStreams: UInt32 = 0,
        activeHTTPRequests: UInt32 = 0,
        connectedPeers: UInt32 = 0,
        connectedSeeds: UInt32 = 0,
        knownPeers: UInt32 = 0,
        connectCandidates: UInt32 = 0,
        interestedPeers: UInt32 = 0,
        unchokedPeers: UInt32 = 0,
        downloadingPeers: UInt32 = 0,
        snubbedPeers: UInt32 = 0,
        pendingBlockRequests: UInt32 = 0,
        targetBlockRequests: UInt32 = 0,
        timedOutBlockRequests: UInt32 = 0,
        pendingPieceReads: UInt32 = 0,
        downloadRateBytesPerSecond: UInt64 = 0,
        uploadRateBytesPerSecond: UInt64 = 0,
        totalPayloadDownloadBytes: UInt64 = 0,
        totalPayloadUploadBytes: UInt64 = 0,
        memoryCacheCapacityBytes: UInt64 = 0,
        memoryCacheUsedBytes: UInt64 = 0,
        memoryCacheHits: UInt64 = 0,
        memoryCacheMisses: UInt64 = 0,
        memoryCacheEvictions: UInt64 = 0,
        memoryCacheEntries: UInt64 = 0,
        warmTorrents: UInt32 = 0,
        quiescedTorrents: UInt32 = 0,
        diskCacheCapacityBytes: UInt64 = 0,
        diskCacheUsedBytes: UInt64 = 0,
        diskCacheProtectedBytes: UInt64 = 0,
        diskCacheEvictions: UInt64 = 0,
        diskCacheReclaimedBytes: UInt64 = 0,
        diskCacheIsOverBudget: Bool = false
    ) {
        self.activeTorrents = activeTorrents
        self.activeStreams = activeStreams
        self.activeHTTPRequests = activeHTTPRequests
        self.connectedPeers = connectedPeers
        self.connectedSeeds = connectedSeeds
        self.knownPeers = knownPeers
        self.connectCandidates = connectCandidates
        self.interestedPeers = interestedPeers
        self.unchokedPeers = unchokedPeers
        self.downloadingPeers = downloadingPeers
        self.snubbedPeers = snubbedPeers
        self.pendingBlockRequests = pendingBlockRequests
        self.targetBlockRequests = targetBlockRequests
        self.timedOutBlockRequests = timedOutBlockRequests
        self.pendingPieceReads = pendingPieceReads
        self.downloadRateBytesPerSecond = downloadRateBytesPerSecond
        self.uploadRateBytesPerSecond = uploadRateBytesPerSecond
        self.totalPayloadDownloadBytes = totalPayloadDownloadBytes
        self.totalPayloadUploadBytes = totalPayloadUploadBytes
        self.memoryCacheCapacityBytes = memoryCacheCapacityBytes
        self.memoryCacheUsedBytes = memoryCacheUsedBytes
        self.memoryCacheHits = memoryCacheHits
        self.memoryCacheMisses = memoryCacheMisses
        self.memoryCacheEvictions = memoryCacheEvictions
        self.memoryCacheEntries = memoryCacheEntries
        self.warmTorrents = warmTorrents
        self.quiescedTorrents = quiescedTorrents
        self.diskCacheCapacityBytes = diskCacheCapacityBytes
        self.diskCacheUsedBytes = diskCacheUsedBytes
        self.diskCacheProtectedBytes = diskCacheProtectedBytes
        self.diskCacheEvictions = diskCacheEvictions
        self.diskCacheReclaimedBytes = diskCacheReclaimedBytes
        self.diskCacheIsOverBudget = diskCacheIsOverBudget
    }
}

public struct NuvioEngineError: Error, LocalizedError, Sendable, Equatable {
    public let status: UInt32?
    public let message: String

    public var errorDescription: String? { message }

    public init(status: UInt32? = nil, message: String) {
        self.status = status
        self.message = message
    }
}

private func ratio(_ value: UInt64, _ total: UInt64) -> Double {
    guard total > 0 else { return 0 }
    return min(max(Double(value) / Double(total), 0), 1)
}
