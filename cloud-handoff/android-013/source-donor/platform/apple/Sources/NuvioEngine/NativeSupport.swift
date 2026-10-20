import CNuvioEngine
import Foundation

let statusOK: UInt32 = 0
let statusNoEvent: UInt32 = 7
let maximumMagnetBytes = 16 * 1024
let maximumTorrentBytes = 4 * 1024 * 1024
let maximumPreloadBytes: UInt64 = 64 * 1024 * 1024
let maximumFilenameHintBytes = 4 * 1024

func nativeError(_ status: UInt32) -> NuvioEngineError {
    let pointer = nuvio_engine_status_message(status)
    let message = pointer.map(String.init(cString:)) ?? "unknown native status \(status)"
    return NuvioEngineError(status: status, message: message)
}

func checkNativeStatus(_ status: UInt32) throws {
    guard status == statusOK else {
        throw nativeError(status)
    }
}

func withOptionalCString<Result>(
    _ value: String?,
    _ body: (UnsafePointer<CChar>?) throws -> Result
) rethrows -> Result {
    guard let value else {
        return try body(nil)
    }
    return try value.withCString(body)
}

func fixedCString<Value>(_ value: inout Value, capacity: Int) -> String {
    withUnsafePointer(to: &value) { pointer in
        pointer.withMemoryRebound(to: CChar.self, capacity: capacity) {
            String(cString: $0)
        }
    }
}

func optionalFixedCString<Value>(_ value: inout Value, capacity: Int) -> String? {
    let result = fixedCString(&value, capacity: capacity)
    return result.isEmpty ? nil : result
}

func validateTorrentID(_ torrentID: String) throws {
    guard (torrentID.count == 40 || torrentID.count == 64), torrentID.allSatisfy(\.isHexDigit) else {
        throw NuvioEngineError(
            message: "torrent ID must be a 40- or 64-character hexadecimal hash"
        )
    }
}

func validateStreamID(_ streamID: String) throws {
    guard streamID.count == 64, streamID.allSatisfy(\.isHexDigit) else {
        throw NuvioEngineError(message: "stream ID must be a 64-character hexadecimal token")
    }
}

private extension Character {
    var isHexDigit: Bool {
        unicodeScalars.count == 1 && unicodeScalars.first.map { scalar in
            switch scalar.value {
            case 48...57, 65...70, 97...102:
                return true
            default:
                return false
            }
        } == true
    }
}
