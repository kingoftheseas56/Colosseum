#include "BookTorrentRanker.h"
#include <QHash>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>

static QString norm(QString s){
    s = s.toLower();
    s.replace(QRegularExpression("[._\\-]+"), " ");
    s.replace(QRegularExpression("[^a-z0-9 ]"), "");
    s.replace(QRegularExpression("\\s+"), " ");
    return s.trimmed();
}

QString BookTorrentRanker::stripArticles(QString s){
    s = norm(s);
    static const QRegularExpression lead("^(the|a|an)\\s+");
    s.remove(lead);
    return s;
}

int BookTorrentRanker::matchTier(const QString& title, const QString& /*author*/, const QString& candidate){
    const QString t = stripArticles(title);
    const QString c = stripArticles(candidate);
    if (t.isEmpty()) return 0;
    if (c == t) return 4;                        // exact
    if (c.startsWith(t + ' ')) return 3;         // prefix ONLY on a token boundary
    const QStringList titleToks = t.split(' ', Qt::SkipEmptyParts);
    if (titleToks.isEmpty()) return 0;
    const QStringList candList = c.split(' ', Qt::SkipEmptyParts);
    const QSet<QString> candToks(candList.cbegin(), candList.cend());
    bool all = true;
    for (const auto& tok : titleToks) if (!candToks.contains(tok)) { all = false; break; }
    if (all) return 2;                           // all title tokens present as WHOLE tokens
    for (const auto& tok : titleToks) if (candToks.contains(tok)) return 1;  // any whole token
    return 0;
}

bool BookTorrentRanker::looksLikePack(const QString& title, qint64 sizeBytes){
    const QString t = title.toLower();
    // Unambiguous: an explicit multi-item count ("5000 books", "12 epubs").
    static const QRegularExpression countWords("\\b\\d{2,}\\s*(books|epubs|ebooks|volumes?)\\b");
    if (countWords.match(t).hasMatch()) return true;
    // Words that also appear in legit single-novel titles ("The Midnight Library",
    // "Omnibus") only count as a pack when the payload is also oversized.
    static const QRegularExpression softPackWords(
        "\\b(collection|collections|pack|library|anthology|omnibus|bundle)\\b");
    const bool oversize = sizeBytes > 800LL * 1024 * 1024;  // a single scanned PDF can hit 100s of MB
    if (softPackWords.match(t).hasMatch()) return oversize;
    return oversize;
}

namespace {
enum class MediaKind { Book, Audio, Video, NonBook, Unknown };

// Tankorent's indexers already stamp each hit with the source site's own
// category. Trust it: PirateBay uses numeric buckets (1xx audio incl. 102
// audiobooks, 2xx video, 3/4/5xx apps/games/porn, 6xx e-books/comics/other);
// ExtTorrents uses word labels. Torrents-CSV returns none → Unknown, gated by
// the title below.
MediaKind kindFromCategory(const TorrentResult& r){
    const QString sk = r.sourceKey.toLower();
    if (sk == "piratebay" && !r.categoryId.isEmpty()){
        switch (r.categoryId.at(0).toLatin1()){
            case '1': return MediaKind::Audio;
            case '2': return MediaKind::Video;
            case '3': case '4': case '5': return MediaKind::NonBook;
            case '6': return MediaKind::Book;
            default:  return MediaKind::Unknown;
        }
    }
    if (sk == "exttorrents" && !r.category.isEmpty()){
        const QString c = r.category.toLower();
        if (c == "books")   return MediaKind::Book;
        if (c == "music")   return MediaKind::Audio;
        if (c == "movies" || c == "tv" || c == "documentaries") return MediaKind::Video;
        if (c == "xxx")     return MediaKind::NonBook;
        return MediaKind::Unknown;   // "Other"/"Games"/"Apps"/"Anime" → title-gated
    }
    return MediaKind::Unknown;
}

// High-precision title signals — only tokens that are near-exclusive to audio
// or video releases, so a genuine e-book is never dropped by a stray word.
MediaKind kindFromTitle(const QString& title){
    const QString t = title.toLower();
    static const QRegularExpression audioRe(
        "\\b(audiobooks?|audio book|unabridged|abridged|mp3|m4b|m4a|aax|\\d{2,3}\\s?kbps)\\b");
    if (audioRe.match(t).hasMatch()) return MediaKind::Audio;
    static const QRegularExpression videoRe(
        "\\b(1080p|720p|2160p|480p|blu-?ray|bd-?rip|br-?rip|web-?rip|web-?dl|hd-?tv|"
        "hd-?rip|dvd-?rip|x264|x265|h ?264|h ?265|hevc|xvid|s\\d{1,2}e\\d{1,2})\\b");
    if (videoRe.match(t).hasMatch()) return MediaKind::Video;
    return MediaKind::Unknown;
}
} // namespace

