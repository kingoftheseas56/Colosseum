#include "colosseum_server/torrent/model/MetadataExchange.h"
#include "colosseum_server/torrent/model/TorrentMetadata.h"
#include "colosseum_server/torrent/model/VirtualPieceMap.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

using namespace ColosseumServer::Torrent;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool bit(const QByteArray& bytes, int index)
{
    return index >= 0 && (index >> 3) < bytes.size()
        && (static_cast<unsigned char>(bytes[index >> 3]) & (0x80u >> (index & 7)));
}
QByteArray fixture(const char* name)
{
    QFile file(QString::fromUtf8(ARC44_W04_FIXTURE_DIR) + QLatin1Char('/') + QString::fromUtf8(name));
    require(file.open(QIODevice::ReadOnly), "fixed .torrent fixture opens");
    return file.readAll();
}

} // namespace

int main()
{
    const QByteArray torrentBytes = fixture("multifile.torrent");
    QString error;
    auto parsed = TorrentMetadataCodec::parseTorrent(torrentBytes, &error);
    require(parsed.has_value(), "fixed multifile torrent parses");
    require(parsed->infoHash == QStringLiteral("359f54dcfa44cc59e025ce4ff0192e66d9f2d424"),
            "infoHash is SHA-1 of exact info section");
    require(parsed->name == QStringLiteral("Arc44Pack"), "torrent name preserved");
    require(parsed->files.size() == 3, "all torrent files modeled");
    require(parsed->files[0].index == 0 && parsed->files[0].offset == 0,
            "first file index and offset");
    require(parsed->files[1].index == 1 && parsed->files[1].offset == 700000,
            "second file cumulative offset");
    require(parsed->files[2].offset == 1700000 && parsed->length == 1712345,
            "third file offset and torrent length");
    require(parsed->pieceLength == 1048576 && parsed->pieceHashes.size() == 2,
            "original verification piece geometry preserved");
    require(parsed->lastPieceLength == 663769, "original final piece remainder preserved");
    require(parsed->guessFileIndex() == std::optional<int>(1),
            "no-series guess chooses largest media file");
    require(parsed->guessFileIndex(SeriesHint{1, 2}) == std::optional<int>(0),
            "series guess prefers matching episode before size");
    require(parsed->guessFileIndex(SeriesHint{9, 9}) == std::optional<int>(1),
            "series miss falls back to largest media file");

    const QString magnet = QStringLiteral(
        "magnet:?xt=urn:btih:359F54DCFA44CC59E025CE4FF0192E66D9F2D424&tr=udp%3A%2F%2Ftracker.example%3A80%2Fannounce");
    auto identity = TorrentMetadataCodec::parseMagnet(magnet, &error);
    require(identity.has_value(), "40-hex magnet parses");
    require(identity->infoHash == parsed->infoHash, "magnet infoHash normalized lowercase");
    require(identity->trackers == QStringList{QStringLiteral("udp://tracker.example:80/announce")},
            "magnet tracker percent-decoded");

    auto state = TorrentMetadataState::fromMagnet(magnet, &error);
    require(state.has_value() && !state->metadataReady(), "magnet starts before metadata ready");
    bool waiterReleased = false;
    state->whenReady([&waiterReleased](const TorrentMetadata& ready) {
        waiterReleased = ready.files.size() == 3;
    });
    require(!waiterReleased, "GET-before-metadata waiter remains pending without premature failure");
    require(state->applyInfoSection(parsed->infoSection, &error), "matching peer metadata accepted");
    require(state->metadataReady() && state->metadata()->files.size() == 3,
            "metadata-ready transition exposes immutable file model");
    require(waiterReleased, "metadata-ready transition releases pending GET-style waiter");
    QByteArray tamperedInfo = parsed->infoSection;
    tamperedInfo[tamperedInfo.size() - 2] ^= 0x01;
    auto badState = TorrentMetadataState::fromMagnet(magnet, &error);
    require(badState.has_value() && !badState->applyInfoSection(tamperedInfo, &error),
            "metadata with wrong SHA-1 is rejected");
    require(!badState->metadataReady(), "hash mismatch cannot cross ready gate");

    const QByteArray envelope = state->persistedTorrentBytes();
    auto restored = TorrentMetadataCodec::parseTorrent(envelope, &error);
    require(restored.has_value() && restored->infoHash == parsed->infoHash,
            "Stremio-style info-only cache envelope round-trips");

    QTemporaryDir temp;
    require(temp.isValid(), "temporary metadata cache directory");
    const QString cachePath = temp.filePath(QStringLiteral("cache"));
    require(TorrentMetadataCodec::persistInfoSection(cachePath, parsed->infoSection, &error),
            "metadata info section persists");
    auto loaded = TorrentMetadataCodec::loadPersisted(cachePath, parsed->infoHash, &error);
    require(loaded.has_value() && loaded->files[1].offset == 700000,
            "persisted metadata reload preserves file geometry");

    VirtualPieceMap map(*parsed, true);
    require(map.valid() && map.isVirtualized(), "1 MiB pieces virtualize for streaming");
    require(map.originalPieceLength() == 1048576 && map.streamPieceLength() == 524288,
            "virtual streaming piece is exactly 512 KiB");
    require(map.originalPieceCount() == 2 && map.streamPieceCount() == 4,
            "virtual and original piece counts differ as Stremio expects");
    require(map.originalPieceForStreamPiece(0) == 0
                && map.originalPieceForStreamPiece(1) == 0
                && map.originalPieceForStreamPiece(2) == 1
                && map.originalPieceForStreamPiece(3) == 1,
            "stream pieces map back to original verification pieces");
    require(map.streamRangeForOriginalPiece(0) == std::pair<int, int>(0, 2),
            "first original piece owns two stream pieces");
    require(map.streamRangeForOriginalPiece(1) == std::pair<int, int>(2, 4),
            "final original piece range caps at real stream piece count");
    require(map.streamPieceSize(3) == 139481, "final virtual piece remainder exact");
    require(map.originalPieceSize(1) == 663769, "final original verification remainder exact");

    VirtualPieceMap unchanged(900000, 786432, true);
    require(unchanged.valid() && !unchanged.isVirtualized() && unchanged.streamPieceLength() == 786432,
            "large non-divisible piece length is not rewritten");
    VirtualPieceMap exactThreshold(900000, 524288, true);
    require(!exactThreshold.isVirtualized(), "512 KiB piece length remains unchanged");

    VerificationBitfield verified(map.originalPieceCount());
    verified.set(0);
    QByteArray projected = verified.streamCompletionBytes(map);
    require(bit(projected, 0) && bit(projected, 1) && !bit(projected, 2),
            "one verified original piece marks its virtual children only");
    verified.set(1);
    projected = verified.streamCompletionBytes(map);
    require(bit(projected, 2) && bit(projected, 3),
            "verified final original piece includes final remainder stream piece");

    VerificationBitfield invalidation = verified;
    invalidation.invalidateFile(parsed->files[0], map.originalPieceLength());
    require(!invalidation.get(0) && invalidation.get(1),
            "missing first file invalidates overlapping original verification piece");
    invalidation.invalidateFile(parsed->files[1], map.originalPieceLength());
    require(!invalidation.get(0) && !invalidation.get(1),
            "cross-piece file invalidates every original piece it overlaps");

    VerificationBitfield raw(10);
    raw.set(0);
    raw.set(9);
    require(raw.bytes().size() == 2
                && static_cast<unsigned char>(raw.bytes()[0]) == 0x80
                && static_cast<unsigned char>(raw.bytes()[1]) == 0x40,
            "persistent bitfield uses Stremio MSB-first byte semantics");
    const QString bitfieldPath = temp.filePath(QStringLiteral("bitfield"));
    require(raw.persist(bitfieldPath, &error), "verification bitfield persists");
    auto rawReloaded = VerificationBitfield::load(bitfieldPath, 10, &error);
    require(rawReloaded.get(0) && rawReloaded.get(9) && !rawReloaded.get(1),
            "verification bitfield reload preserves bits");
    QByteArray exchangeBytes(40000, '\x5a');
    const QString exchangeHash = QString::fromLatin1(
        QCryptographicHash::hash(exchangeBytes, QCryptographicHash::Sha1).toHex());
    MetadataExchangeAssembler assembler(exchangeHash, exchangeBytes.size());
    require(assembler.valid() && assembler.pieceCount() == 3,
            "ut_metadata assembler uses 16 KiB pieces");
    require(assembler.acceptPiece(1, exchangeBytes.mid(16384, 16384))
                == MetadataExchangeAssembler::Result::Incomplete,
            "out-of-order metadata piece accepted but remains incomplete");
    require(assembler.acceptPiece(0, exchangeBytes.left(16384))
                == MetadataExchangeAssembler::Result::Incomplete,
            "second metadata piece still incomplete");
    require(assembler.acceptPiece(2, exchangeBytes.mid(32768))
                == MetadataExchangeAssembler::Result::Complete,
            "complete metadata validates SHA-1 before readiness");
    require(assembler.metadata() == exchangeBytes, "metadata pieces reassemble byte-exactly");

    MetadataExchangeAssembler mismatch(QString(40, QLatin1Char('0')), exchangeBytes.size());
    mismatch.acceptPiece(0, exchangeBytes.left(16384));
    mismatch.acceptPiece(1, exchangeBytes.mid(16384, 16384));
    require(mismatch.acceptPiece(2, exchangeBytes.mid(32768))
                == MetadataExchangeAssembler::Result::HashMismatch,
            "wrong metadata SHA-1 clears assembly instead of becoming ready");
    require(mismatch.metadata().isEmpty(), "hash mismatch exposes no metadata");
    MetadataExchangeAssembler oversized(exchangeHash, MetadataExchangeAssembler::kMaxMetadataSize + 1);
    require(!oversized.valid(), "metadata larger than Stremio 4 MiB ceiling is rejected");

    std::cout << "ARC44_W04_TORRENT_MODEL_OK\n";
    return 0;
}
