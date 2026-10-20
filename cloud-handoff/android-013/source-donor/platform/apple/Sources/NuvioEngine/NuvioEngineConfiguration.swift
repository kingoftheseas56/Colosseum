import Foundation

public enum NuvioUploadMode: Sendable, Equatable {
    case disabled
    case unlimited
    case limited(bytesPerSecond: UInt64)
}

public enum NuvioTorrentProfile: UInt32, Sendable, Equatable {
    case soft = 0
    case balanced = 1
    case fast = 2
}

public struct NuvioEngineConfiguration: Sendable, Equatable {
    public var dataDirectory: URL
    public var cacheDirectory: URL
    public var memoryCacheCapacityBytes: UInt64
    public var diskCacheCapacityBytes: UInt64
    public var torrentProfile: NuvioTorrentProfile
    public var listenPort: UInt16
    public var uploadMode: NuvioUploadMode
    public var streamInactivityTimeoutMilliseconds: UInt32
    public var warmTorrentTimeoutMilliseconds: UInt32
    public var tlsCABundle: URL?

    public init(
        dataDirectory: URL,
        cacheDirectory: URL,
        memoryCacheCapacityBytes: UInt64 = 64 * 1024 * 1024,
        diskCacheCapacityBytes: UInt64 = 2 * 1024 * 1024 * 1024,
        torrentProfile: NuvioTorrentProfile = .balanced,
        listenPort: UInt16 = 0,
        uploadMode: NuvioUploadMode = .unlimited,
        streamInactivityTimeoutMilliseconds: UInt32 = 30_000,
        warmTorrentTimeoutMilliseconds: UInt32 = 60_000,
        tlsCABundle: URL? = nil
    ) {
        self.dataDirectory = dataDirectory
        self.cacheDirectory = cacheDirectory
        self.memoryCacheCapacityBytes = memoryCacheCapacityBytes
        self.diskCacheCapacityBytes = diskCacheCapacityBytes
        self.torrentProfile = torrentProfile
        self.listenPort = listenPort
        self.uploadMode = uploadMode
        self.streamInactivityTimeoutMilliseconds = streamInactivityTimeoutMilliseconds
        self.warmTorrentTimeoutMilliseconds = warmTorrentTimeoutMilliseconds
        self.tlsCABundle = tlsCABundle
    }
}

extension NuvioEngineConfiguration {
    func validated() throws -> Self {
        try validateDirectoryPath(dataDirectory, name: "data directory")
        try validateDirectoryPath(cacheDirectory, name: "cache directory")
        if case let .limited(bytesPerSecond) = uploadMode, bytesPerSecond == 0 {
            throw NuvioEngineError(message: "limited upload mode requires a positive byte rate")
        }
        if let tlsCABundle {
            guard tlsCABundle.isFileURL else {
                throw NuvioEngineError(message: "TLS CA bundle must be a file URL")
            }
            let values = try tlsCABundle.resourceValues(forKeys: [
                .isRegularFileKey,
                .fileSizeKey,
            ])
            guard values.isRegularFile == true else {
                throw NuvioEngineError(message: "TLS CA bundle must be a regular file")
            }
            let size = values.fileSize ?? 0
            guard size > 0 && size <= 16 * 1024 * 1024 else {
                throw NuvioEngineError(
                    message: "TLS CA bundle must contain between 1 byte and 16 MiB"
                )
            }
            try validateNativeString(tlsCABundle.path, name: "TLS CA bundle path", limit: 16 * 1024)
        }
        return self
    }

    var nativeUploadConfiguration: (mode: UInt32, limit: UInt64) {
        switch uploadMode {
        case .disabled:
            return (0, 0)
        case .unlimited:
            return (1, 0)
        case let .limited(bytesPerSecond):
            return (2, bytesPerSecond)
        }
    }

    private func validateDirectoryPath(_ url: URL, name: String) throws {
        guard url.isFileURL else {
            throw NuvioEngineError(message: "\(name) must be a file URL")
        }
        try validateNativeString(url.path, name: name, limit: 16 * 1024)
    }
}

func validateNativeString(_ value: String, name: String, limit: Int) throws {
    guard !value.isEmpty else {
        throw NuvioEngineError(message: "\(name) must not be empty")
    }
    guard !value.utf8.contains(0) else {
        throw NuvioEngineError(message: "\(name) must not contain NUL bytes")
    }
    guard value.lengthOfBytes(using: .utf8) <= limit else {
        throw NuvioEngineError(message: "\(name) exceeds the \(limit)-byte native limit")
    }
}
