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
}

MusicTorrentMatch MusicTorrentMatcher::pick(const MusicTorrentTrack& track,
                                           const QList<MusicTorrentFile>& files)
{
    if (norm(track.artist).isEmpty() || norm(track.title).isEmpty())
        return {-1, {}, QStringLiteral("missing-identity")};
    // Whole-album images require CUE offsets and are intentionally unsupported.
    for (const auto& file : files)
        if (QFileInfo(file.path).suffix().compare(QStringLiteral("cue"), Qt::CaseInsensitive) == 0)
            return {-1, {}, QStringLiteral("unsupported-cue")};

    static const QSet<QString> extensions = {QStringLiteral("flac"), QStringLiteral("mp3"),
        QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("ogg"),
        QStringLiteral("opus"), QStringLiteral("wav"), QStringLiteral("aiff"), QStringLiteral("alac")};
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
        if (!file.title.isEmpty() && !equal(file.title, track.title)) continue;
        if (!equal(file.title.isEmpty() ? title : file.title, track.title)) continue;
        // Immediate parent directories provide identity only in the documented
        // Artist/Album/Track layout. No substring/token scoring or title fallback.
        const QString artist = file.artist.isEmpty() && parts.size() >= 3 ? parts.at(parts.size()-3) : file.artist;
        const QString album = file.album.isEmpty() && parts.size() >= 2 ? parts.at(parts.size()-2) : file.album;
        if (!equal(artist, track.artist)) continue;
        if (!track.album.isEmpty() && !equal(album, track.album)) continue;
        if (track.trackNumber > 0 && number > 0 && track.trackNumber != number) continue;
        if (track.discNumber > 0 && file.discNumber > 0 && track.discNumber != file.discNumber) continue;
        if (track.durationMs > 0 && file.durationMs > 0 && qAbs(track.durationMs-file.durationMs) > 2000) continue;
        if (result.index >= 0) return {-1, {}, QStringLiteral("ambiguous-match")};
        result = {file.index, file.path, {}};
    }
    return result;
}