bool BookTorrentRanker::isReadableBook(const TorrentResult& r){
    // A title that says "Audiobook"/"S01E01" beats a coarse or missing category —
    // check it first so an audiobook filed under a generic bucket still drops.
    const MediaKind tk = kindFromTitle(r.title);
    if (tk == MediaKind::Audio || tk == MediaKind::Video) return false;
    const MediaKind ck = kindFromCategory(r);
    if (ck == MediaKind::Audio || ck == MediaKind::Video || ck == MediaKind::NonBook)
        return false;
    if (ck == MediaKind::Book) return true;   // tracker-confirmed e-book category → trust it at ANY size
    // ck == Unknown (e.g. torrents-csv exposes no categories): the title cleared the
    // audio/video nets, so a generous size cap is the THIRD net — a genuine e-book is
    // small; anything past ~100 MB here is almost certainly a plainly-named audiobook
    // or an A/V rip that beat the title regex. (sizeBytes 0 = unknown → don't guess.)
    static const qint64 kMaxUncategorizedEbookBytes = 100LL * 1024 * 1024;
    if (r.sizeBytes > kMaxUncategorizedEbookBytes) return false;
    return true;
}

QString BookTorrentRanker::guessFormat(const QString& title){
    const QString t = title.toLower();
    struct { const char* ext; const char* label; } m[] = {
        {"epub","EPUB"},{"azw3","AZW3"},{"mobi","MOBI"},{"pdf","PDF"},{"fb2","FB2"}
    };
    for (auto& e : m) if (t.contains(QString(".")+e.ext) || t.contains(QString(" ")+e.ext)) return e.label;
    return "EBOOK";
}

QList<RankedTorrent> BookTorrentRanker::rank(const QString& title, const QString& author,
                                             const QList<TorrentResult>& raw){
    QHash<QString, TorrentResult> best;   // dedup: infoHash, else normalized title
    for (const auto& r : raw) {
        const QString key = !r.infoHash.isEmpty() ? r.infoHash.toLower() : ("t:" + norm(r.title));
        auto it = best.find(key);
        if (it == best.end() || r.seeders > it.value().seeders) best.insert(key, r);
    }
    QList<RankedTorrent> out;
    for (const auto& r : best) {
        RankedTorrent rt;
        rt.src = r;
        rt.matchTier = matchTier(title, author, r.title);
        rt.pack = looksLikePack(r.title, r.sizeBytes);
        rt.formatGuess = guessFormat(r.title);
        out.push_back(rt);
    }
    // Merge "title-leading" (tier 3) and "all-title-tokens" (tier 2) into ONE bucket:
    // for the SAME book, "A Game of Thrones - GRRM" and "GRRM - A Game of Thrones" are
    // equally the book — only word order differs — so seeders (the load-bearing
    // availability signal for a torrent) must decide between them, not that spurious
    // gap. Exact (4) stays on top so a high-seed prefix superset (e.g. a "Dune Messiah"
    // sequel) can't leapfrog the exact "Dune"; partial (1) and none (0) stay below.
    // (Hemanth 2026-07-13: high-seeded torrents were sinking beneath 1-seed matches.)
    auto matchBucket = [](int tier){ return tier == 3 ? 2 : tier; };
    std::sort(out.begin(), out.end(), [&](const RankedTorrent& a, const RankedTorrent& b){
        const int ba = matchBucket(a.matchTier), bb = matchBucket(b.matchTier);
        if (ba != bb) return ba > bb;              // stronger match first
        return a.src.seeders > b.src.seeders;      // within a bucket, most seeders first
    });
    return out;
}
