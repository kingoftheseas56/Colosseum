#include "BookTorrentFilePicker.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

static QString normName(QString s){
    s = s.toLower();
    s.replace(QRegularExpression("[._\\-]+"), " ");
    s.replace(QRegularExpression("[^a-z0-9 ]"), "");
    return s.replace(QRegularExpression("\\s+"), " ").trimmed();
}

QString BookTorrentFilePicker::extOf(const QString& name){ return QFileInfo(name).suffix().toLower(); }

bool BookTorrentFilePicker::isEbook(const QString& name){
    static const QSet<QString> exts{"epub","pdf","mobi","azw3","fb2"};   // NO djvu (reader can't render)
    return exts.contains(extOf(name));
}

int BookTorrentFilePicker::formatRank(const QString& ext){
    if (ext=="epub") return 6;
    if (ext=="azw3"||ext=="mobi") return 5;
    if (ext=="fb2") return 4;
    if (ext=="pdf") return 3;
    return 0;
}

PickedFile BookTorrentFilePicker::pick(const QString& title, const QString& author,
                                       const QList<ManifestFile>& files){
    const QString wantTitle = normName(title);
    const QStringList titleToks  = wantTitle.split(' ', Qt::SkipEmptyParts);
    const QStringList authorToks = normName(author).split(' ', Qt::SkipEmptyParts);
    PickedFile best;                 // idx == -1 by default
    int bestWhole=-1, bestTitleCov=-1, bestFmt=-1, bestAuthorCov=-1;
    for (const auto& f : files) {
        if (!isEbook(f.name)) continue;
        const QString stem = normName(QFileInfo(f.name).completeBaseName());  // ext stripped
        const int whole = (!wantTitle.isEmpty() && stem == wantTitle) ? 1 : 0;
        int titleCov = 0;  for (const auto& w : titleToks)  if (stem.contains(w)) ++titleCov;
        int authorCov = 0; for (const auto& w : authorToks) if (stem.contains(w)) ++authorCov;
        const int fmt = formatRank(extOf(f.name));
        // lexicographic: exact whole-title stem, then title-token coverage,
        // then format preference, then author tokens ONLY as a final tie-break.
        const bool better =
            whole > bestWhole ||
            (whole==bestWhole && titleCov > bestTitleCov) ||
            (whole==bestWhole && titleCov==bestTitleCov && fmt > bestFmt) ||
            (whole==bestWhole && titleCov==bestTitleCov && fmt==bestFmt && authorCov > bestAuthorCov);
        if (best.idx < 0 || better) {
            best.idx=f.idx; best.name=f.name; best.ext=extOf(f.name);
            bestWhole=whole; bestTitleCov=titleCov; bestFmt=fmt; bestAuthorCov=authorCov;
        }
    }
    return best;
}
