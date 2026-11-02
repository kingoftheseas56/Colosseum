#include <QtTest>
#include <algorithm>
#include <QSet>
#include "vinyl/VinylHomeListening.h"

class ListeningSource final : public QObject {
    Q_OBJECT
public:
    struct Request { QString lane, operation, value; QVariantMap options; int id; };
    QList<Request> requests;
    QStringList cancelled;
    Q_INVOKABLE int request(QString lane, QString operation, QString value, QVariantMap options) {
        const int id = requests.size() + 1;
        requests.append({lane, operation, value, options, id}); return id;
    }
    Q_INVOKABLE void cancel(QString lane) { cancelled.append(lane); }
};

namespace {
QVariantMap seed(int value) {
    return {{"kind", "artist"}, {"provider", "youtube-music"},
        {"providerId", "UC" + QString::number(value).rightJustified(22, QLatin1Char('0'))}, {"name", QStringLiteral("Artist %1").arg(value)}};
}
QVariantList seeds() { QVariantList result; for (int i = 0; i < 5; ++i) result.append(seed(i)); return result; }
QVariantMap candidate(const QString &id, int artist) {
    return {{"kind", "album"}, {"providerId", id}, {"relation", "own-release"},
        {"artistRefs", QVariantList{seed(artist)}}, {"title", id}};
}
QVariantMap album(const QString &id, const QString &prefix = {}) {
    QVariantList tracks;
    for (int i = 0; i < 6; ++i) tracks.append(QVariantMap{{"id", QStringLiteral("%1-track-%2").arg(prefix.isEmpty() ? id : prefix).arg(i)},
        {"title", QStringLiteral("Track %1").arg(i)}, {"available", i != 5}, {"seconds", 180}});
    return {{"id", "canonical-" + id}, {"browseId", id}, {"title", id}, {"artists", QStringList{"Artist"}}, {"cover", "https://art.test/" + id + ".jpg"}, {"tracks", tracks}};
}
QVariantList personalSections() {
    return {QVariantMap{{"id", "from-your-artists"}, {"items", QVariantList{candidate("album-a", 0), candidate("album-b", 1), candidate("album-c", 2)}}},
            QVariantMap{{"id", "related-discoveries"}, {"items", QVariantList{seed(8)}}}};
}
void complete(VinylHomeListening &model, const ListeningSource::Request &request, const QVariantMap &data, const QString &error = {}) {
    QVERIFY(model.accept(request.lane, request.id, data, error));
}
}

