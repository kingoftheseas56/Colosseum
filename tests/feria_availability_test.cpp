#include "PorticoTrendAdapters.h"
#include "PorticoCanonicalizer.h"
#include "PorticoContentStore.h"
#include "PorticoAvailabilityService.h"
#include "PorticoRuntimeFacade.h"
#include <QEventLoop>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>
#include <cstdlib>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int checks = 0;
    auto check = [&](bool ok, const char *message) {
        ++checks;
        if (!ok) { qCritical() << message; std::exit(1); }
    };
    PorticoTrend::Query query;
    QString error;
    const auto shelf = StremioCatalogAdapter::parsePayload(
        R"({"metas":[{"id":"tt6933238","imdb_id":"tt6933238","name":"Unabomber","releaseInfo":"2026"}]})",
        query, {{"type", "movie"}, {"providerId", "netflix"}}, &error);
    check(error.isEmpty() && shelf.items.size() == 1, "Catalog fixture parses");
    check(shelf.items.first().externalIds.value("imdb").toString() == "tt6933238",
          "Streaming catalog must preserve IMDb identity for availability lookup");
    PorticoContentStore store;
    auto netflix = PorticoCanonicalizer::decorate(shelf).toVariantMap();
    netflix.insert("providerId", "netflix");
    store.ingestShelf("netflix", netflix);
    auto prime = netflix;
    prime.insert("providerId", "prime");
    store.ingestShelf("prime", prime);
    const auto canonical = PorticoCanonicalizer::decorate(shelf).items.first().canonicalKey;
    const auto title = store.title(canonical);
    check(title.value("catalogProviders").toStringList().contains("netflix"),
          "Netflix provenance must survive ingestion from another shelf");
    check(title.value("catalogProviders").toStringList().contains("prime"),
          "Store retains all contributing catalog providers");
    auto oldItem = shelf.items.first();
    oldItem.externalIds.clear();
    check(canonical == PorticoCanonicalizer::canonicalKey(oldItem), "Availability metadata preserves existing saved-title IDs");
    const auto item = title.value("_trend").toMap();
    const QByteArray offers = R"JSON({"data":{"popularTitles":{"edges":[
      {"node":{"objectType":"MOVIE","content":{"title":"Unabomber","originalReleaseYear":1996,"externalIds":{"imdbId":"tt0118024"}},"offers":[{"package":{"technicalName":"prime","clearName":"Prime Video"},"monetizationType":"FLATRATE","standardWebURL":"https://primevideo.com/wrong"}]}},
      {"node":{"objectType":"MOVIE","content":{"title":"Unabomber","originalReleaseYear":2026,"fullPath":"/us/movie/unabomber","externalIds":{"imdbId":"tt6933238"}},"offers":[
        {"package":{"technicalName":"netflix","clearName":"Netflix"},"monetizationType":"FLATRATE","standardWebURL":"https://www.netflix.com/title/82010386"},
        {"package":{"technicalName":"netflixbasicwithads","clearName":"Netflix Standard with Ads"},"monetizationType":"FLATRATE","standardWebURL":"https://www.netflix.com/title/82010386"},
        {"package":{"technicalName":"amazon","clearName":"Amazon Video"},"monetizationType":"RENT","standardWebURL":"https://amazon.com/title/1"},
        {"package":{"technicalName":"netflix","clearName":"Netflix"},"standardWebURL":"javascript:alert(1)"}
      ]}}
    ]}}})JSON";
    const auto result = PorticoAvailabilityService::parse(item, "US", offers);
    const auto doors = result.value("offers").toList();
    check(result.value("status") == "ready" && doors.size() == 2, "Match IMDb; collapse duplicates and reject non-web URLs");
    check(doors.first().toMap().value("providerId") == "netflix", "Wrong namesake's offers are excluded");
    check(doors.first().toMap().value("availabilityConfirmed").toBool(), "Offer is explicitly confirmed");
    check(doors.last().toMap().value("offerType") == "Rent", "Rental is distinguished from subscription");
    check(result.value("attributionUrl") == "https://www.justwatch.com/us/movie/unabomber", "JustWatch attribution links to matched title");
    auto wrong = item;
    wrong.insert("externalIds", QVariantMap{{"imdb", "tt9999999"}});
    check(PorticoAvailabilityService::parse(wrong, "US", offers).value("status") == "unmatched", "No name-only fallback when IMDb mismatches");
    check(PorticoAvailabilityService::parse(item, "US", R"({"errors":[{"message":"down"}]})").value("status") == "error", "Upstream errors remain unconfirmed");
    const auto empty = PorticoAvailabilityService::parse(item, "IN", R"({"data":{"popularTitles":{"edges":[{"node":{"objectType":"MOVIE","content":{"externalIds":{"imdbId":"tt6933238"}},"offers":[]}}]}}})");
    check(empty.value("status") == "ready" && empty.value("offers").toList().isEmpty(), "No regional offers is distinct from lookup failure");
    auto noId = item;
    noId.insert("externalIds", QVariantMap{});
    check(PorticoAvailabilityService::parse(noId, "US", offers).value("offers").toList().size() == 2, "ID-less fallback checks exact title and year");
    const QByteArray ambiguous = R"({"data":{"popularTitles":{"edges":[
        {"node":{"objectType":"MOVIE","content":{"title":"Unabomber","originalReleaseYear":2026},"offers":[]}},
        {"node":{"objectType":"MOVIE","content":{"title":"Unabomber","originalReleaseYear":2026},"offers":[]}}
    ]}}})";
    check(PorticoAvailabilityService::parse(noId, "US", ambiguous).value("status") == "unmatched", "Ambiguous ID-less matches stay unconfirmed");
    noId.insert("year", 2005);
    check(PorticoAvailabilityService::parse(noId, "US", offers).value("status") == "unmatched", "ID-less lookup excludes wrong year");
    PorticoAvailabilityService cache;
    check(cache.lookup(item, "US").value("status") == "idle", "Unfetched title is not confirmed");
    PorticoRuntimeFacade runtime;
    runtime.contentStore()->ingestShelf("netflix", netflix);
    runtime.setActiveApps({"netflix"});
    const auto catalogDoors = runtime.destinationsFor(runtime.title(canonical));
    check(catalogDoors.first().toMap().value("providerId") == "netflix"
          && catalogDoors.first().toMap().value("catalogListed").toBool(), "Original Netflix catalog ranks before generic search fallbacks");
    check(!catalogDoors.first().toMap().value("availabilityConfirmed").toBool(), "Catalog provenance alone does not claim regional availability");
    if (app.arguments().contains("--live")) {
        for (const QString &country : {QString("US"), QString("IN")}) {
            runtime.setRegion(country);
            QEventLoop loop;
            QObject::connect(&runtime, &PorticoRuntimeFacade::availabilityRevisionChanged, &loop, [&] {
                if (runtime.availabilityFor(item).value("status") != "loading") loop.quit();
            });
            QTimer::singleShot(20000, &loop, &QEventLoop::quit);
            runtime.requestAvailability(item);
            loop.exec();
            const auto observed = runtime.availabilityFor(item);
            qInfo() << country << observed;
            check(observed.value("status") == "ready", "Live JustWatch title resolves");
            const auto liveOffers = observed.value("offers").toList();
            check(!liveOffers.isEmpty() && liveOffers.first().toMap().value("providerId") == "netflix", "Live Unabomber offer is Netflix");
            check(runtime.destinationsFor(item).first().toMap().value("availabilityConfirmed").toBool(), "Runtime forwards confirmed offer to QML");
            runtime.setRegion(country == "US" ? "GB" : "CA");
            check(runtime.availabilityFor(item).value("status") == "idle", "Offers from another region never leak into current country");
        }
    }
    qInfo() << checks << "availability checks passed";
    return 0;
}
