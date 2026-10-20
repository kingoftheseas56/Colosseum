#include "ArtistServiceBridge.h"

#include <QSignalSpy>
#include <QSet>
#include <QTest>
#include <functional>
#include <utility>

using namespace vinyl::artist;
namespace {
const QString A = QStringLiteral("11111111-1111-4111-8111-111111111111");
const QString B = QStringLiteral("22222222-2222-4222-8222-222222222222");
const QString GROUP = QStringLiteral("33333333-3333-4333-8333-333333333333");
const QString GROUP2 = QStringLiteral("66666666-6666-4666-8666-666666666666");
const QString RELEASE = QStringLiteral("44444444-4444-4444-8444-444444444444");
const QString RECORDING = QStringLiteral("55555555-5555-4555-8555-555555555555");
const QString PROFILE1 = QStringLiteral("profile-one");
const QString PROFILE2 = QStringLiteral("profile-two");

struct ReadFake final : IArtistReadPort {
    QVector<ReadRequest> issued;
    QHash<quint64, std::function<void(ReadReply)>> handlers;
    QSet<quint64> canceled;
    void request(const ReadRequest &r, std::function<void(ReadReply)> callback) override {
        issued.append(r);
        handlers.insert(r.token, std::move(callback));
    }
    void cancel(quint64 token) override { canceled.insert(token); }
    ReadRequest last() const { return issued.last(); }
    void deliver(quint64 token, ReadReply reply) {
        QVERIFY2(handlers.contains(token), "Unknown test request token");
        auto callback = handlers.take(token);
        callback(std::move(reply)); // deliberately delivers even if canceled
    }
};
struct SavedFake final : ISavedArtistPort {
    QHash<QString, QSet<QString>> memberships;
    bool offline = false;
    int reads = 0, writes = 0;
    std::function<void()> beforeReply;
    SavedReply build(const QString &profile, const QString &artist, bool saved) {
        SavedReply reply;
        reply.profileScopeId = profile;
        reply.artistMbid = artist;
        reply.state = offline ? State::Unavailable : State::Ready;
        reply.errorCode = offline ? QStringLiteral("unavailable") : QString();
        reply.saved = saved;
        if (beforeReply) {
            auto callback = std::exchange(beforeReply, {});
            callback(); // simulate synchronous profile change while store call is in flight
        }
        return reply;
    }
    SavedReply read(const QString &profile, const QString &artist) override {
        ++reads;
        return build(profile, artist, memberships.value(profile).contains(artist));
    }
    SavedReply write(const QString &profile, const QString &artist, bool saved) override {
        ++writes;
        if (!offline) {
            if (saved) memberships[profile].insert(artist);
            else memberships[profile].remove(artist);
        }
        return build(profile, artist, memberships.value(profile).contains(artist));
    }
};
struct PlaybackFake final : IPlaybackCommandPort {
    QString playing = QStringLiteral("original-playing-recording");
    int selectRecordingCalls = 0, selectReleaseCalls = 0, stops = 0;
    ExactSelection last;
    bool rejectNext = false;
    CommandReply selectRecording(const ExactSelection &selection) override {
        ++selectRecordingCalls; last = selection;
        if (rejectNext) { rejectNext = false; return {false, QStringLiteral("source_unavailable"),
                                                           QStringLiteral("Exact source unavailable. Retry.")}; }
        playing = selection.recording.value;
        return {true, {}, {}};
    }
    CommandReply selectRelease(const ExactSelection &selection) override {
        ++selectReleaseCalls; last = selection;
        return {true, {}, {}}; // opens an exact edition, never stops the song
    }
};
ReadReply response(const ReadRequest &req, State state = State::Ready) {
    ReadReply r;
    r.channel = req.channel;
    r.token = req.token;
    r.profileScopeId = req.profileScopeId;
    r.artistMbid = req.artistMbid;
    r.cursor = req.cursor;
    r.filter = req.filter;
    r.state = state;
    r.source = {QStringLiteral("musicbrainz"), QStringLiteral("corpus-1"),
                QStringLiteral("revision-1")};
    return r;
}
void finishArtist(ReadFake &port, const QString &name = QStringLiteral("Artist A")) {
    auto req = port.last();
    auto r = response(req);
    r.entity = {{"artistMbid", req.artistMbid}, {"name", name}};
    port.deliver(req.token, r);
}
void finishDiscography(ReadFake &port, const QString &group = GROUP,
                       const QString &nextCursor = {}) {
    auto req = port.last();
    auto r = response(req);
    r.items = {QVariantMap{{"releaseGroupMbid", group}, {"title", QStringLiteral("Album")}}};
    r.nextCursor = nextCursor;
    port.deliver(req.token, r);
}
void prepareReady(ArtistServiceBridge &bridge, ReadFake &port) {
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QVERIFY(bridge.requestArtist(A));
    finishArtist(port);
    QVERIFY(bridge.requestDiscography({}, QStringLiteral("all")));
    finishDiscography(port);
    QCOMPARE(bridge.artistState(), State::Ready);
    QCOMPARE(bridge.discographyState(), State::Ready);
}
} // namespace

