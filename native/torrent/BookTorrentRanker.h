#pragma once
#include "TorrentResult.h"
#include <QList>
#include <QString>

struct RankedTorrent {
    TorrentResult src;
    int     matchTier = 0;   // 4 exact · 3 prefix · 2 all-tokens · 1 partial · 0 none
    bool    pack = false;
    QString formatGuess;     // "EPUB"/"PDF"/"MOBI"/"AZW3"/"FB2"/"EBOOK"
};

class BookTorrentRanker {
public:
    // Dedup by canonical infoHash (keep max seeders), score each row, sort:
    // matchTier desc, then seeders desc. Rows with no infoHash dedup by normalized title.
    static QList<RankedTorrent> rank(const QString& title, const QString& author,
                                     const QList<TorrentResult>& raw);
    // The download-to-read shelf shows readable books only. Tankorent's indexers
    // already classify each hit by category (PirateBay 1xx=audio/2xx=video/6xx=books;
    // ExtTorrents Music/Movies/Books); we trust that first and fall back to strong
    // title signals for the one source that returns no category. Audiobooks and
    // video releases are dropped so an audiobook never masquerades as an epub.
    static bool    isReadableBook(const TorrentResult& r);
    // exposed for tests
    static QString stripArticles(QString s);
    static int     matchTier(const QString& title, const QString& author, const QString& candidate);
    static bool    looksLikePack(const QString& title, qint64 sizeBytes);
    static QString guessFormat(const QString& title);
};
