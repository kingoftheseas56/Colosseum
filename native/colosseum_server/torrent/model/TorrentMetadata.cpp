#include "colosseum_server/torrent/model/TorrentMetadata.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <libtorrent/error_code.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_info.hpp>

#include <algorithm>

namespace ColosseumServer::Torrent {
namespace {

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

QString sha1Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}
QString hashHex(const lt::sha1_hash& hash)
{
    const std::string raw = hash.to_string();
    return QString::fromLatin1(QByteArray(raw.data(), static_cast<int>(raw.size())).toHex());
}

void appendUnique(QStringList& values, const QString& value)
{
    if (!value.isEmpty() && !values.contains(value))
        values.append(value);
}

bool isMediaFile(const QString& path)
{
    static const QRegularExpression re(
        QStringLiteral(R"((\.mkv|\.avi|\.mp4|\.wmv|\.vp8|\.mov|\.mpg|\.ts|\.m3u8|\.webm|\.flac|\.mp3|\.wav|\.wma|\.aac|\.ogg)$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(path).hasMatch();
}

struct EpisodeStamp {
    int season = 0;
    QVector<int> episodes;
};

EpisodeStamp episodeStamp(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    EpisodeStamp stamp;
    static const QRegularExpression sxe(
        QStringLiteral(R"(S(\d{1,2})E(\d{2})(?:E(\d{2}))?)"),
        QRegularExpression::CaseInsensitiveOption);
    auto match = sxe.match(path);
    if (match.hasMatch()) {
        stamp.season = match.captured(1).toInt();
        stamp.episodes.append(match.captured(2).toInt());
        if (!match.captured(3).isEmpty())
            stamp.episodes.append(match.captured(3).toInt());
        return stamp;
    }

    static const QRegularExpression xstamp(
        QStringLiteral(R"((\d{1,2})x(\d{1,2}))"),
        QRegularExpression::CaseInsensitiveOption);
    match = xstamp.match(path);
    if (match.hasMatch()) {
        stamp.season = match.captured(1).toInt();
        stamp.episodes.append(match.captured(2).toInt());
        return stamp;
    }

    static const QRegularExpression seasonEpisode(
        QStringLiteral(R"(season[ .]?(\d{1,2}).*?ep(?:isode)?[ .]?(\d+))"),
        QRegularExpression::CaseInsensitiveOption);
    match = seasonEpisode.match(path);
    if (match.hasMatch()) {
        stamp.season = match.captured(1).toInt();
        stamp.episodes.append(match.captured(2).toInt());
    }
    return stamp;
}

QByteArray infoOnlyEnvelope(const QByteArray& infoSection)
{
    if (infoSection.isEmpty())
        return {};
    QByteArray bytes("d13:announce-listle4:info");
    bytes += infoSection;
    bytes += 'e';
    return bytes;
}

} // namespace

std::optional<int> TorrentMetadata::guessFileIndex(std::optional<SeriesHint> hint) const
{
    QVector<int> media;
    media.reserve(files.size());
    for (int i = 0; i < files.size(); ++i) {
        if (isMediaFile(files.at(i).path))
            media.append(i);
    }
    if (media.isEmpty())
        return std::nullopt;

    QVector<int> candidates;
    if (hint && hint->season > 0 && hint->episode > 0) {
        for (const int i : media) {
            const EpisodeStamp stamp = episodeStamp(files.at(i).path);
            if (stamp.season == hint->season && stamp.episodes.contains(hint->episode))
                candidates.append(i);
        }
    }
    if (candidates.isEmpty())
        candidates = media;

    int selected = candidates.first();
    for (const int i : candidates) {
        if (files.at(i).length > files.at(selected).length)
            selected = i;
    }
    return files.at(selected).index;
}

std::optional<MagnetIdentity> TorrentMetadataCodec::parseMagnet(const QString& uri,
                                                                QString* error)
{
    lt::error_code ec;
    const lt::add_torrent_params params = lt::parse_magnet_uri(uri.toStdString(), ec);
    if (ec) {
        setError(error, QString::fromStdString(ec.message()));
        return std::nullopt;
    }
    if (!params.info_hashes.has_v1()) {
        setError(error, QStringLiteral("magnet has no v1 info hash"));
        return std::nullopt;
    }

    MagnetIdentity result;
    result.infoHash = hashHex(params.info_hashes.v1).toLower();
    for (const auto& tracker : params.trackers)
        appendUnique(result.trackers, QString::fromStdString(tracker));
    return result;
}

std::optional<TorrentMetadata> TorrentMetadataCodec::parseTorrent(
    const QByteArray& torrentBytes, QString* error)
{
    if (torrentBytes.isEmpty()) {
        setError(error, QStringLiteral("empty torrent metadata"));
        return std::nullopt;
    }

    lt::error_code ec;
    const lt::torrent_info ti(torrentBytes.constData(), torrentBytes.size(), ec);
    if (ec || !ti.is_valid()) {
        setError(error, ec ? QString::fromStdString(ec.message())
                           : QStringLiteral("invalid torrent metadata"));
        return std::nullopt;
    }
    if (!ti.info("pieces")) {
        setError(error, QStringLiteral("torrent metadata has no v1 pieces field"));
        return std::nullopt;
    }

    const auto info = ti.info_section();
    TorrentMetadata result;
    result.infoSection = QByteArray(info.data(), static_cast<int>(info.size()));
    result.infoHash = sha1Hex(result.infoSection);
    result.name = QString::fromStdString(ti.name());
    result.isPrivate = ti.priv();
    result.length = ti.total_size();
    result.pieceLength = ti.piece_length();
    if (result.length <= 0 || result.pieceLength <= 0) {
        setError(error, QStringLiteral("torrent has invalid length or piece length"));
        return std::nullopt;
    }
    result.lastPieceLength = result.length % result.pieceLength;
    if (result.lastPieceLength == 0)
        result.lastPieceLength = result.pieceLength;

    result.pieceHashes.reserve(ti.num_pieces());
    for (int i = 0; i < ti.num_pieces(); ++i) {
        const std::string raw = ti.hash_for_piece(lt::piece_index_t{i}).to_string();
        result.pieceHashes.append(QByteArray(raw.data(), static_cast<int>(raw.size())));
    }

    const lt::file_storage& storage = ti.files();
    result.files.reserve(storage.num_files());
    for (int i = 0; i < storage.num_files(); ++i) {
        const lt::file_index_t index{i};
        TorrentFile file;
        file.index = i;
        file.path = QString::fromStdString(storage.file_path(index));
        QString normalized = file.path;
        normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
        file.name = normalized.section(QLatin1Char('/'), -1);
        file.length = storage.file_size(index);
        file.offset = storage.file_offset(index);
        result.files.append(std::move(file));
    }

    for (const auto& tracker : ti.trackers())
        appendUnique(result.announce, QString::fromStdString(tracker.url));
    for (const auto& seed : ti.web_seeds()) {
        if (seed.type == lt::web_seed_entry::url_seed)
            appendUnique(result.urlList, QString::fromStdString(seed.url));
    }
    return result;
}

std::optional<TorrentMetadata> TorrentMetadataCodec::parseInfoSection(
    const QByteArray& infoSection, const QString& expectedInfoHash, QString* error)
{
    const QString expected = expectedInfoHash.trimmed().toLower();
    if (infoSection.isEmpty() || expected.size() != 40) {
        setError(error, QStringLiteral("invalid metadata info section or expected info hash"));
        return std::nullopt;
    }
    if (sha1Hex(infoSection) != expected) {
        setError(error, QStringLiteral("metadata SHA-1 does not match expected info hash"));
        return std::nullopt;
    }
    auto parsed = parseTorrent(infoOnlyEnvelope(infoSection), error);
    if (!parsed || parsed->infoHash != expected) {
        if (!parsed)
            return std::nullopt;
        setError(error, QStringLiteral("parsed metadata identity mismatch"));
        return std::nullopt;
    }
    return parsed;
}

QByteArray TorrentMetadataCodec::persistedEnvelope(const QByteArray& infoSection)
{
    return infoOnlyEnvelope(infoSection);
}

bool TorrentMetadataCodec::persistInfoSection(const QString& filePath,
                                              const QByteArray& infoSection,
                                              QString* error)
{
    const QByteArray bytes = infoOnlyEnvelope(infoSection);
    if (bytes.isEmpty()) {
        setError(error, QStringLiteral("cannot persist empty info section"));
        return false;
    }
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(error, QStringLiteral("failed to persist torrent metadata"));
        return false;
    }
    return true;
}
std::optional<TorrentMetadata> TorrentMetadataCodec::loadPersisted(
    const QString& filePath, const QString& expectedInfoHash, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("persisted torrent metadata is unavailable"));
        return std::nullopt;
    }
    auto parsed = parseTorrent(file.readAll(), error);
    if (!parsed)
        return std::nullopt;
    if (parsed->infoHash != expectedInfoHash.trimmed().toLower()) {
        setError(error, QStringLiteral("persisted torrent metadata identity mismatch"));
        return std::nullopt;
    }
    return parsed;
}

