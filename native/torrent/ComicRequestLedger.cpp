#include "ComicRequestLedger.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace {

// Stable, lowercase, single-word persistence strings for ComicCollectionFormat.
// Deliberately NOT ComicEditionIdentity::formatName()/parseFormat(): those are
// a fuzzy catalog-text alias table (e.g. TradePaperback's aliases are "tpb",
// "trade", "trade paperback" — formatName().toLower() == "tradepaperback"
// would NOT round-trip through parseFormat(), which only matches the spaced
// alias). The ledger needs a lossless, exact round-trip for every enum value,
// so it owns this dedicated mapping instead.
QString formatToString(ComicEditionIdentity::ComicCollectionFormat format)
{
    using ComicEditionIdentity::ComicCollectionFormat;
    switch (format) {
    case ComicCollectionFormat::Compendium:     return QStringLiteral("compendium");
    case ComicCollectionFormat::Omnibus:        return QStringLiteral("omnibus");
    case ComicCollectionFormat::TradePaperback: return QStringLiteral("tradepaperback");
    case ComicCollectionFormat::Deluxe:         return QStringLiteral("deluxe");
    case ComicCollectionFormat::Absolute:       return QStringLiteral("absolute");
    case ComicCollectionFormat::Hardcover:      return QStringLiteral("hardcover");
    case ComicCollectionFormat::Collection:     return QStringLiteral("collection");
    case ComicCollectionFormat::Volume:         return QStringLiteral("volume");
    case ComicCollectionFormat::Book:           return QStringLiteral("book");
    case ComicCollectionFormat::Unknown:        break;
    }
    return QStringLiteral("unknown");
}

ComicEditionIdentity::ComicCollectionFormat formatFromString(const QString& text)
{
    using ComicEditionIdentity::ComicCollectionFormat;
    if (text == QStringLiteral("compendium"))     return ComicCollectionFormat::Compendium;
    if (text == QStringLiteral("omnibus"))        return ComicCollectionFormat::Omnibus;
    if (text == QStringLiteral("tradepaperback")) return ComicCollectionFormat::TradePaperback;
    if (text == QStringLiteral("deluxe"))         return ComicCollectionFormat::Deluxe;
    if (text == QStringLiteral("absolute"))       return ComicCollectionFormat::Absolute;
    if (text == QStringLiteral("hardcover"))      return ComicCollectionFormat::Hardcover;
    if (text == QStringLiteral("collection"))     return ComicCollectionFormat::Collection;
    if (text == QStringLiteral("volume"))         return ComicCollectionFormat::Volume;
    if (text == QStringLiteral("book"))           return ComicCollectionFormat::Book;
    return ComicCollectionFormat::Unknown;
}

// Stable, lowercase persistence strings for ComicPayloadKind (own mapping —
// ComicEditionFileSelector has no name helper for this enum).
QString payloadKindToString(ComicEditionFileSelector::ComicPayloadKind kind)
{
    using ComicEditionFileSelector::ComicPayloadKind;
    switch (kind) {
    case ComicPayloadKind::SingleArchive:        return QStringLiteral("singlearchive");
    case ComicPayloadKind::IssueArchiveSet:      return QStringLiteral("issuearchiveset");
    case ComicPayloadKind::LooseImageSubtree:    return QStringLiteral("looseimagesubtree");
    case ComicPayloadKind::CombinedWholeArchive: return QStringLiteral("combinedwholearchive");
    case ComicPayloadKind::None:                 break;
    }
    return QStringLiteral("none");
}

ComicEditionFileSelector::ComicPayloadKind payloadKindFromString(const QString& text)
{
    using ComicEditionFileSelector::ComicPayloadKind;
    if (text == QStringLiteral("singlearchive"))        return ComicPayloadKind::SingleArchive;
    if (text == QStringLiteral("issuearchiveset"))      return ComicPayloadKind::IssueArchiveSet;
    if (text == QStringLiteral("looseimagesubtree"))    return ComicPayloadKind::LooseImageSubtree;
    if (text == QStringLiteral("combinedwholearchive")) return ComicPayloadKind::CombinedWholeArchive;
    return ComicPayloadKind::None;
}

