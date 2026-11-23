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
    // The mirror of isReadableBook for audiobooks: audio by the source's category or by
    // strong title tokens (audiobook/unabridged/mp3/m4b/...). A video title never counts.
    static bool    isAudiobook(const TorrentResult& r);
    // "m4b"/"mp3"/"m4a"/"aax"/"flac"/"ogg" when the title says so, else empty.
    static QString guessAudioFormat(const QString& title);
    // Multi-book pack ("Series 1-4", "Books 1-3", "Collection", "Box Set"); a single
    // book split in parts ("Part 1 of 2") is NOT a bundle.
    static bool    looksLikeBundle(const QString& title);
    // exposed for tests
    static QString stripArticles(QString s);
    static int     matchTier(const QString& title, const QString& author, const QString& candidate);
    static bool    looksLikePack(const QString& title, qint64 sizeBytes);
    static QString guessFormat(const QString& title);
};
