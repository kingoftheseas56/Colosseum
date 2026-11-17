#include "AudiobookRecordingMatch.h"

#include <QMetaType>
#include <QRegularExpression>
#include <QSet>

namespace {
QString normalized(const QVariant& value) {
    return value.toString().simplified().toCaseFolded();
}
QString compactId(const QString& value) {
    QString result;
    for (const QChar c : value.toUpper())
        if (c.isLetterOrNumber()) result.append(c);
    return result;
}
QSet<QString> identifiers(const QVariantMap& row) {
    QSet<QString> ids;
    const QStringList fields = {QStringLiteral("asin"),QStringLiteral("isbn"),
                                QStringLiteral("isbn10"),QStringLiteral("isbn13")};
    for (const QString& field : fields) {
        const QString id = compactId(row.value(field).toString());
        if (id.size() >= 10 && id.size() <= 17) ids.insert(id);
    }
    const QVariantMap obj = row.value(QStringLiteral("identifiers")).toMap();
    for (const QString& field : fields) {
        const QVariant v = obj.value(field);
        QStringList values;
        if (v.metaType().id() == QMetaType::QStringList) values = v.toStringList();
        else if (v.metaType().id() == QMetaType::QVariantList)
            for (const QVariant& x : v.toList()) values << x.toString();
        else values << v.toString();
        for (const QString& value : values) {
            const QString id = compactId(value);
            if (id.size() >= 10 && id.size() <= 17) ids.insert(id);
        }
    }
    static const QRegularExpression embedded(
        QStringLiteral(R"(\b(?:ASIN|ISBN(?:-1[03])?)\s*[:#-]?\s*([A-Z0-9-]{10,20})\b)"),
        QRegularExpression::CaseInsensitiveOption);
    for (const QString& field : {QStringLiteral("title"),QStringLiteral("detail"),
                                 QStringLiteral("description")}) {
        const QString text = row.value(field).toString();
        auto matches = embedded.globalMatch(text);
        while (matches.hasNext()) {
            const QString id = compactId(matches.next().captured(1));
            if (id.size() >= 10 && id.size() <= 17) ids.insert(id);
        }
    }
    return ids;
}
QSet<QString> narrators(const QVariant& value) {
    QSet<QString> set;
    QStringList names;
    if (value.canConvert<QStringList>()) names = value.toStringList();
    else if (value.canConvert<QVariantList>()) {
        for (const QVariant& item : value.toList()) names.append(item.toString());
    } else names = value.toString().split(QRegularExpression(QStringLiteral("[,;/]|\\band\\b")),
                                         Qt::SkipEmptyParts);
    for (const QString& name : names) {
        const QString n = name.simplified().toCaseFolded();
        if (!n.isEmpty()) set.insert(n);
    }
    return set;
}
bool overlaps(const QSet<QString>& a,const QSet<QString>& b) {
    for (const QString& name : a) if (b.contains(name)) return true;
    return false;
}
bool contradicts(const QVariantMap& a,const QVariantMap& b) {
    const QString al = normalized(a.value(QStringLiteral("language")));
    const QString bl = normalized(b.value(QStringLiteral("language")));
    if (!al.isEmpty() && !bl.isEmpty() && al != bl) return true;
    const QSet<QString> an = narrators(a.value(QStringLiteral("narrators")));
    const QSet<QString> bn = narrators(b.value(QStringLiteral("narrators")));
    if (!an.isEmpty() && !bn.isEmpty() && !overlaps(an,bn)) return true;
    for (const QString& field : {QStringLiteral("abridgement")}) {
        const QString av = normalized(a.value(field)), bv = normalized(b.value(field));
        if (!av.isEmpty() && !bv.isEmpty() && av != bv) return true;
    }
    return false;
}
QVariantMap none(const QString& reason) {
    return {{QStringLiteral("verdict"),QStringLiteral("none")},
            {QStringLiteral("reason"),reason},
            {QStringLiteral("recordingKey"),QString()}};
}
QVariantMap found(const QString& verdict,const QVariantMap& row) {
    return {{QStringLiteral("verdict"),verdict},
            {QStringLiteral("reason"),QString()},
            {QStringLiteral("recordingKey"),row.value(QStringLiteral("recordingKey")).toString()}};
}
}

