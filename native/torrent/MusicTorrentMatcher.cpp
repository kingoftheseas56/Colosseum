#include "MusicTorrentMatcher.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace {
QString norm(QString value)
{
    value = value.normalized(QString::NormalizationForm_C).toCaseFolded();
    value.replace(QRegularExpression(QStringLiteral("[._]+")), QStringLiteral(" "));
    return value.simplified();
}
bool equal(const QString& a, const QString& b) { return norm(a) == norm(b); }

bool releaseAlbum(QString folder, const MusicTorrentTrack& track)
{
    if (equal(folder, track.album)) return true;
    // Strip only a known technical suffix, never arbitrary bracketed words.
    // Numeric uploader suffixes (e.g. "[MP3 320] 88" or "FLAC 88") are accepted only after
    // this audio-format marker. Edition/live/remix labels stay in the identity.
    static const QRegularExpression format(QStringLiteral(
        R"(\s*[\[(](?:mp3(?:[ -]+(?:v0|v2|128|192|256|320)(?:\s*kbps)?)?|flac(?:\s+(?:16|24)[ -]?(?:bit)?)?|alac|aac|ogg|opus|wav)[\])]\s*(?:\d{1,3})?\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression plainFormat(QStringLiteral(
        R"(\s+(?:flac|alac|mp3(?:[ -]+(?:v0|v2|128|192|256|320)(?:\s*kbps)?)?)\s+\d{1,3}\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto audio = format.match(folder);
    const auto plainAudio = plainFormat.match(folder);
    if (audio.hasMatch()) folder.truncate(audio.capturedStart());
    else if (plainAudio.hasMatch()) folder.truncate(plainAudio.capturedStart());
    folder = folder.trimmed();
    if (equal(folder, track.album)) return true;
    static const QRegularExpression year(QStringLiteral(R"(\s*[\[(]((?:19|20)\d{2})[\])]\s*$)"));
    const auto date = year.match(folder);
    if (!date.hasMatch()) return false;
    if (track.releaseYear > 0 && date.captured(1).toInt() != track.releaseYear) return false;
    folder.truncate(date.capturedStart());
    return equal(folder, track.album);
}

bool pathIdentity(const QStringList& parts, int albumPos, const MusicTorrentTrack& track)
{
    if (albumPos < 0 || track.album.isEmpty()) return false;
    const QString folder = parts.at(albumPos);
    if (albumPos > 0 && equal(parts.at(albumPos - 1), track.artist) && releaseAlbum(folder, track))
        return true;
    // The delimiter is structural; neither artist nor album is substring-scored.
    const QString marker = QStringLiteral(" - ");
    for (qsizetype pos = folder.indexOf(marker); pos >= 0; pos = folder.indexOf(marker, pos + marker.size())) {
        if (equal(folder.left(pos), track.artist) && releaseAlbum(folder.mid(pos + marker.size()), track))
            return true;
    }
    return false;
}
}

MusicTorrentMatch MusicTorrentMatcher::pick(const MusicTorrentTrack& track,
                                           const QList<MusicTorrentFile>& files)
{
    if (norm(track.artist).isEmpty() || norm(track.title).isEmpty())
        return {-1, {}, QStringLiteral("missing-identity")};
    static const QSet<QString> extensions = {QStringLiteral("flac"), QStringLiteral("mp3"),
        QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("ogg"),
        QStringLiteral("opus"), QStringLiteral("wav"), QStringLiteral("aiff"), QStringLiteral("alac")};
    bool hasCue = false;
    int audioCount = 0;
    for (const auto& file : files) {
        const QString suffix = QFileInfo(file.path).suffix().toLower();
        hasCue |= suffix == QStringLiteral("cue");
        if (file.index >= 0 && extensions.contains(suffix)) ++audioCount;
    }
    // Single-file album images need CUE offsets. A sidecar beside separately
    // numbered songs is harmless, but never treat an image as the first song.
    if (hasCue && audioCount <= 1) return {-1, {}, QStringLiteral("unsupported-cue")};
    MusicTorrentMatch result{-1, {}, QStringLiteral("no-exact-match")};
    for (const auto& file : files) {
        if (file.index < 0 || !extensions.contains(QFileInfo(file.path).suffix().toLower())) continue;
        QString path = file.path;
        path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString title = QFileInfo(path).completeBaseName();
        int number = file.trackNumber;
        const auto prefix = QRegularExpression(QStringLiteral("^(\\d{1,3})[ ._-]+(.+)$")).match(title);
        if (prefix.hasMatch()) {
            if (number > 0 && number != prefix.captured(1).toInt()) continue;
            number = prefix.captured(1).toInt();
            title = prefix.captured(2);
        }
        if (hasCue && !prefix.hasMatch()) continue;
        if (!file.title.isEmpty() && !equal(file.title, track.title)) continue;
        if (prefix.hasMatch() && !equal(title, track.title)) continue;
        if (!equal(file.title.isEmpty() ? title : file.title, track.title)) continue;
        int albumPos = int(parts.size()) - 2;
        int disc = file.discNumber;
        if (albumPos >= 0) {
            static const QRegularExpression discFolder(QStringLiteral("^(?:cd|disc|disk)[ ._-]*(\\d{1,2})$"), QRegularExpression::CaseInsensitiveOption);
            const auto folder = discFolder.match(parts.at(albumPos));
            if (folder.hasMatch()) {
                const int pathDisc = folder.captured(1).toInt();
                if (pathDisc <= 0 || (disc > 0 && disc != pathDisc)) continue;
                disc = pathDisc;
                --albumPos;
            }
        }
        if (!file.artist.isEmpty() && !equal(file.artist, track.artist)) continue;
        if (!file.album.isEmpty() && !track.album.isEmpty() && !equal(file.album, track.album)) continue;
        const bool tagged = !file.artist.isEmpty() && (track.album.isEmpty() || !file.album.isEmpty());
        const bool pathMatches = pathIdentity(parts, albumPos, track);
        if (pathMatches && !equal(title, track.title)) continue;
        const bool identifiablePath = albumPos >= 0 && !track.album.isEmpty()
            && (parts.at(albumPos).contains(QStringLiteral(" - "))
                || (albumPos > 0 && (equal(parts.at(albumPos - 1), track.artist)
                                    || releaseAlbum(parts.at(albumPos), track))));
        if ((!tagged || identifiablePath) && !pathMatches) continue;
        if (track.trackNumber > 0 && number > 0 && track.trackNumber != number) continue;
        if (track.discNumber > 0 && disc > 0 && track.discNumber != disc) continue;
        if (track.durationMs > 0 && file.durationMs > 0 && qAbs(track.durationMs-file.durationMs) > 2000) continue;
        if (result.index >= 0) return {-1, {}, QStringLiteral("ambiguous-match")};
        result = {file.index, file.path, {}};
    }
    return result;
}
