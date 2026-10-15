#pragma once
#include <QList>
#include <QString>

struct ManifestFile { int idx = 0; QString name; qint64 length = 0; };
struct PickedFile   { int idx = -1; QString name; QString ext; };

class BookTorrentFilePicker {
public:
    // Choose the single best ebook file for {title, author}. Returns idx == -1
    // when the manifest holds no renderable ebook file (caller fails honestly).
    static PickedFile pick(const QString& title, const QString& author,
                           const QList<ManifestFile>& files);
    static bool isEbook(const QString& name);     // ext in the ebook set (NO djvu)
    static int  formatRank(const QString& ext);    // higher = preferred (epub best)
    static QString extOf(const QString& name);
};