std::optional<TorrentMetadataState> TorrentMetadataState::fromMagnet(const QString& uri,
                                                                      QString* error)
{
    auto identity = TorrentMetadataCodec::parseMagnet(uri, error);
    if (!identity)
        return std::nullopt;
    TorrentMetadataState state;
    state.m_infoHash = identity->infoHash;
    state.m_trackers = identity->trackers;
    return state;
}
std::optional<TorrentMetadataState> TorrentMetadataState::fromTorrent(
    const QByteArray& torrentBytes, QString* error)
{
    auto parsed = TorrentMetadataCodec::parseTorrent(torrentBytes, error);
    if (!parsed)
        return std::nullopt;
    TorrentMetadataState state;
    state.m_infoHash = parsed->infoHash;
    state.m_trackers = parsed->announce;
    state.m_metadata = std::move(parsed);
    return state;
}

const TorrentMetadata* TorrentMetadataState::metadata() const
{
    return m_metadata ? &*m_metadata : nullptr;
}

void TorrentMetadataState::whenReady(std::function<void(const TorrentMetadata&)> callback)
{
    if (!callback)
        return;
    if (m_metadata) {
        callback(*m_metadata);
        return;
    }
    m_readyCallbacks.append(std::move(callback));
}

bool TorrentMetadataState::applyInfoSection(const QByteArray& infoSection, QString* error)
{
    if (m_infoHash.isEmpty()) {
        setError(error, QStringLiteral("metadata arrived without a pending torrent identity"));
        return false;
    }
    if (infoSection.size() > 4 * 1024 * 1024) {
        setError(error, QStringLiteral("metadata exceeds 4 MiB"));
        return false;
    }
    auto parsed = TorrentMetadataCodec::parseInfoSection(infoSection, m_infoHash, error);
    if (!parsed)
        return false;
    m_metadata = std::move(parsed);
    releaseReadyCallbacks();
    return true;
}

bool TorrentMetadataState::restorePersisted(const QByteArray& torrentBytes, QString* error)
{
    auto parsed = TorrentMetadataCodec::parseTorrent(torrentBytes, error);
    if (!parsed)
        return false;
    if (!m_infoHash.isEmpty() && parsed->infoHash != m_infoHash) {
        setError(error, QStringLiteral("persisted metadata identity mismatch"));
        return false;
    }
    if (m_infoHash.isEmpty())
        m_infoHash = parsed->infoHash;
    m_metadata = std::move(parsed);
    releaseReadyCallbacks();
    return true;
}
QByteArray TorrentMetadataState::persistedTorrentBytes() const
{
    return m_metadata ? TorrentMetadataCodec::persistedEnvelope(m_metadata->infoSection)
                      : QByteArray{};
}

void TorrentMetadataState::releaseReadyCallbacks()
{
    if (!m_metadata || m_readyCallbacks.isEmpty())
        return;
    QList<std::function<void(const TorrentMetadata&)>> callbacks;
    callbacks.swap(m_readyCallbacks);
    for (auto& callback : callbacks)
        callback(*m_metadata);
}

} // namespace ColosseumServer::Torrent
