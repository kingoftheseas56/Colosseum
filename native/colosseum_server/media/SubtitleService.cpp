#include "colosseum_server/media/MediaPipeline.h"

#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>

namespace ColosseumServer::Media {
namespace {

void setError(QString *error, const QString &value) { if (error) *error = value; }

qint64 parseTime(const QString &text)
{
    const auto m = QRegularExpression(QStringLiteral("^(\\d+):(\\d+):(\\d+)[,.](\\d+)$")).match(text);
    if (!m.hasMatch()) return -1;
    return m.captured(1).toLongLong() * 3600000
        + m.captured(2).toLongLong() * 60000
        + m.captured(3).toLongLong() * 1000
        + m.captured(4).leftJustified(3, QLatin1Char('0')).left(3).toLongLong();
}

QByteArray timeText(qint64 ms, SubtitleFormat format)
{
    if (ms < 0) ms = 0;
    const qint64 hours = ms / 3600000; ms %= 3600000;
    const qint64 minutes = ms / 60000; ms %= 60000;
    const qint64 seconds = ms / 1000; const qint64 millis = ms % 1000;
    return QByteArray::number(hours).rightJustified(2, '0') + ':'
        + QByteArray::number(minutes).rightJustified(2, '0') + ':'
        + QByteArray::number(seconds).rightJustified(2, '0')
        + (format == SubtitleFormat::Vtt ? '.' : ',')
        + QByteArray::number(millis).rightJustified(3, '0');
}

quint64 readLe64(const QByteArray &bytes, qsizetype off)
{
    quint64 value = 0;
    for (int i = 0; i < 8; ++i) value |= quint64(uchar(bytes.at(off + i))) << (8 * i);
    return value;
}
} // namespace

QVector<SubtitleCue> SubtitleService::parseSrt(const QByteArray &bytes, QString *error)
{
    QVector<SubtitleCue> result;
    QString text = QString::fromUtf8(bytes);
    text.replace(QLatin1Char('\r'), QString());
    const QStringList blocks = text.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        QStringList lines = block.split(QLatin1Char('\n'));
        while (lines.size() > 2 && !lines.first().contains(QStringLiteral(" --> ")))
            lines.removeFirst();
        if (lines.size() < 2) continue;
        const QStringList timing = lines.takeFirst().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (timing.size() < 3) continue;
        const qint64 start = parseTime(timing.at(0));
        const qint64 end = parseTime(timing.at(2));
        const QString cueText = lines.join(QLatin1Char('\n'));
        if (start < 0 || end < 0 || cueText.isEmpty()) continue;
        result.append({static_cast<int>(result.size()), start, end, cueText});
    }
    setError(error, {});
    return result;
}

QByteArray SubtitleService::render(const QVector<SubtitleCue> &cues,
                                   SubtitleFormat format, qint64 offsetMs)
{
    QByteArray out;
    if (format == SubtitleFormat::Vtt) out += "WEBVTT\n\n";
    for (int i = 0; i < cues.size(); ++i) {
        const SubtitleCue &cue = cues.at(i);
        out += QByteArray::number(i) + '\n';
        out += timeText(cue.startMs + offsetMs, format) + " --> "
            + timeText(cue.endMs + offsetMs, format) + '\n';
        QString escaped = cue.text;
        escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
        out += escaped.toUtf8() + "\n\n";
    }
    return out;
}
QString SubtitleService::openSubHashFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return {};
    }
    const qint64 size = file.size();
    if (size < 65536) {
        setError(error, QStringLiteral("media is smaller than the OpenSubtitles hash window"));
        return {};
    }
    const QByteArray first = file.read(65536);
    if (!file.seek(size - 65536)) {
        setError(error, QStringLiteral("cannot seek to final hash window"));
        return {};
    }
    const QByteArray last = file.read(65536);
    if (first.size() != 65536 || last.size() != 65536) {
        setError(error, QStringLiteral("cannot read OpenSubtitles hash windows"));
        return {};
    }
    quint64 sum = quint64(size);
    for (qsizetype off = 0; off < first.size(); off += 8) sum += readLe64(first, off);
    for (qsizetype off = 0; off < last.size(); off += 8) sum += readLe64(last, off);
    setError(error, {});
    return QStringLiteral("%1").arg(sum, 16, 16, QLatin1Char('0'));
}