bool isTerminalState(const QString& state)
{
    return state == QStringLiteral("completed")
        || state == QStringLiteral("failed")
        || state == QStringLiteral("cancelled");
}

bool isValidInfoHash40(const QString& hash)
{
    if (hash.size() != 40) return false;
    for (const QChar c : hash) {
        const bool digit = c >= QLatin1Char('0') && c <= QLatin1Char('9');
        const bool lower = c >= QLatin1Char('a') && c <= QLatin1Char('f');
        const bool upper = c >= QLatin1Char('A') && c <= QLatin1Char('F');
        if (!digit && !lower && !upper) return false;
    }
    return true;
}

QJsonObject rowToJson(const ComicEditionRequestRow& r)
{
    QJsonObject o;
    o[QStringLiteral("editionId")]    = r.editionId;
    o[QStringLiteral("infoHash")]     = r.infoHash;
    o[QStringLiteral("magnetUri")]    = r.magnetUri;
    o[QStringLiteral("seriesId")]     = r.seriesId;
    o[QStringLiteral("seriesTitle")]  = r.seriesTitle;
    o[QStringLiteral("editionTitle")] = r.editionTitle;
    o[QStringLiteral("format")]       = formatToString(r.format);
    o[QStringLiteral("ordinal")]      = r.ordinal;
    o[QStringLiteral("isbnDigits")]   = r.isbnDigits;

    QJsonArray issuesArr;
    for (const ComicEditionIdentity::ComicIssueRef& issue : r.collectedIssues) {
        QJsonObject io;
        io[QStringLiteral("series")] = issue.series;
        io[QStringLiteral("number")] = issue.number;
        issuesArr.append(io);
    }
    o[QStringLiteral("collectedIssues")] = issuesArr;

    o[QStringLiteral("savePath")] = r.savePath;

    QJsonArray pickedArr;
    for (int idx : r.pickedFileIndices)
        pickedArr.append(idx);
    o[QStringLiteral("pickedFileIndices")] = pickedArr;

    o[QStringLiteral("payloadKind")] = payloadKindToString(r.payloadKind);
    o[QStringLiteral("state")]       = r.state;
    return o;
}

// Returns false (row must be quarantined, `out` left untouched) when the row
// is structurally unusable — currently: missing/empty editionId, the one
// field every other lookup in this ledger is keyed on.
bool rowFromJson(const QJsonObject& o, ComicEditionRequestRow* out)
{
    const QString editionId = o.value(QStringLiteral("editionId")).toString();
    if (editionId.isEmpty())
        return false;

    ComicEditionRequestRow r;
    r.editionId    = editionId;
    r.infoHash     = o.value(QStringLiteral("infoHash")).toString();
    r.magnetUri    = o.value(QStringLiteral("magnetUri")).toString();
    r.seriesId     = o.value(QStringLiteral("seriesId")).toString();
    r.seriesTitle  = o.value(QStringLiteral("seriesTitle")).toString();
    r.editionTitle = o.value(QStringLiteral("editionTitle")).toString();
    r.format       = formatFromString(o.value(QStringLiteral("format")).toString());
    r.ordinal      = o.value(QStringLiteral("ordinal")).toInt(-1);
    r.isbnDigits   = o.value(QStringLiteral("isbnDigits")).toString();

    for (const QJsonValue& v : o.value(QStringLiteral("collectedIssues")).toArray()) {
        const QJsonObject io = v.toObject();
        ComicEditionIdentity::ComicIssueRef issue;
        issue.series = io.value(QStringLiteral("series")).toString();
        issue.number = io.value(QStringLiteral("number")).toInt(-1);
        r.collectedIssues.append(issue);
    }

    r.savePath = o.value(QStringLiteral("savePath")).toString();

    for (const QJsonValue& v : o.value(QStringLiteral("pickedFileIndices")).toArray())
        r.pickedFileIndices.append(v.toInt(-1));

    r.payloadKind = payloadKindFromString(o.value(QStringLiteral("payloadKind")).toString());
    r.state       = o.value(QStringLiteral("state")).toString();

    *out = r;
    return true;
}

} // namespace

