#include "ComicTorrentFilePicker.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

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

PickedFile ComicTorrentFilePicker::pick(const QString& title,
                                         const QList<ManifestFile>& files)
{
    const QString wanted = normalized(title);
    const QStringList tokens = wanted.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    PickedFile best;
    int bestWhole = -1;
    int bestCoverage = -1;
    int bestFormat = -1;

    for (const ManifestFile& file : files) {
        if (!isComicArchive(file.name)) continue;
        const QString stem = normalized(QFileInfo(file.name).completeBaseName());
        const int whole = (!wanted.isEmpty() && stem == wanted) ? 1 : 0;
        int coverage = 0;
        for (const QString& token : tokens)
            if (stem.contains(token)) ++coverage;
        const QString ext = extOf(file.name);
        const int format = formatRank(ext);
        const bool better = whole > bestWhole
            || (whole == bestWhole && coverage > bestCoverage)
            || (whole == bestWhole && coverage == bestCoverage && format > bestFormat);
        if (best.idx < 0 || better) {
            best = PickedFile{file.idx, file.name, ext};
            bestWhole = whole;
            bestCoverage = coverage;
            bestFormat = format;
        }
    }
    return best;
}