QVariantMap AudiobookRecordingMatch::match(const QVariantMap& source,
                                          const QVariantList& recordings) {
    if (source.value(QStringLiteral("bundle")).toBool())
        return none(QStringLiteral("bundle"));
    if (recordings.isEmpty()) return none(QStringLiteral("no_candidates"));
    // A uniquely resolved work can be asserted by the caller's catalogue lookup,
    // or independently established from every candidate's normalized work metadata.
    const QString sourceWork = normalized(source.value(QStringLiteral("title")))
        + QStringLiteral("|") + normalized(source.value(QStringLiteral("author")));
    QSet<QString> candidateWorks;
    bool everyWorkNamed = true;
    for (const QVariant& item : recordings) {
        const QVariantMap r = item.toMap();
        if (r.value(QStringLiteral("bundle")).toBool()) continue;
        const QString title = normalized(r.value(QStringLiteral("workTitle"),
                                                  r.value(QStringLiteral("title"))));
        const QString author = normalized(r.value(QStringLiteral("workAuthor"),
                                                   r.value(QStringLiteral("author"))));
        if (title.isEmpty() || author.isEmpty()) { everyWorkNamed = false; continue; }
        candidateWorks.insert(title + QStringLiteral("|") + author);
    }
    const bool uniqueWork = (source.value(QStringLiteral("workMatchCount")).toInt() == 1)
        || source.value(QStringLiteral("workUnique")).toBool()
        || (everyWorkNamed && !sourceWork.startsWith(QChar('|')) && !sourceWork.endsWith(QChar('|'))
            && candidateWorks.size() == 1 && candidateWorks.contains(sourceWork));
    const QSet<QString> sourceIds = identifiers(source);
    QList<QVariantMap> exact, probable;
    bool identifierConflict = false;
    bool anyConflict = false;
    for (const QVariant& candidate : recordings) {
        const QVariantMap r = candidate.toMap();
        if (r.value(QStringLiteral("bundle")).toBool()
            || r.value(QStringLiteral("recordingKey")).toString().isEmpty()) continue;
        const QSet<QString> recIds = identifiers(r);
        const bool idHit = !sourceIds.isEmpty() && overlaps(sourceIds,recIds);
        const bool conflict = contradicts(source,r);
        if (idHit) {
            if (conflict) identifierConflict = true;
            else exact.append(r);
            continue;
        }
        if (conflict || (!sourceIds.isEmpty() && !recIds.isEmpty())) {
            anyConflict = true;
            continue;
        }
        // Probable requires explicit evidence that the work match was unique.
        const bool oneWork = uniqueWork
            && (!source.value(QStringLiteral("workMatchCount")).isValid()
                || source.value(QStringLiteral("workMatchCount")).toInt() == 1);
        const QString sl = normalized(source.value(QStringLiteral("language")));
        const QString rl = normalized(r.value(QStringLiteral("language")));
        const QSet<QString> sn = narrators(source.value(QStringLiteral("narrators")));
        const QSet<QString> rn = narrators(r.value(QStringLiteral("narrators")));
        if (oneWork && !sl.isEmpty() && sl == rl
            && !sn.isEmpty() && overlaps(sn,rn))
            probable.append(r);
    }
    if (identifierConflict) return none(QStringLiteral("conflict"));
    if (exact.size() > 1) return none(QStringLiteral("ambiguous"));
    if (exact.size() == 1) return found(QStringLiteral("exact"),exact.first());
    if (probable.size() > 1) return none(QStringLiteral("ambiguous"));
    if (probable.size() == 1) return found(QStringLiteral("probable"),probable.first());
    if (source.value(QStringLiteral("workMatchCount")).toInt() > 1)
        return none(QStringLiteral("ambiguous"));
    return none(anyConflict ? QStringLiteral("conflict") : QStringLiteral("no_match"));
}
