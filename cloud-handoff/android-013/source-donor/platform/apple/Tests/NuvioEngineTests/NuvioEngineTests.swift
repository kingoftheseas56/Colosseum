import Foundation
import XCTest
@testable import NuvioEngine

final class NuvioEngineTests: XCTestCase {
    func testConfigurationDefaultsToBalancedTorrentProfile() {
        let root = FileManager.default.temporaryDirectory
        let configuration = NuvioEngineConfiguration(
            dataDirectory: root.appendingPathComponent("data"),
            cacheDirectory: root.appendingPathComponent("cache")
        )
        XCTAssertEqual(configuration.torrentProfile, .balanced)
    }

    func testConfigurationRejectsInvalidUploadLimit() throws {
        let root = FileManager.default.temporaryDirectory
        let configuration = NuvioEngineConfiguration(
            dataDirectory: root.appendingPathComponent("data"),
            cacheDirectory: root.appendingPathComponent("cache"),
            uploadMode: .limited(bytesPerSecond: 0)
        )
        XCTAssertThrowsError(try configuration.validated())
    }

    func testConfigurationRejectsNonFileURLs() throws {
        let configuration = NuvioEngineConfiguration(
            dataDirectory: URL(string: "https://example.invalid/data")!,
            cacheDirectory: FileManager.default.temporaryDirectory
        )
        XCTAssertThrowsError(try configuration.validated())
    }

    func testCreateStatsAndIdempotentShutdown() async throws {
        let directories = try TemporaryDirectories()
        defer { directories.remove() }
        let configuration = NuvioEngineConfiguration(
            dataDirectory: directories.data,
            cacheDirectory: directories.cache,
            memoryCacheCapacityBytes: 8 * 1024 * 1024,
            diskCacheCapacityBytes: 32 * 1024 * 1024
        )
        let engine = try await NuvioEngine.create(configuration: configuration)

        XCTAssertFalse(NuvioEngine.version.isEmpty)
        XCTAssertEqual(NuvioEngine.protocolBackendVersion, "2.0.12.0")
        try await Task.sleep(nanoseconds: 1_100_000_000)
        let snapshot = try await engine.currentStats()
        XCTAssertEqual(snapshot.memoryCacheCapacityBytes, 8 * 1024 * 1024)
        XCTAssertEqual(snapshot.diskCacheCapacityBytes, 32 * 1024 * 1024)

        let firstStream = try await engine.statsStream()
        let secondStream = try await engine.statsStream()
        var firstIterator = firstStream.makeAsyncIterator()
        var secondIterator = secondStream.makeAsyncIterator()
        let firstStats = await firstIterator.next()
        let secondStats = await secondIterator.next()
        XCTAssertNotNil(firstStats)
        XCTAssertNotNil(secondStats)

        let events = try await engine.eventStream()
        var eventIterator = events.makeAsyncIterator()
        await engine.shutdown()
        let eventAfterShutdown = await eventIterator.next()
        XCTAssertNil(eventAfterShutdown)
        await engine.shutdown()
        await XCTAssertThrowsErrorAsync {
            _ = try await engine.currentStats()
        }
    }

    func testInputValidationRunsBeforeNativeSubmission() async throws {
        let directories = try TemporaryDirectories()
        defer { directories.remove() }
        let engine = try await NuvioEngine.create(configuration: .init(
            dataDirectory: directories.data,
            cacheDirectory: directories.cache
        ))

        await XCTAssertThrowsErrorAsync {
            _ = try await engine.addMagnet("")
        }
        await XCTAssertThrowsErrorAsync {
            _ = try await engine.addTorrent(Data())
        }
        await XCTAssertThrowsErrorAsync {
            _ = try await engine.files(for: "not-an-info-hash")
        }
        await XCTAssertThrowsErrorAsync {
            try await engine.stopStream("not-a-stream-token")
        }
        await engine.shutdown()
    }

    func testTorrentStreamLifecycleThroughSwiftActor() async throws {
        let directories = try TemporaryDirectories()
        defer { directories.remove() }
        let engine = try await NuvioEngine.create(configuration: .init(
            dataDirectory: directories.data,
            cacheDirectory: directories.cache
        ))

        var torrent = Data(
            "d4:infod6:lengthi4e4:name8:test.bin12:piece lengthi16384e6:pieces20:".utf8
        )
        torrent.append(Data(repeating: 0, count: 20))
        torrent.append(Data("ee".utf8))

        let firstEvents = try await engine.eventStream()
        let secondEvents = try await engine.eventStream()
        let torrentID = try await engine.addTorrent(torrent)
        XCTAssertEqual(torrentID.count, 40)
        var firstEventIterator = firstEvents.makeAsyncIterator()
        var secondEventIterator = secondEvents.makeAsyncIterator()
        let firstObservedEvent = await firstEventIterator.next()
        let secondObservedEvent = await secondEventIterator.next()
        XCTAssertEqual(firstObservedEvent, secondObservedEvent)
        XCTAssertEqual(firstObservedEvent?.type, .torrentAdded)
        let files = try await engine.files(for: torrentID)
        XCTAssertEqual(files, [NuvioTorrentFile(
            index: 0,
            offset: 0,
            size: 4,
            path: "test.bin",
            pathWasTruncated: false
        )])

        let stream = try await engine.prepareStream(torrentID: torrentID, fileIndex: 0)
        XCTAssertEqual(stream.torrentID, torrentID)
        XCTAssertEqual(stream.fileIndex, 0)
        XCTAssertEqual(stream.fileSize, 4)
        XCTAssertEqual(stream.url.host, "127.0.0.1")
        try await Task.sleep(nanoseconds: 300_000_000)
        let streamStats = try await engine.currentStreamStats(stream.id)
        XCTAssertEqual(streamStats.fileIndex, 0)
        XCTAssertEqual(streamStats.fileSize, 4)
        XCTAssertEqual(streamStats.contiguousReadyBytes, 0)
        XCTAssertEqual(streamStats.bufferProgress, 0)
        try await engine.stopStream(stream.id)
        try await engine.removeTorrent(torrentID)
        await engine.shutdown()
    }
}

private struct TemporaryDirectories {
    let root: URL
    let data: URL
    let cache: URL

    init() throws {
        root = FileManager.default.temporaryDirectory
            .appendingPathComponent("nuvio-swift-tests-\(UUID().uuidString)")
        data = root.appendingPathComponent("data")
        cache = root.appendingPathComponent("cache")
        try FileManager.default.createDirectory(at: data, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: cache, withIntermediateDirectories: true)
    }

    func remove() {
        try? FileManager.default.removeItem(at: root)
    }
}

private func XCTAssertThrowsErrorAsync(
    _ expression: () async throws -> Void,
    file: StaticString = #filePath,
    line: UInt = #line
) async {
    do {
        try await expression()
        XCTFail("expected expression to throw", file: file, line: line)
    } catch {
        // Expected.
    }
}