class HomeListeningTest final : public QObject {
    Q_OBJECT
private slots:
    void mixesRetainAlbumIdentityAndBoundTheirTracklists() {
        ListeningSource source;
        VinylHomeListening model(nullptr, [] {}, [](const QVariantList &, bool) {});
        model.setInputs(&source, "profile", 1, seeds(), personalSections(), "2026-10-30/1");
        for (int i = 0; i < source.requests.size(); ++i) {
            const auto request = source.requests[i];
            if (request.operation == "album") complete(model, request, album(request.value));
            else complete(model, request, {{"items", QVariantList{candidate("adjacent-album", 8)}}});
        }
        model.openMix("personal");
        const auto view = model.view(); QCOMPARE(view.value("status").toString(), QStringLiteral("ready"));
        const auto songs = view.value("songs").toList(); QCOMPARE(songs.size(), 12);
        QSet<QString> unique;
        for (const auto &value : songs) {
            const auto song = value.toMap(), record = song.value("album").toMap(), track = song.value("track").toMap();
            QCOMPARE(record.value("tracks").toList()[song.value("index").toInt()].toMap(), track);
            QVERIFY(track.value("available").toBool()); unique.insert(VinylHomeListening::trackKey(track));
        }
        QCOMPARE(unique.size(), songs.size());
        model.openMix("discovery");
        const auto adjacent = model.view().value("songs").toList(); QVERIFY(!adjacent.isEmpty());
        for (const auto &song : adjacent) QCOMPARE(song.toMap().value("album").toMap().value("browseId").toString(), QStringLiteral("adjacent-album"));
        QVERIFY(!model.quickPicks().isEmpty());
        QCOMPARE(model.quickPicks().first().toMap().value("artist").toString(), QStringLiteral("Artist"));
        for (const auto &value : model.cards()) {
            const auto card = value.toMap();
            if (card.value("kind") != "mixes") continue;
            const auto covers = card.value("artworkCovers").toList();
            QVERIFY(!covers.isEmpty());
            QVERIFY(covers.size() <= 3);
            const auto mixSongs = card.value("mixId") == "personal" ? songs : adjacent;
            for (const auto &cover : covers) QVERIFY(std::any_of(mixSongs.begin(), mixSongs.end(), [&](const QVariant &song) {
                return song.toMap().value("album").toMap().value("cover") == cover;
            }));
        }
    }
    void profileSwitchRejectsLateAlbums() {
        ListeningSource source;
        VinylHomeListening model(nullptr, [] {}, [](const QVariantList &, bool) {});
        model.setInputs(&source, "profile-a", 1, seeds(), personalSections(), "day");
        const auto old = source.requests.first();
        model.setInputs(&source, "profile-b", 2, seeds(), {}, "day");
        complete(model, old, album(old.value)); model.openMix("personal");
        QVERIFY(model.view().value("songs").toList().isEmpty()); QVERIFY(!source.cancelled.isEmpty());
    }
    void failedDiscoveryRemainsReachableAndCanBeRetried() {
        ListeningSource source;
        VinylHomeListening model(nullptr, [] {}, [](const QVariantList &, bool) {});
        model.setInputs(&source, "profile", 1, seeds(), {personalSections().last()}, "day");
        QCOMPARE(source.requests.size(), 1);
        complete(model, source.requests.last(), {}, "offline");
        const auto cards = model.cards();
        const auto failed = std::find_if(cards.begin(), cards.end(), [](const QVariant &value) {
            return value.toMap().value("mixId") == "discovery";
        });
        QVERIFY(failed != cards.end());
        QCOMPARE(failed->toMap().value("status").toString(), QStringLiteral("error"));
        QVERIFY(model.quickPicks().isEmpty());
        model.openMix("discovery");
        QCOMPARE(source.requests.size(), 2);
        complete(model, source.requests.last(), {{"items", QVariantList{candidate("retried-album", 8)}}});
        complete(model, source.requests.last(), album("retried-album"));
        QCOMPARE(model.view().value("status").toString(), QStringLiteral("ready"));
        QVERIFY(!model.view().value("songs").toList().isEmpty());
    }
    void mismatchedAlbumCannotBecomeAPlayableMix() {
        ListeningSource source;
        VinylHomeListening model(nullptr, [] {}, [](const QVariantList &, bool) {});
        model.setInputs(&source, "profile", 1, seeds(), {personalSections().first()}, "day");
        model.openMix("personal");
        for (int i = 0; i < source.requests.size(); ++i) complete(model, source.requests[i], album("unrelated"));
        QVERIFY(model.view().value("songs").toList().isEmpty());
        QVERIFY(model.view().value("error").toString().contains("identity"));
    }
    void mixTimeoutDoesNotFailARestoredPausedStation() {
        ListeningSource source;
        VinylHomeListening model(nullptr, [] {}, [](const QVariantList &, bool) { QFAIL("paused station produced audio"); });
        model.setInputs(&source, "profile", 1, seeds(), {personalSections().first()}, "day");
        model.restoreRadio({{"kind", "artist-radio"}, {"seed", seed(0)}}, {"already-queued"});
        model.openMix("personal");
        QCOMPARE(model.radioState(), QStringLiteral("ready"));
        QTRY_COMPARE_WITH_TIMEOUT(model.view().value("status").toString(), QStringLiteral("error"), 17000);
        QVERIFY(model.radioActive());
        QCOMPARE(model.radioState(), QStringLiteral("ready"));
    }
    void radioRefillsAndExhaustsWithoutRepeatingAudio() {
        ListeningSource source; QList<QVariantList> batches; QList<bool> appends;
        VinylHomeListening model(nullptr, [] {}, [&](const QVariantList &songs, bool append) { batches.append(songs); appends.append(append); });
        model.setInputs(&source, "profile", 1, seeds(), {}, "day"); model.startRadio(seed(0));
        complete(model, source.requests.last(), {{"items", QVariantList{candidate("radio-a", 0), candidate("radio-b", 0)}}});
        complete(model, source.requests.last(), album("radio-a"));
        QCOMPARE(batches.size(), 1); QVERIFY(!appends.first());
        model.refillRadio(); complete(model, source.requests.last(), album("radio-b"));
        QCOMPARE(batches.size(), 2); QVERIFY(appends.last());
        QSet<QString> unique; int total = 0;
        for (const auto &batch : batches) for (const auto &value : batch) { ++total; unique.insert(VinylHomeListening::trackKey(value.toMap().value("track").toMap())); }
        QCOMPARE(unique.size(), total);
        model.refillRadio(); QCOMPARE(model.radioState(), QStringLiteral("exhausted"));
        QCOMPARE(batches.size(), 2);
    }
    void providerFailureIsAnErrorRatherThanEmptyRadio() {
        ListeningSource source;
        VinylHomeListening model(nullptr, [] {}, [](const QVariantList &, bool) { QFAIL("failed provider produced playback"); });
        model.setInputs(&source, "profile", 1, seeds(), {}, "day"); model.startRadio(seed(0));
        complete(model, source.requests.last(), {}, "network failure");
        QCOMPARE(model.radioState(), QStringLiteral("error"));
        model.refillRadio();
        QCOMPARE(source.requests.size(), 2);
        QCOMPARE(source.requests.last().value, seed(0).value("providerId").toString());
        QCOMPARE(model.radioState(), QStringLiteral("loading"));
    }
    void unavailableArtistResponseCanRetryAndValidEmptyResponseExhausts() {
        ListeningSource source;
        VinylHomeListening model(nullptr, [] {}, [](const QVariantList &, bool) { QFAIL("unavailable provider produced playback"); });
        model.setInputs(&source, "profile", 1, seeds(), {}, "day");
        model.startRadio(seed(0));
        complete(model, source.requests.last(), {{"status", "unavailable"}, {"error", "offline"}, {"items", QVariantList()}});
        QCOMPARE(model.radioState(), QStringLiteral("error"));
        model.refillRadio();
        QCOMPARE(source.requests.size(), 2);
        complete(model, source.requests.last(), {{"status", "empty"}, {"complete", true}, {"items", QVariantList()}});
        QCOMPARE(model.radioState(), QStringLiteral("exhausted"));
    }
    void exclusionsApplyToEverySelectedArtistCardAndRadioStart() {
        ListeningSource source;
        VinylHomeListening model(nullptr, [] {}, [](const QVariantList &, bool) {});
        const auto excludedId = seed(0).value("providerId").toString();
        model.setInputs(&source, "profile", 1, seeds(), {}, "day", {excludedId});
        const auto cards = model.cards();
        QCOMPARE(cards.size(), 4);
        QSet<QString> actual;
        for (const auto &value : cards) {
            const auto card = value.toMap();
            QVERIFY(card.value("kind").toString() == QStringLiteral("radio"));
            const auto id = card.value("seedArtist").toMap().value("providerId").toString();
            QVERIFY(id != excludedId);
            actual.insert(id);
        }
        QSet<QString> expected;
        for (int i = 1; i < 5; ++i) expected.insert(seed(i).value("providerId").toString());
        QCOMPARE(actual, expected);
        model.startRadio(seed(0));
        QVERIFY(!model.radioActive());
        model.restoreRadio({{"kind", "artist-radio"}, {"seed", seed(0)}}, {});
        QVERIFY(!model.radioActive());
        QVERIFY(source.requests.isEmpty());
    }
    void cardsExposeEachValidSelectedArtistWithoutStartingWork() {
        ListeningSource source; int batches = 0;
        VinylHomeListening model(nullptr, [] {}, [&](const QVariantList &, bool) { ++batches; });
        auto selected = seeds();
        auto imaged = seed(2); imaged["image"] = QStringLiteral("https://example.test/artist-2.jpg");
        selected[2] = imaged;
        auto duplicate = seed(2); duplicate["name"] = QStringLiteral("Duplicate name");
        duplicate["image"] = QStringLiteral("https://example.test/duplicate.jpg");
        selected.append(duplicate);
        auto malformed = seed(9); malformed["providerId"] = QStringLiteral("not-a-channel");
        selected.append(malformed);
        auto local = seed(10); local["provider"] = QStringLiteral("local");
        selected.append(local);

        model.setInputs(&source, "profile", 1, selected, personalSections(), "day");
        for (int i = 0; i < source.requests.size(); ++i) {
            const auto request = source.requests[i];
            if (request.operation == "album") complete(model, request, album(request.value));
            else complete(model, request, {{"items", QVariantList{candidate("adjacent-album", 8)}}});
        }
        const int requestsBeforeEnumeration = source.requests.size();
        const auto cards = model.cards();
        QCOMPARE(source.requests.size(), requestsBeforeEnumeration);
        QCOMPARE(batches, 0);
        QVERIFY(!model.radioActive());

        QSet<QString> stationIds, stationSeeds, mixIds; int stationCount = 0;
        for (const auto &value : cards) {
            const auto card = value.toMap();
            if (card.value("kind").toString() == QStringLiteral("mixes")) {
                mixIds.insert(card.value("id").toString());
                continue;
            }
            QCOMPARE(card.value("kind").toString(), QStringLiteral("radio"));
            ++stationCount;
            const auto ref = card.value("seedArtist").toMap();
            const auto providerId = ref.value("providerId").toString();
            stationIds.insert(card.value("id").toString());
            stationSeeds.insert(providerId);
            if (providerId == seed(2).value("providerId").toString()) {
                QCOMPARE(card.value("id").toString(), QStringLiteral("home-radio:") + providerId);
                QCOMPARE(card.value("cover").toString(), imaged.value("image").toString());
                QCOMPARE(ref, imaged);
            }
        }
        QCOMPARE(stationCount, 5);
        QCOMPARE(stationIds.size(), 5);
        QCOMPARE(stationSeeds.size(), 5);
        for (int i = 0; i < 5; ++i) {
            const auto providerId = seed(i).value("providerId").toString();
            QVERIFY(stationIds.contains(QStringLiteral("home-radio:") + providerId));
            QVERIFY(stationSeeds.contains(providerId));
        }
        QVERIFY(!stationSeeds.contains(malformed.value("providerId").toString()));
        QVERIFY(!stationSeeds.contains(local.value("providerId").toString()));
        QSet<QString> expectedMixIds{QStringLiteral("home-mix:personal"), QStringLiteral("home-mix:discovery")};
        QCOMPARE(mixIds, expectedMixIds);
    }
    void excludedCollaboratorTracksAreSkippedWithoutChangingAlbumData() {
        ListeningSource source; QList<QVariantList> batches;
        VinylHomeListening model(nullptr, [] {}, [&](const QVariantList &songs, bool append) {
            Q_UNUSED(append); batches.append(songs);
        });
        const auto excluded = seed(2);
        model.setInputs(&source, "profile", 1, seeds(), {}, "day",
                        {excluded.value("providerId").toString()});
        model.startRadio(seed(0));
        complete(model, source.requests.last(), {{"items", QVariantList{candidate("radio-a", 0)}}});
        auto response = album("radio-a");
        const QVariantList tracks{
            QVariantMap{{"id", "collab-ref"}, {"title", "Excluded ref credit"},
                {"artistRefs", QVariantList{seed(1), excluded}}},
            QVariantMap{{"id", "collab-id"}, {"title", "Excluded id credit"},
                {"artistIds", QStringList{"ytm:artist:" + excluded.value("providerId").toString()}}},
            QVariantMap{{"id", "allowed-track"}, {"title", "Allowed"},
                {"artistRefs", QVariantList{seed(1)}}}};
        response["tracks"] = tracks;
        const auto original = response;
        complete(model, source.requests.last(), response);
        QCOMPARE(response, original);
        QCOMPARE(batches.size(), 1);
        const auto songs = batches.first();
        QCOMPARE(songs.size(), 1);
        QCOMPARE(songs.first().toMap().value("track").toMap().value("id").toString(), QStringLiteral("allowed-track"));
    }
    void radioRelatedArtistCandidatesRespectExclusions() {
        ListeningSource source;
        VinylHomeListening model(nullptr, [] {}, [](const QVariantList &, bool) {});
        const auto excluded = seed(1);
        model.setInputs(&source, "profile", 1, seeds(), {}, "day",
                        {excluded.value("providerId").toString()});
        model.startRadio(seed(0));
        auto allowed = seed(2); allowed["relation"] = "related-artist";
        auto rejected = excluded; rejected["relation"] = "related-artist";
        complete(model, source.requests.last(), {{"items", QVariantList{rejected, allowed}}});
        QCOMPARE(source.requests.size(), 2);
        QCOMPARE(source.requests.last().value, seed(2).value("providerId").toString());
    }
    void exclusionChangeClearsPreparedMixesAndCancelsRadioWork() {
        ListeningSource source; int batches = 0;
        VinylHomeListening model(nullptr, [] {}, [&](const QVariantList &, bool) { ++batches; });
        model.setInputs(&source, "profile", 1, seeds(), personalSections(), "day/none");
        for (int i = 0; i < source.requests.size(); ++i) {
            const auto request = source.requests[i];
            if (request.operation == "album") complete(model, request, album(request.value));
        }
        model.openMix("personal");
        QVERIFY(!model.view().value("songs").toList().isEmpty());
        model.startRadio(seed(0));
        const auto lateRadio = source.requests.last();
        const int requestCountBeforeExclusion = source.requests.size();
        model.setInputs(&source, "profile", 1, seeds(), personalSections(), "day/excluded",
                        {seed(0).value("providerId").toString()});
        QVERIFY(!model.radioActive());
        QVERIFY(model.view().value("songs").toList().isEmpty());
        QVERIFY(source.cancelled.contains(lateRadio.lane));
        for (int i = requestCountBeforeExclusion; i < source.requests.size(); ++i)
            QVERIFY(source.requests[i].value != QStringLiteral("album-a"));
        complete(model, lateRadio, {{"items", QVariantList{candidate("late-radio-album", 0)}}});
        QCOMPARE(batches, 0);
    }
    void malformedArtistDoesNotStartRadio() {
        ListeningSource source;
        VinylHomeListening model(nullptr, [] {}, [](const QVariantList &, bool) {});
        model.setInputs(&source,"profile",1,seeds(),{},"day");
        auto malformed=seed(0);malformed["providerId"]="not-a-channel";
        model.startRadio(malformed);QVERIFY(source.requests.isEmpty());QVERIFY(!model.radioActive());
        malformed["providerId"]="UC!!!!!!!!!!!!!!!!!!!!!!";
        model.startRadio(malformed);QVERIFY(source.requests.isEmpty());QVERIFY(!model.radioActive());
    }
    void restoredRadioWaitsForRefillAndPreservesQueueSemantics() {
        ListeningSource source; int batches = 0;
        VinylHomeListening model(nullptr, [] {}, [&](const QVariantList &, bool append) { QVERIFY(append); ++batches; });
        model.setInputs(&source, "profile", 1, seeds(), {}, "day");
        model.restoreRadio({{"kind", "artist-radio"}, {"seed", seed(0)}}, {"already-queued"});
        QVERIFY(model.radioActive()); QCOMPARE(batches, 0); QVERIFY(source.requests.isEmpty());
        model.refillRadio(); QCOMPARE(source.requests.size(), 1);
        complete(model, source.requests.last(), {{"items", QVariantList{candidate("radio-a", 0)}}});
        complete(model, source.requests.last(), album("radio-a")); QCOMPARE(batches, 1);
    }
};
QTEST_GUILESS_MAIN(HomeListeningTest)
#include "vinyl_home_listening_test.moc"
