#include "MangaVolumeRequestLedger.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace MangaTankoban {

namespace {
VolumeRequestRow fromJson(const QJsonObject& o)
{
    VolumeRequestRow r;
    r.volumeId        = o.value(QStringLiteral("volumeId")).toString();
    r.infoHash        = o.value(QStringLiteral("infoHash")).toString();
    r.magnetUri       = o.value(QStringLiteral("magnetUri")).toString();
    r.seriesId        = o.value(QStringLiteral("seriesId")).toString();
    r.volumeNumber    = o.value(QStringLiteral("volumeNumber")).toString();
    r.savePath        = o.value(QStringLiteral("savePath")).toString();
    r.pickedFileIndex = o.value(QStringLiteral("pickedFileIndex")).toInt(-1);
    r.expectedFileIndex = o.value(QStringLiteral("expectedFileIndex")).toInt(-1);
    r.expectedFilePath  = o.value(QStringLiteral("expectedFilePath")).toString();
    r.state           = o.value(QStringLiteral("state")).toString();
    return r;
}

QJsonObject toJson(const VolumeRequestRow& r)
{
    QJsonObject o;
    o[QStringLiteral("volumeId")]        = r.volumeId;
    o[QStringLiteral("infoHash")]        = r.infoHash;
    o[QStringLiteral("magnetUri")]       = r.magnetUri;
    o[QStringLiteral("seriesId")]        = r.seriesId;
    o[QStringLiteral("volumeNumber")]    = r.volumeNumber;
    o[QStringLiteral("savePath")]        = r.savePath;
    o[QStringLiteral("pickedFileIndex")] = r.pickedFileIndex;
    o[QStringLiteral("expectedFileIndex")] = r.expectedFileIndex;
    o[QStringLiteral("expectedFilePath")]  = r.expectedFilePath;
    o[QStringLiteral("state")]           = r.state;
    return o;
}
} // namespace

MangaVolumeRequestLedger::MangaVolumeRequestLedger(const QString& path)
    : m_path(path)
{
    reload();
}

bool MangaVolumeRequestLedger::isTerminal(const QString& state)
{
    return state == QStringLiteral("completed")
        || state == QStringLiteral("failed")
        || state == QStringLiteral("cancelled");
}

int MangaVolumeRequestLedger::indexOf(const QString& volumeId) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].volumeId == volumeId)
            return i;
    return -1;
}

void MangaVolumeRequestLedger::upsert(const VolumeRequestRow& row)
{
    const int at = indexOf(row.volumeId);
    if (at >= 0)
        m_rows[at] = row;
    else
        m_rows.append(row);
    persist();
}

void MangaVolumeRequestLedger::setState(const QString& volumeId, const QString& state)
{
    const int at = indexOf(volumeId);
    if (at < 0) return;
    m_rows[at].state = state;
    persist();
}

void MangaVolumeRequestLedger::markDownloading(const QString& volumeId, int pickedFileIndex)
{
    const int at = indexOf(volumeId);
    if (at < 0) return;
    m_rows[at].pickedFileIndex = pickedFileIndex;
    m_rows[at].state = QStringLiteral("downloading");
    persist();
}

QList<VolumeRequestRow> MangaVolumeRequestLedger::active() const
{
    QList<VolumeRequestRow> live;
    for (const VolumeRequestRow& r : m_rows)
        if (!isTerminal(r.state))
            live.append(r);
    return live;
}

bool MangaVolumeRequestLedger::contains(const QString& volumeId) const
{
    return indexOf(volumeId) >= 0;
}

VolumeRequestRow MangaVolumeRequestLedger::row(const QString& volumeId) const
{
    const int at = indexOf(volumeId);
    return at >= 0 ? m_rows[at] : VolumeRequestRow{};
}

void MangaVolumeRequestLedger::reload()
{
    m_rows.clear();
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return;  // absent journal is the normal first-run case, not an error
    const QByteArray data = f.readAll();
    f.close();
    if (data.isEmpty())
        return;
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "[MangaVolumeRequestLedger] ignoring corrupt journal at" << m_path
                   << ":" << parseError.errorString();
        return;  // a corrupt journal must not silently pass as "no intents"
    }
    const QJsonArray arr = doc.array();
    for (const QJsonValue& v : arr)
        m_rows.append(fromJson(v.toObject()));
}

void MangaVolumeRequestLedger::persist() const
{
    const QFileInfo fi(m_path);
    if (!fi.absolutePath().isEmpty())
        QDir().mkpath(fi.absolutePath());

    QJsonArray arr;
    for (const VolumeRequestRow& r : m_rows)
        arr.append(toJson(r));

    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "[MangaVolumeRequestLedger] cannot open journal for write:" << m_path
                   << ":" << f.errorString();
        return;  // a dropped write defeats restart safety — never swallow it silently
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    if (!f.commit())
        qWarning() << "[MangaVolumeRequestLedger] failed to commit journal:" << m_path
                   << ":" << f.errorString();
}

} // namespace MangaTankoban
