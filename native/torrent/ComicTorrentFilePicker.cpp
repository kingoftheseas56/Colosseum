#include "ComicTorrentFilePicker.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace {
QString normalized(QString value)
{
    value = value.toLower();
    value.replace(QRegularExpression(QStringLiteral("[._\\-]+")), QStringLiteral(" "));
    value.replace(QRegularExpression(QStringLiteral("[^a-z0-9 ]")), QString());
    value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return value.trimmed();
}
} // namespace

QString ComicTorrentFilePicker::extOf(const QString& name)
{
    return QFileInfo(name).suffix().toLower();
}

bool ComicTorrentFilePicker::isComicArchive(const QString& name)
{
    static const QSet<QString> extensions{
        QStringLiteral("cbr"), QStringLiteral("cbz"),
        QStringLiteral("cb7"), QStringLiteral("cbt")
    };
    return extensions.contains(extOf(name));
}

int ComicTorrentFilePicker::formatRank(const QString& ext)
{
    if (ext == QStringLiteral("cbz")) return 4;
    if (ext == QStringLiteral("cbr")) return 3;
    if (ext == QStringLiteral("cb7")) return 2;
    if (ext == QStringLiteral("cbt")) return 1;
    return 0;
}

ComicArchiveDecision ComicTorrentFilePicker::decide(const QString& title,
                                                    const QList<ManifestFile>& files)
{
    const QString wanted = normalized(title);
    const QStringList tokens = wanted.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    ComicArchiveDecision decision;
    QList<ComicArchiveCandidate> eligible;
    for (const ManifestFile& file : files) {
        if (!isComicArchive(file.name)) continue;
        const QString stem = normalized(QFileInfo(file.name).completeBaseName());
        ComicArchiveCandidate c;
        c.index = file.idx;
        c.name = file.name;
        c.extension = extOf(file.name);
        c.bytes = file.length;
        c.exactTitle = (!wanted.isEmpty() && stem == wanted);
        for (const QString& token : tokens)
            if (stem.contains(token)) ++c.tokenCoverage;
        eligible.append(c);
    }

    // Order candidates for display only: exact-title first, then token coverage,
    // then easiest-to-extract format. Ordering NEVER decides an ambiguous pack.
    std::sort(eligible.begin(), eligible.end(),
              [](const ComicArchiveCandidate& a, const ComicArchiveCandidate& b) {
        if (a.exactTitle != b.exactTitle) return a.exactTitle;
        if (a.tokenCoverage != b.tokenCoverage) return a.tokenCoverage > b.tokenCoverage;
        const int fa = formatRank(a.extension), fb = formatRank(b.extension);
        if (fa != fb) return fa > fb;
        return a.index < b.index;
    });
    decision.candidates = eligible;

    if (eligible.isEmpty())
        return decision;   // no comic file — the caller fails honestly

    const auto toPicked = [](const ComicArchiveCandidate& c) {
        return PickedFile{c.index, c.name, c.extension};
    };

    if (eligible.size() == 1) {
        decision.selected = toPicked(eligible.first());
        return decision;   // a lone comic archive auto-selects
    }

    int exactCount = 0;
    const ComicArchiveCandidate* exactOne = nullptr;
    for (const ComicArchiveCandidate& c : eligible)
        if (c.exactTitle) { ++exactCount; exactOne = &c; }
    if (exactCount == 1) {
        decision.selected = toPicked(*exactOne);
        return decision;   // one and only one exact canonical-title archive
    }

    decision.requiresChoice = true;   // multi-volume / ambiguous — the user chooses
    return decision;
}

PickedFile ComicTorrentFilePicker::pick(const QString& title,
                                         const QList<ManifestFile>& files)
{
    const ComicArchiveDecision decision = decide(title, files);
    return decision.requiresChoice ? PickedFile{} : decision.selected;
}