bool SubtitleService::retrieve(const QUrl &url, QByteArray *bytes,
                               QString *error, int timeoutMs)
{
    if (!bytes || !url.isValid()) { setError(error, QStringLiteral("invalid subtitle url")); return false; }
    if (url.isLocalFile()) {
        QFile file(url.toLocalFile());
        if (!file.open(QIODevice::ReadOnly)) { setError(error, file.errorString()); return false; }
        *bytes = file.readAll(); setError(error, {}); return true;
    }
    if (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) {
        setError(error, QStringLiteral("subtitle url must be http, https, or file")); return false;
    }
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.get(QNetworkRequest(url));
    QEventLoop loop; QTimer timer; timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs); loop.exec();
    if (!timer.isActive()) { reply->abort(); reply->deleteLater(); setError(error, QStringLiteral("subtitle request timed out")); return false; }
    timer.stop();
    if (reply->error() != QNetworkReply::NoError) {
        const QString message = reply->errorString(); reply->deleteLater(); setError(error, message); return false;
    }
    *bytes = reply->readAll(); reply->deleteLater(); setError(error, {}); return true;
}

bool SubtitleService::subtitlesTracks(const QUrl &url, QJsonObject *result,
                                      QString *error, int timeoutMs)
{
    QByteArray bytes;
    if (!retrieve(url, &bytes, error, timeoutMs)) return false;
    QString parseError;
    const QVector<SubtitleCue> cues = parseSrt(bytes, &parseError);
    if (!parseError.isEmpty()) { setError(error, parseError); return false; }
    QJsonArray tracks;
    for (const SubtitleCue &cue : cues) {
        QJsonObject item;
        item.insert(QStringLiteral("number"), cue.number);
        item.insert(QStringLiteral("startTime"), double(cue.startMs));
        item.insert(QStringLiteral("endTime"), double(cue.endMs));
        item.insert(QStringLiteral("text"), cue.text);
        tracks.append(item);
    }
    result->insert(QStringLiteral("url"), url.toString());
    result->insert(QStringLiteral("tracks"), tracks);
    setError(error, {}); return true;
}
bool SubtitleService::openSubHash(const QUrl &url, QString *hash,
                                  QString *error, int timeoutMs)
{
    if (!hash || !url.isValid()) { setError(error, QStringLiteral("url required")); return false; }
    if (url.isLocalFile()) {
        *hash = openSubHashFile(url.toLocalFile(), error);
        return !hash->isEmpty();
    }
    if (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) {
        setError(error, QStringLiteral("url must begin with http or file")); return false;
    }
    QNetworkAccessManager manager;
    auto waitReply = [&](QNetworkReply *reply, QByteArray *body) -> bool {
        QEventLoop loop; QTimer timer; timer.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeoutMs); loop.exec();
        if (!timer.isActive()) { reply->abort(); setError(error, QStringLiteral("hash request timed out")); reply->deleteLater(); return false; }
        timer.stop();
        if (reply->error() != QNetworkReply::NoError) { setError(error, reply->errorString()); reply->deleteLater(); return false; }
        if (body) *body = reply->readAll();
        reply->deleteLater(); return true;
    };
    QNetworkRequest headReq(url);
    headReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *head = manager.head(headReq);
    QEventLoop headLoop; QTimer headTimer; headTimer.setSingleShot(true);
    QObject::connect(head, &QNetworkReply::finished, &headLoop, &QEventLoop::quit);
    QObject::connect(&headTimer, &QTimer::timeout, &headLoop, &QEventLoop::quit);
    headTimer.start(timeoutMs); headLoop.exec();
    if (!headTimer.isActive() || head->error() != QNetworkReply::NoError) {
        const QString message = !headTimer.isActive() ? QStringLiteral("hash HEAD timed out") : head->errorString();
        head->abort(); head->deleteLater(); setError(error, message); return false;
    }
    headTimer.stop();
    bool sizeOk = false;
    const qint64 size = head->header(QNetworkRequest::ContentLengthHeader).toLongLong(&sizeOk);
    const QUrl mediaUrl = head->url();
    head->deleteLater();
    if (!sizeOk || size < 65536) { setError(error, QStringLiteral("failed to get valid content length")); return false; }
    auto readRange = [&](qint64 start, qint64 end, QByteArray *body) -> bool {
        QNetworkRequest request(mediaUrl);
        request.setRawHeader("Range", "bytes=" + QByteArray::number(start) + '-' + QByteArray::number(end));
        request.setRawHeader("enginefs-prio", "10");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        return waitReply(manager.get(request), body);
    };
    QByteArray first, last;
    if (!readRange(0, 65535, &first) || !readRange(size - 65536, size - 1, &last)) return false;
    if (first.size() != 65536 || last.size() != 65536) {
        setError(error, QStringLiteral("response for calculating movie hash has wrong length")); return false;
    }
    quint64 sum = quint64(size);
    for (qsizetype off = 0; off < first.size(); off += 8) sum += readLe64(first, off);
    for (qsizetype off = 0; off < last.size(); off += 8) sum += readLe64(last, off);
    *hash = QStringLiteral("%1").arg(sum, 16, 16, QLatin1Char('0'));
    setError(error, {}); return true;
}

} // namespace ColosseumServer::Media
