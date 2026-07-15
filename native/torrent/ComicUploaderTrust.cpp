#include "ComicUploaderTrust.h"

#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

namespace ComicUploaderTrust {
namespace {

constexpr const char* kTrustResource = ":/tankorent/comics_uploader_trust.json";
constexpr int kSupportedVersion = 1;

void fillList(const QJsonArray& arr, QStringList& out)
{
    for (const auto& v : arr) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty())
            out.append(s);
    }
}

bool containsCi(const QStringList& list, const QString& tag)
{
    for (const QString& entry : list)
        if (entry.compare(tag, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

// Extracts the bounded release-tag text at the rightmost recognized position:
// "[Name]", "(Name)", "(- Name -)", or (fallback, only when no bracket/paren
// tag exists) a trailing "- Name" at the very end of the title. A bare
// substring occurrence elsewhere in the title (no surrounding [], (), or a
// leading "- ") is never returned — that is precisely what keeps "nem"
// inside "Nemesis" from reading as uploader evidence.
QString extractBoundedTag(const QString& title)
{
    const QString trimmed = title.trimmed();

    static const QRegularExpression bracket(QStringLiteral("\\[\\s*([^\\[\\]]+?)\\s*\\]"));
    static const QRegularExpression parenDashed(QStringLiteral("\\(\\s*-\\s*([^()]+?)\\s*-\\s*\\)"));
    static const QRegularExpression paren(QStringLiteral("\\(\\s*([^()]+?)\\s*\\)"));

    QString best;
    int bestPos = -1;
    const auto consider = [&](const QRegularExpression& re) {
        auto it = re.globalMatch(trimmed);
        while (it.hasNext()) {
            const auto m = it.next();
            if (m.capturedStart() > bestPos) {
                bestPos = m.capturedStart();
                best = m.captured(1).trimmed();
            }
        }
    };
    // Order matters: try "(- Name -)" before plain "(Name)" so an overlapping
    // plain-paren match at the SAME start position never overwrites the more
    // specific dashed capture (bestPos uses strict '>', ties keep the first).
    consider(bracket);
    consider(parenDashed);
    consider(paren);
    if (!best.isEmpty())
        return best;

    static const QRegularExpression trailingDash(QStringLiteral("-\\s*([A-Za-z0-9][A-Za-z0-9 ._]*)$"));
    if (const auto m = trailingDash.match(trimmed); m.hasMatch())
        return m.captured(1).trimmed();

    return QString();
}

} // namespace

TrustTable load()
{
    TrustTable table;

    QFile f(QString::fromLatin1(kTrustResource));
    if (!f.open(QIODevice::ReadOnly))
        return table; // missing resource → degrade to empty, no automatic trust

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return table;

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt(-1) != kSupportedVersion)
        return table; // unknown/missing version → ignore gracefully, empty table

    fillList(root.value(QStringLiteral("tier1")).toArray(), table.tier1);
    fillList(root.value(QStringLiteral("tier2")).toArray(), table.tier2);
    fillList(root.value(QStringLiteral("blocked")).toArray(), table.blocked);
    return table;
}

UploaderTrust taggedUploader(const QString& releaseTitle, const TrustTable& table)
{
    UploaderTrust result;

    const QString tag = extractBoundedTag(releaseTitle);
    if (tag.isEmpty())
        return result; // no bounded tag → tier stays 99 (unknown)

    result.name = tag;
    if (containsCi(table.blocked, tag))
        result.tier = -1;
    else if (containsCi(table.tier1, tag))
        result.tier = 1;
    else if (containsCi(table.tier2, tag))
        result.tier = 2;
    else
        result.tier = 99;
    return result;
}

} // namespace ComicUploaderTrust