ComicRequestLedger::ComicRequestLedger(const QString& path)
    : m_path(path)
{
}

int ComicRequestLedger::schemaVersion()
{
    return 1;
}

int ComicRequestLedger::indexOf(const QString& editionId) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].editionId == editionId)
            return i;
    return -1;
}

void ComicRequestLedger::load()
{
    m_rows.clear();

    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return;   // absent journal is the normal first-run case, not an error
    const QByteArray data = f.readAll();
    f.close();
    if (data.isEmpty())
        return;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[ComicRequestLedger] ignoring corrupt journal at" << m_path
                   << ":" << parseError.errorString();
        return;
    }

    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt(-1);
    if (version != schemaVersion()) {
        qWarning() << "[ComicRequestLedger] ignoring journal at" << m_path
                   << "with unknown schema version" << version
                   << "(expected" << schemaVersion() << ")";
        return;   // a version mismatch must not be partially applied
    }

    for (const QJsonValue& v : root.value(QStringLiteral("rows")).toArray()) {
        if (!v.isObject()) {
            qWarning() << "[ComicRequestLedger] quarantining a non-object row in" << m_path;
            continue;
        }
        ComicEditionRequestRow row;
        if (!rowFromJson(v.toObject(), &row)) {
            qWarning() << "[ComicRequestLedger] quarantining a malformed row in" << m_path;
            continue;
        }
        // A duplicate editionId within the file keeps the LAST occurrence's
        // data, at the FIRST occurrence's position (stable ordering).
        const int at = indexOf(row.editionId);
        if (at >= 0)
            m_rows[at] = row;
        else
            m_rows.append(row);
    }
}

void ComicRequestLedger::persist() const
{
    const QFileInfo fi(m_path);
    if (!fi.absolutePath().isEmpty())
        QDir().mkpath(fi.absolutePath());

    QJsonArray rowsArr;
    for (const ComicEditionRequestRow& r : m_rows)
        rowsArr.append(rowToJson(r));

    QJsonObject root;
    root[QStringLiteral("version")] = schemaVersion();
    root[QStringLiteral("rows")]    = rowsArr;

    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "[ComicRequestLedger] cannot open journal for write:" << m_path
                   << ":" << f.errorString();
        return;   // a dropped write defeats restart safety — never swallow it silently
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit())
        qWarning() << "[ComicRequestLedger] failed to commit journal:" << m_path
                   << ":" << f.errorString();
}

QList<ComicEditionRequestRow> ComicRequestLedger::all() const
{
    return m_rows;
}

QList<ComicEditionRequestRow> ComicRequestLedger::active() const
{
    QList<ComicEditionRequestRow> live;
    for (const ComicEditionRequestRow& r : m_rows) {
        if (isTerminalState(r.state)) continue;
        if (!isValidInfoHash40(r.infoHash)) continue;   // not safely resumable
        live.append(r);
    }
    return live;
}

void ComicRequestLedger::upsert(const ComicEditionRequestRow& row)
{
    const int at = indexOf(row.editionId);
    if (at >= 0)
        m_rows[at] = row;
    else
        m_rows.append(row);
    persist();
}

void ComicRequestLedger::setSelection(const QString& editionId, const QList<int>& pickedFileIndices,
                                       ComicEditionFileSelector::ComicPayloadKind kind)
{
    const int at = indexOf(editionId);
    if (at < 0) return;
    m_rows[at].pickedFileIndices = pickedFileIndices;
    m_rows[at].payloadKind       = kind;
    persist();
}

void ComicRequestLedger::setState(const QString& editionId, const QString& state)
{
    const int at = indexOf(editionId);
    if (at < 0) return;
    m_rows[at].state = state;
    persist();
}

void ComicRequestLedger::remove(const QString& editionId)
{
    const int at = indexOf(editionId);
    if (at < 0) return;
    m_rows.removeAt(at);
    persist();
}
