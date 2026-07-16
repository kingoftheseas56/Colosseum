// KnabenIndexer contract (pure, no network): the request is a RELEVANCE search
// that NEVER sorts by seeders (seeder-sort makes knaben ignore the query and
// return a global SEO-spam firehose), and the parse maps the aggregator's JSON
// into TorrentResults faithfully — dropping rows without a usable 40-hex
// infohash and keeping each row's ORIGIN tracker (e.g. 1337x) as provenance.
#include "torrent/KnabenIndexer.h"
#include "torrent/TorrentResult.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool cond, const char* msg)
{
    if (!cond) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}
}

int main()
{
    // ── buildRequestBody: relevance search, safety filters, NEVER seeder-sorted ──
    {
        const QByteArray body = KnabenIndexer::buildRequestBody(QStringLiteral("Invincible Compendium"), 50);
        require(body.contains("\"query\":\"Invincible Compendium\""), "body carries the query verbatim");
        require(body.contains("\"search_type\":\"score\""), "body uses relevance (score) search");
        require(!body.contains("order_by"),
                "body NEVER sets order_by — seeder-sort makes knaben ignore the query (spam)");
        require(body.contains("\"hide_xxx\":true"), "adult filtered at the source");
        require(body.contains("\"hide_unsafe\":true"), "malware filtered at the source");

        // A category scope, when supplied, rides as a categories[] array; absent otherwise.
        const QByteArray scoped = KnabenIndexer::buildRequestBody(QStringLiteral("x"), 10, QStringLiteral("300000"));
        require(scoped.contains("\"categories\":[300000]"), "a category scope rides as categories[]");
        const QByteArray unscoped = KnabenIndexer::buildRequestBody(QStringLiteral("x"), 10, QString());
        require(!unscoped.contains("categories"), "no category scope -> no categories field");
    }

    // ── parseHits: real aggregator shape (1337x + TPB + two junk rows) ──
    {
        const QByteArray sample = QByteArrayLiteral(
            "{\"hits\":["
            "{\"title\":\"Invincible Compendium Vol. 1-3\","
             "\"hash\":\"AF86C88E059C253809C67B77A85F584C5449DC9C\","
             "\"seeders\":18,\"peers\":4,\"bytes\":8366844,\"tracker\":\"1337x\","
             "\"magnetUrl\":\"magnet:?xt=urn:btih:AF86C88E059C253809C67B77A85F584C5449DC9C&dn=x&tr=udp%3A%2F%2Ftr%3A1337\","
             "\"details\":\"https://knaben.xyz/1337x/x\",\"date\":\"2019-03-12T13:34:03+00:00\"},"
            "{\"title\":\"Invincible Complete Set\","
             "\"hash\":\"b2b58c79a36d000000000000000000000000abcd\","
             "\"seeders\":5,\"peers\":1,\"bytes\":1024,\"tracker\":\"The Pirate Bay\",\"magnetUrl\":\"\"},"
            "{\"title\":\"row with no usable hash\",\"hash\":\"nothex\",\"seeders\":9,\"tracker\":\"1337x\"},"
            "{\"title\":\"\",\"hash\":\"CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC\",\"seeders\":9}"
            "]}");

        const QList<TorrentResult> rows = KnabenIndexer::parseHits(sample);
        require(rows.size() == 2, "junk rows (bad hash, empty title) dropped; 2 usable survive");

        const TorrentResult& a = rows.at(0);
        require(a.title == QStringLiteral("Invincible Compendium Vol. 1-3"), "row 0 title");
        require(a.infoHash == QStringLiteral("af86c88e059c253809c67b77a85f584c5449dc9c"),
                "row 0 infohash canonicalized to lowercase 40-hex");
        require(a.seeders == 18 && a.leechers == 4, "row 0 seeders + peers mapped");
        require(a.sizeBytes == 8366844, "row 0 size mapped");
        require(a.sourceKey == QStringLiteral("knaben") && a.sourceName == QStringLiteral("Knaben"),
                "row 0 tagged as the knaben source");
        require(a.category == QStringLiteral("1337x"),
                "row 0 keeps its ORIGIN tracker (1337x) as visible provenance");
        require(a.detailsUrl == QStringLiteral("https://knaben.xyz/1337x/x"), "row 0 details url mapped");
        require(a.publishDate.isValid(), "row 0 ISO date parsed");
        require(a.magnetUri.startsWith(QStringLiteral("magnet:?xt=urn:btih:AF86C88E")),
                "row 0 keeps knaben's own magnet (its curated tracker list) verbatim");

        const TorrentResult& b = rows.at(1);
        require(b.category == QStringLiteral("The Pirate Bay"), "row 1 origin tracker preserved");
        require(b.magnetUri.startsWith(QStringLiteral("magnet:?xt=urn:btih:b2b58c79a36d")),
                "row 1 had no magnetUrl -> one synthesized from the canonical (lowercase) infohash");
    }

    std::cout << "KNABEN_INDEXER_OK\n";
    return 0;
}
