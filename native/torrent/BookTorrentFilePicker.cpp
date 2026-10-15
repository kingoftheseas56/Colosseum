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
    // Only formats the reader actually renders (getEngineCandidates): epub/mobi/fb2/azw3/pdf.
    // Arc 14 D6 ground-truth (2026-08-21): AZW3 is KF8 in a PalmDB container — the vendored
    // engine's mobi.js detects it by content (PalmDB 'BOOKMOBI' magic, not extension) and opens
    // it through the same reflowable MOBI/KF8 path as .mobi. NO djvu — picking one the reader
    // can't open would leave the download unreadable.
    static const QSet<QString> exts{"epub","mobi","fb2","azw3","pdf"};
    return exts.contains(extOf(name));
}

int BookTorrentFilePicker::formatRank(const QString& ext){
    // TTS-aware: reflowable + TTS-capable (epub/mobi/fb2/azw3) beat fixed-layout PDF, which
    // the reader can't read aloud. epub first (best all-round); mobi/fb2/azw3 share a tier
    // (mirrors BiblioApi.js LIBGEN_FORMAT_TIER: epub 3, mobi/fb2/azw3 2, pdf 1); pdf last.
    if (ext=="epub") return 6;
    if (ext=="mobi") return 5;
    if (ext=="fb2")  return 5;
    if (ext=="azw3") return 5;
    if (ext=="pdf")  return 3;
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
