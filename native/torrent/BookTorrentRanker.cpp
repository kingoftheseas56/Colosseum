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
    std::sort(out.begin(), out.end(), [](const RankedTorrent& a, const RankedTorrent& b){
        if (a.matchTier != b.matchTier) return a.matchTier > b.matchTier;   // best match first
        return a.src.seeders > b.src.seeders;                               // then most seeders
    });
    return out;
}