class ArtistBridgeTests : public QObject {
    Q_OBJECT
private slots:
    void continuationKeepsSelections();
    void emptyContinuationChecksRevision();
    void metaObjectAndTypedResults();
    void cancelAndLateArtistReplies();
    void leaveAndReturnFencesLateResponses();
    void profileSavedIndependenceAndReentrancy();
    void browsingDoesNotStopPlayback();
    void strongExactSelectionAndClearRetryError();
    void emptyOfflineNotFoundAndRetry();
    void cursorFilterAndSourceRevisionFence();
    void mismatchedArtistAndPayloadFailClosed();
    void profileSwitchRejectsLateReadAndForeignProfileReply();
    void duplicateReleaseGroupAndMissingSourceFailClosed();
    void concurrentLanesDoNotCancelEachOther();
};
void ArtistBridgeTests::metaObjectAndTypedResults() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    QVERIFY(bridge.metaObject()->indexOfProperty("artistState") >= 0);
    QVERIFY(bridge.metaObject()->indexOfProperty("savedState") >= 0);
    QVERIFY(bridge.metaObject()->indexOfMethod("requestMedia()") >= 0);
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QCOMPARE(bridge.result(Channel::Artwork).value("state").toString(), QStringLiteral("Idle"));
    QVERIFY(bridge.requestArtwork());
    QCOMPARE(bridge.artworkState(), State::Loading);
    auto req = rd.last();
    auto reply = response(req);
    reply.entity = {{"artistMbid", A}, {"images", QStringList{"wide-1"}}};
    rd.deliver(req.token, reply);
    QCOMPARE(bridge.artworkState(), State::Ready);
    QCOMPARE(bridge.result(Channel::Artwork).value("source").toMap().value("revision").toString(),
             QStringLiteral("revision-1"));
}
void ArtistBridgeTests::cancelAndLateArtistReplies() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QVERIFY(bridge.requestArtist(A));
    const auto old = rd.last();
    QVERIFY(bridge.enterArtist(PROFILE1, B));
    QVERIFY(rd.canceled.contains(old.token));
    QVERIFY(bridge.requestArtist(B));
    const auto current = rd.last();
    auto stale = response(old); stale.entity = {{"artistMbid", A}, {"name", "Old"}};
    rd.deliver(old.token, stale);
    QCOMPARE(bridge.artistState(), State::Loading);
    QCOMPARE(bridge.artistMbid(), B);
    finishArtist(rd, QStringLiteral("New"));
    QCOMPARE(bridge.artistState(), State::Ready);
    QCOMPARE(bridge.artist().value("name").toString(), QStringLiteral("New"));
    Q_UNUSED(current);
}
void ArtistBridgeTests::leaveAndReturnFencesLateResponses() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QVERIFY(bridge.requestMedia());
    const auto stale = rd.last();
    bridge.leaveArtist();
    QVERIFY(rd.canceled.contains(stale.token));
    QCOMPARE(bridge.mediaState(), State::Idle);
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QVERIFY(bridge.requestMedia());
    const auto fresh = rd.last();
    auto previous = response(stale);
    previous.entity = {{"artistMbid", A}, {"videos", "outdated"}};
    rd.deliver(stale.token, previous);
    QCOMPARE(bridge.mediaState(), State::Loading);
    auto now = response(fresh);
    now.entity = {{"artistMbid", A}, {"videos", "new"}};
    rd.deliver(fresh.token, now);
    QCOMPARE(bridge.mediaState(), State::Ready);
    QCOMPARE(bridge.media().value("videos").toString(), QStringLiteral("new"));
}
void ArtistBridgeTests::profileSavedIndependenceAndReentrancy() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QCOMPARE(bridge.savedState(), State::Ready);
    QVERIFY(!bridge.saved());
    QVERIFY(bridge.toggleSavedArtist());
    QVERIFY(bridge.saved());
    QVERIFY(saved.memberships[PROFILE1].contains(A));
    QVERIFY(bridge.setProfile(PROFILE2));
    QCOMPARE(bridge.savedState(), State::Idle);
    QVERIFY(!bridge.saved());
    QVERIFY(!bridge.toggleSavedArtist()); // no current artist
    QVERIFY(bridge.enterArtist(PROFILE2, A));
    QVERIFY(!bridge.saved());
    QVERIFY(bridge.toggleSavedArtist());
    QVERIFY(saved.memberships[PROFILE2].contains(A));
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QVERIFY(bridge.saved());
    saved.beforeReply = [&bridge] { QVERIFY(bridge.setProfile(PROFILE2)); };
    QVERIFY(!bridge.refreshSavedArtist()); // old reply rejected after reentrant switch
    QCOMPARE(bridge.savedState(), State::Idle);
    QVERIFY(!bridge.saved());
    QCOMPARE(bridge.profileScopeId(), PROFILE2);
}
void ArtistBridgeTests::browsingDoesNotStopPlayback() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QVERIFY(bridge.requestArtist(A)); finishArtist(rd);
    QVERIFY(bridge.requestDiscography()); finishDiscography(rd);
    QVERIFY(bridge.requestMedia());
    QCOMPARE(playback.playing, QStringLiteral("original-playing-recording"));
    QCOMPARE(playback.stops, 0);
    bridge.leaveArtist();
    QVERIFY(bridge.enterArtist(PROFILE1, B));
    QVERIFY(bridge.requestArtwork());
    QCOMPARE(playback.playing, QStringLiteral("original-playing-recording"));
    QCOMPARE(playback.selectRecordingCalls, 0);
    QCOMPARE(playback.stops, 0);
}
void ArtistBridgeTests::strongExactSelectionAndClearRetryError() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    prepareReady(bridge, rd);
    QVERIFY(!bridge.selectRecording(B, GROUP, RELEASE, RECORDING));
    QVERIFY(!bridge.selectRecording(A, GROUP, RELEASE, QStringLiteral("yt-video-id")));
    QVERIFY(!bridge.selectRecording(A, GROUP2, RELEASE, RECORDING));
    QVERIFY(!bridge.selectRecording(A, GROUP, QStringLiteral("bad-release"), RECORDING));
    QCOMPARE(playback.selectRecordingCalls, 0);
    QCOMPARE(playback.playing, QStringLiteral("original-playing-recording"));
    QVERIFY(bridge.selectRelease(A, GROUP, RELEASE));
    QCOMPARE(playback.selectReleaseCalls, 1);
    QCOMPARE(playback.playing, QStringLiteral("original-playing-recording"));
    QCOMPARE(playback.last.profile.value, PROFILE1);
    QCOMPARE(playback.last.group.value, GROUP);
    QCOMPARE(playback.last.release.value, RELEASE);
    playback.rejectNext = true;
    QVERIFY(!bridge.selectRecording(A, GROUP, RELEASE, RECORDING));
    QCOMPARE(bridge.playbackError(), QStringLiteral("Exact source unavailable. Retry."));
    QCOMPARE(playback.playing, QStringLiteral("original-playing-recording"));
    QVERIFY(bridge.selectRecording(A, GROUP, RELEASE, RECORDING));
    QCOMPARE(bridge.playbackError(), QString());
    QCOMPARE(playback.last.recording.value, RECORDING);
    QCOMPARE(playback.playing, RECORDING);
}
void ArtistBridgeTests::emptyOfflineNotFoundAndRetry() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    saved.offline = true;
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QCOMPARE(bridge.savedState(), State::Unavailable);
    QVERIFY(!bridge.toggleSavedArtist());
    QCOMPARE(saved.writes, 0);
    QVERIFY(bridge.requestMedia());
    auto r = response(rd.last(), State::Empty);
    rd.deliver(r.token, r);
    QCOMPARE(bridge.mediaState(), State::Empty);
    QVERIFY(bridge.requestArtwork());
    auto offline = response(rd.last(), State::Unavailable);
    offline.errorCode = QStringLiteral("unavailable");
    rd.deliver(offline.token, offline);
    QCOMPARE(bridge.artworkState(), State::Unavailable);
    QVERIFY(bridge.retry(Channel::Artwork));
    auto recover = response(rd.last());
    recover.entity = {{"artistMbid", A}, {"images", QStringList{"one"}}};
    rd.deliver(recover.token, recover);
    QCOMPARE(bridge.artworkState(), State::Ready);
    QVERIFY(bridge.requestArtist(A));
    auto missing = response(rd.last(), State::NotFound);
    rd.deliver(missing.token, missing);
    QCOMPARE(bridge.artistState(), State::NotFound);
    QVERIFY(bridge.retry(Channel::Artist));
    finishArtist(rd);
    QCOMPARE(bridge.artistState(), State::Ready);
}
void ArtistBridgeTests::cursorFilterAndSourceRevisionFence() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QVERIFY(bridge.requestDiscography({}, QStringLiteral("album")));
    finishDiscography(rd, GROUP, QStringLiteral("opaque-cursor-1"));
    QCOMPARE(bridge.nextCursor(), QStringLiteral("opaque-cursor-1"));
    QVERIFY(!bridge.requestDiscography(QStringLiteral("invented"), QStringLiteral("album")));
    QVERIFY(!bridge.requestDiscography(QStringLiteral("opaque-cursor-1"), QStringLiteral("single")));
    QCOMPARE(bridge.discographyState(), State::Ready);
    QVERIFY(bridge.requestDiscography(QStringLiteral("opaque-cursor-1"), QStringLiteral("album")));
    auto staleCorpus = response(rd.last());
    staleCorpus.source.revision = QStringLiteral("revision-2");
    staleCorpus.items = {QVariantMap{{"releaseGroupMbid", GROUP2}}};
    rd.deliver(staleCorpus.token, staleCorpus);
    QCOMPARE(bridge.result(Channel::Discography).value("errorCode").toString(),
             QStringLiteral("invalid_page"));
    QCOMPARE(bridge.discography().size(), 1);
    QVERIFY(bridge.retry(Channel::Discography));
    finishDiscography(rd, GROUP2);
    QCOMPARE(bridge.discography().size(), 2);
    QCOMPARE(bridge.discographyState(), State::Ready);
    QVERIFY(bridge.nextCursor().isEmpty());
    QVERIFY(bridge.requestDiscography({}, QStringLiteral("single")));
    QCOMPARE(bridge.discography().size(), 0);
    QCOMPARE(bridge.discographyFilter(), QStringLiteral("single"));
}
void ArtistBridgeTests::mismatchedArtistAndPayloadFailClosed() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    QSignalSpy rejected(&bridge, &ArtistServiceBridge::rejected);
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QVERIFY(!bridge.requestArtist(B));
    QCOMPARE(rejected.size(), 1);
    QVERIFY(bridge.requestArtist(A));
    auto mismatch = response(rd.last());
    mismatch.artistMbid = B;
    mismatch.entity = {{"artistMbid", B}, {"name", "Another"}};
    rd.deliver(mismatch.token, mismatch);
    QCOMPARE(bridge.artistState(), State::Error);
    QCOMPARE(bridge.result(Channel::Artist).value("errorCode").toString(),
             QStringLiteral("identity_mismatch"));
    QVERIFY(bridge.retry(Channel::Artist));
    finishArtist(rd);
    QCOMPARE(bridge.artistState(), State::Ready);
    QVERIFY(bridge.requestDiscography()); // malformed release-group item
    auto invalid = response(rd.last());
    invalid.items = {QVariantMap{{"releaseMbid", RELEASE}}};
    rd.deliver(invalid.token, invalid);
    QCOMPARE(bridge.discographyState(), State::Error);
    QCOMPARE(bridge.result(Channel::Discography).value("errorCode").toString(),
             QStringLiteral("invalid_payload"));
}
void ArtistBridgeTests::profileSwitchRejectsLateReadAndForeignProfileReply() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QVERIFY(bridge.requestMedia());
    const auto old = rd.last();
    QVERIFY(bridge.setProfile(PROFILE2));
    QVERIFY(rd.canceled.contains(old.token));
    QVERIFY(bridge.enterArtist(PROFILE2, A));
    QVERIFY(bridge.requestMedia());
    const auto current = rd.last();
    auto late = response(old);
    late.entity = {{"artistMbid", A}, {"videos", "wrong-profile"}};
    rd.deliver(old.token, late);
    QCOMPARE(bridge.mediaState(), State::Loading);
    auto foreign = response(current);
    foreign.profileScopeId = PROFILE1; // right token, wrong profile is not trusted
    foreign.entity = {{"artistMbid", A}, {"videos", "foreign"}};
    rd.deliver(current.token, foreign);
    QCOMPARE(bridge.mediaState(), State::Error);
    QCOMPARE(bridge.result(Channel::Media).value("errorCode").toString(),
             QStringLiteral("identity_mismatch"));
    QVERIFY(bridge.retry(Channel::Media));
    auto valid = response(rd.last());
    valid.entity = {{"artistMbid", A}, {"videos", "current"}};
    rd.deliver(valid.token, valid);
    QCOMPARE(bridge.mediaState(), State::Ready);
    QCOMPARE(bridge.media().value("videos").toString(), QStringLiteral("current"));
    QCOMPARE(bridge.profileScopeId(), PROFILE2);
}
void ArtistBridgeTests::duplicateReleaseGroupAndMissingSourceFailClosed() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QVERIFY(bridge.requestArtist(A));
    auto withoutSource = response(rd.last());
    withoutSource.entity = {{"artistMbid", A}, {"name", "No source"}};
    withoutSource.source = {};
    rd.deliver(withoutSource.token, withoutSource);
    QCOMPARE(bridge.result(Channel::Artist).value("errorCode").toString(),
             QStringLiteral("missing_source"));
    QVERIFY(bridge.retry(Channel::Artist));
    finishArtist(rd);
    QVERIFY(bridge.requestDiscography());
    auto dup = response(rd.last());
    dup.items = {QVariantMap{{"releaseGroupMbid", GROUP}},
                 QVariantMap{{"releaseGroupMbid", GROUP}}};
    rd.deliver(dup.token, dup);
    QCOMPARE(bridge.discographyState(), State::Error);
    QCOMPARE(bridge.result(Channel::Discography).value("errorCode").toString(),
             QStringLiteral("invalid_payload"));
    QCOMPARE(bridge.discography().size(), 0);
}
void ArtistBridgeTests::concurrentLanesDoNotCancelEachOther() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    QVERIFY(bridge.enterArtist(PROFILE1, A));
    QVERIFY(bridge.requestArtist(A)); const auto artist = rd.last();
    QVERIFY(bridge.requestMedia()); const auto media = rd.last();
    QVERIFY(bridge.requestArtwork()); const auto artwork = rd.last();
    QVERIFY(bridge.requestMedia()); const auto media2 = rd.last();
    QVERIFY(rd.canceled.contains(media.token));
    QVERIFY(!rd.canceled.contains(artist.token));
    QVERIFY(!rd.canceled.contains(artwork.token));
    auto late = response(media); late.entity = {{"artistMbid", A}, {"videos", "stale"}};
    rd.deliver(media.token, late);
    QCOMPARE(bridge.mediaState(), State::Loading);
    auto ok = response(media2); ok.entity = {{"artistMbid", A}, {"videos", "fresh"}};
    rd.deliver(media2.token, ok);
    QCOMPARE(bridge.media().value("videos").toString(), QStringLiteral("fresh"));
    auto art = response(artist); art.entity = {{"artistMbid", A}, {"name", "Artist"}};
    rd.deliver(artist.token, art);
    QCOMPARE(bridge.artistState(), State::Ready);
}
void ArtistBridgeTests::continuationKeepsSelections() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    prepareReady(bridge, rd);
    QVERIFY(bridge.requestDiscography({}, QStringLiteral("all")));
    finishDiscography(rd, GROUP, QStringLiteral("next-page"));
    QVERIFY(bridge.requestDiscography(QStringLiteral("next-page"), QStringLiteral("all")));
    QVERIFY2(bridge.selectRecording(A, GROUP, RELEASE, RECORDING),
             "already loaded selections must remain available during continuation");
}
void ArtistBridgeTests::emptyContinuationChecksRevision() {
    ReadFake rd; SavedFake saved; PlaybackFake playback;
    ArtistServiceBridge bridge(rd, saved, playback);
    prepareReady(bridge, rd);
    QVERIFY(bridge.requestDiscography({}, QStringLiteral("all")));
    finishDiscography(rd, GROUP, QStringLiteral("next-page"));
    QVERIFY(bridge.requestDiscography(QStringLiteral("next-page"), QStringLiteral("all")));
    auto wrongRevision = response(rd.last(), State::Empty);
    wrongRevision.source.revision = QStringLiteral("other-revision");
    rd.deliver(wrongRevision.token, wrongRevision);
    QCOMPARE(bridge.discographyState(), State::Error);
    QCOMPARE(bridge.result(Channel::Discography).value("errorCode").toString(),
             QStringLiteral("invalid_page"));
}
QTEST_GUILESS_MAIN(ArtistBridgeTests)
#include "test_artist_service_bridge.moc"
