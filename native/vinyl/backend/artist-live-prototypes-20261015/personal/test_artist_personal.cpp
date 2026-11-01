#include "ArtistPersonalState.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QVersionNumber>
#include <functional>
#include <stdexcept>

using namespace artist_personal;
namespace {
const QString A = "b10bbbfc-cf9e-42e0-be17-e2c3e1d2600d";
const QString B = "d8e9d9a7-51ea-4997-ae5d-093506dcf1b8";
const QString R = "7d44ce1e-7c9e-420b-8d5c-82d3955a19a1";
const QString R2 = "b89902d5-ff42-43c0-ab87-3c9ef1013148";

void need(bool condition, const char *reason) {
    if (!condition) throw std::runtime_error(reason);
}
void status(const Status &value, Code expected, const char *reason) {
    if (value.code != expected) {
        throw std::runtime_error(QString("%1: expected %2 got %3 (%4)")
            .arg(reason, codeName(expected), codeName(value.code), value.detail)
            .toStdString());
    }
}
QByteArray bytes(const QString &path) {
    QFile f(path);
    need(f.open(QIODevice::ReadOnly), "test fixture open for read");
    return f.readAll();
}
void writeBytes(const QString &path, const QByteArray &value) {
    QFile f(path);
    need(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
         "test fixture open for write");
    need(f.write(value) == value.size(), "test fixture write length");
    f.close();
    need(f.error() == QFileDevice::NoError, "test fixture write status");
}
QJsonObject obj(const QByteArray &value) {
    QJsonParseError error;
    auto d = QJsonDocument::fromJson(value, &error);
    need(error.error == QJsonParseError::NoError && d.isObject(), "valid test JSON");
    return d.object();
}
QByteArray json(const QJsonObject &value) {
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
struct Fixture {
    QString root;
    AtomicFilePort store;
    explicit Fixture(const QString &rootDir) : root(rootDir), store(rootDir) {}
    QString path(const QString &profile) const { return store.filePath(profile); }
};
void saveToggleRestart(const QString &root) {
    Fixture f(root);
    ArtistPersonalState a(f.store);
    status(a.open("profile-A"), Code::Ok, "open A");
    need(a.loadedVersion() == 0, "new empty profile");
    need(!QFile::exists(f.path("profile-A")), "read does not create empty file");
    status(a.saveArtist("", 200), Code::InvalidArtistMbid, "empty MBID");
    status(a.saveArtist("Eminem", 200), Code::InvalidArtistMbid, "title is not MBID");
    status(a.saveArtist("ytmusic:456", 200), Code::InvalidArtistMbid, "provider ID");
    status(a.unsaveArtist("bad"), Code::InvalidArtistMbid, "bad remove");
    status(a.saveArtist(A, 1000, vinyl::PersonalDisplaySnapshot{"Offline artist", ""}),
           Code::Ok, "save without catalogue");
    need(a.isSavedArtist(A), "saved from canonical MBID alone");
    auto original = a.findSavedArtist(A);
    need(original && original->savedAtMs() == 1000, "original savedAt");
    need(original->displaySnapshot().has_value()
            && original->displaySnapshot()->title == "Offline artist", "offline snapshot");
    auto repeat = a.saveArtist(A, 9000,
                    vinyl::PersonalDisplaySnapshot{"Different metadata", "wrong"});
    need(repeat.ok() && !repeat.changed, "idempotent save no-op");
    need(a.findSavedArtist(A)->savedAtMs() == 1000, "duplicate keeps savedAt");
    need(a.findSavedArtist(A)->displaySnapshot()->title == "Offline artist",
         "snapshot cannot override established save");
    status(a.saveArtist(B, 0), Code::InvalidTimestamp, "invalid savedAt");
    status(a.saveArtist(B, 200, vinyl::PersonalDisplaySnapshot{QString(300, 'x'), ""}),
           Code::InvalidSnapshot, "snapshot bounded");
    ArtistPersonalState second(f.store);
    status(second.open("profile-A"), Code::Ok, "restart");
    need(second.loadedVersion() == 3, "BA5 schema 3");
    need(second.findSavedArtist(A)->displaySnapshot()->title == "Offline artist",
         "offline display survives restart");
    status(second.toggleArtist(A, 5000), Code::Ok, "toggle unsave");
    need(!second.isSavedArtist(A), "toggle removed");
    status(second.toggleArtist(A, 6000), Code::Ok, "toggle save");
    need(second.findSavedArtist(A)->savedAtMs() == 6000, "new save timestamp");
    status(second.unsaveArtist(A), Code::Ok, "explicit unsave");
    status(second.unsaveArtist(A), Code::MissingItem, "remove missing fails");
    ArtistPersonalState third(f.store);
    status(third.open("profile-A"), Code::Ok, "second restart");
    need(!third.isSavedArtist(A), "unsave durable");
}
void playlistOrderAndMutations(const QString &root) {
    Fixture f(root);
    ArtistPersonalState s(f.store);
    status(s.open("playlists"), Code::Ok, "open playlists");
    status(s.createPlaylist("z-older", "Morning", 100), Code::Ok, "first playlist");
    status(s.createPlaylist("a-newer", "Evening", 200), Code::Ok, "second playlist");
    status(s.createPlaylist("z-older", "Clash", 300),
           Code::DuplicatePlaylist, "duplicate playlist ID");
    status(s.createPlaylist("bad id", " ", 300), Code::InvalidName, "blank name");

    status(s.createPlaylist("", "No", 300), Code::InvalidPlaylistId, "blank ID");
    status(s.renamePlaylist("z-older", "Morning"), Code::Ok, "rename no-op");
    auto names = s.playlistsByCreation();
    need(names.size() == 2 && names[0].name() == "Morning"
         && names[1].name() == "Evening", "creation order independent of BA5 serialized ID sort");
    status(s.addRecording("z-older", "one", R, 110,
             -1, vinyl::PersonalDisplaySnapshot{"Song offline", "Artist offline"}),
           Code::Ok, "append one");
    status(s.addRecording("z-older", "two", R, 120), Code::Ok, "duplicate recording allowed");
    status(s.addRecording("z-older", "three", R2, 130, 1), Code::Ok, "insert middle");
    auto pl = s.findPlaylist("z-older");
    need(pl && pl->size() == 3 &&
         pl->items()[0].itemId().value() == "one" &&
         pl->items()[1].itemId().value() == "three" &&
         pl->items()[2].itemId().value() == "two", "ordered insert");
    status(s.addRecording("z-older", "one", R2, 140),
           Code::DuplicateItem, "duplicate occurrence ID rejected");
    status(s.addRecording("z-older", "provider", "youtube:track", 140),
           Code::InvalidRecordingMbid, "provider item not canonical");
    status(s.addRecording("z-older", "bad", R2, 140, 99),
           Code::IndexOutOfRange, "insert bounds");
    status(s.moveRecording("z-older", "one", 2), Code::Ok, "move item");
    status(s.moveRecording("z-older", "one", -1),
           Code::IndexOutOfRange, "negative move");
    pl = s.findPlaylist("z-older");
    need(pl->items()[0].itemId().value() == "three" &&
         pl->items()[2].itemId().value() == "one", "move exact occurrence");
    status(s.removeRecording("z-older", "two"), Code::Ok, "remove duplicate occurrence");
    need(s.findPlaylist("z-older")->size() == 2 &&
         s.findPlaylist("z-older")->items()[1].recordingMbid().toString() == R,
         "one recording duplicate remains");
    status(s.removeRecording("z-older", "two"), Code::MissingItem, "missing occurrence");
    status(s.renamePlaylist("z-older", "AM favorites"), Code::Ok, "rename");
    ArtistPersonalState again(f.store);
    status(again.open("playlists"), Code::Ok, "playlist restart");
    pl = again.findPlaylist("z-older");
    need(pl && pl->name() == "AM favorites" && pl->size() == 2 &&
         pl->items()[1].itemId().value() == "one", "item order and IDs persist");
    need(pl->items()[1].displaySnapshot().has_value() &&
         pl->items()[1].displaySnapshot()->title == "Song offline",
         "non-authoritative item snapshot persists");
    need(again.playlistsByCreation()[0].playlistId().value() == "z-older",
         "name collection chronological ordering survives restart");
    status(again.deletePlaylist("a-newer"), Code::Ok, "delete playlist");
    status(again.deletePlaylist("a-newer"), Code::MissingPlaylist, "delete missing");
    need(again.playlistsByCreation().size() == 1, "delete order");
}
void profilesAndConcurrentChange(const QString &root) {
    Fixture f(root);
    ArtistPersonalState one(f.store), other(f.store);
    status(one.open("p1"), Code::Ok, "first scope");
    status(other.open("p2"), Code::Ok, "second scope");
    need(f.path("p1") != f.path("p2"), "stable partition path");
    need(f.path("x/y") != f.path("x\\y"), "hashed separators and case preserved");
    status(one.saveArtist(A, 100), Code::Ok, "p1 save");
    status(other.saveArtist(A, 200), Code::Ok, "p2 independent save");
    status(one.unsaveArtist(A), Code::Ok, "p1 unsave");
    need(other.isSavedArtist(A), "p2 unaffected");
    ArtistPersonalState otherAgain(f.store);
    status(otherAgain.open("p2"), Code::Ok, "p2 restart");
    need(otherAgain.findSavedArtist(A)->savedAtMs() == 200, "scope durable independence");
    status(one.open(" "), Code::InvalidProfile, "invalid profile scope");
    status(one.saveArtist(A, 300), Code::NotOpen, "failed open clears stale data");

    ArtistPersonalState slow(f.store), fast(f.store);
    status(slow.open("compete"), Code::Ok, "writer 1");
    status(fast.open("compete"), Code::Ok, "writer 2");
    status(fast.saveArtist(A, 300), Code::Ok, "winning writer");
    status(slow.saveArtist(B, 400), Code::ConcurrentChange, "stale writer prevented");
    need(!slow.isSavedArtist(B), "failed commit has no in-memory mutation");
    status(slow.open("compete"), Code::Ok, "refresh stale writer");
    status(slow.saveArtist(B, 400), Code::Ok, "now succeeds");
}
void corruptionAndMigration(const QString &root) {
    Fixture f(root);
    ArtistPersonalState s(f.store);
    status(s.open("damaged"), Code::Ok, "corruption seed");
    status(s.saveArtist(A, 101), Code::Ok, "valid seed");
    auto p = f.path("damaged");
    QByteArray healthy = bytes(p);
    auto rejectOpen = [&](QByteArray bad, Code expected, const char *reason) {
        writeBytes(p, bad);
        ArtistPersonalState suspect(f.store);
        status(suspect.open("damaged"), expected, reason);
        need(!suspect.ready(), "failed load stays closed");
        status(suspect.saveArtist(B, 102), Code::NotOpen, "no overwrite after corruption");
        need(bytes(p) == bad, "failed load preserved on-disk bytes");
    };
    rejectOpen(healthy.left(healthy.size()/2), Code::Corrupt, "truncated JSON");
    auto future = obj(healthy); future["version"] = 999;
    rejectOpen(json(future), Code::UnsupportedVersion, "future schema blocked");
    auto mismatch = obj(healthy); mismatch["profileScopeId"] = "wrong-profile";
    rejectOpen(json(mismatch), Code::ProfileMismatch, "profile mismatch blocked");
    auto rogue = obj(healthy); rogue["unknownField"] = "incompatible";
    rejectOpen(json(rogue), Code::Corrupt, "unknown root field blocked");
    writeBytes(p, healthy + "\n");
    auto sample = bytes(p);
    status(s.saveArtist(B, 102), Code::ConcurrentChange,
           "externally restored bytes differ from stale baseline");
    need(bytes(p) == sample, "CAS protects out-of-band data change");
    for (int version : {1, 2}) {
        const QString name = QString("legacy-%1").arg(version);
        QJsonObject item{{"kind", "artist"}, {"mbid", A}, {"savedAtMs", 500}};
        QJsonObject legacy{
            {"schema", vinyl::VinylPersonalStatePersistence::schemaName()},
            {"version", version}, {"profileScopeId", name},
            {"library", QJsonArray{item}}, {"playlists", QJsonArray{}}};
        writeBytes(f.path(name), json(legacy));
        ArtistPersonalState converted(f.store);
        status(converted.open(name), Code::Ok, "legacy load");
        need(converted.loadedVersion() == version && converted.isSavedArtist(A),
             "supported legacy identity");
        need(!converted.findSavedArtist(A)->displaySnapshot().has_value(),
             "no invented old display snapshot");
        status(converted.saveArtist(B, 501), Code::Ok, "mutate legacy");
        need(obj(bytes(f.path(name)))["version"].toInt() == 3,
             "successful next write migrates to BA5 v3");
    }
}
class RejectWrites final : public PersonalBytesPort {
public:
    Load read(const QString &) override { return {Status::success(), {false,{}}}; }
    Status commit(const QString &, const BytesSnapshot &, const QByteArray &) override {
        return Status::failure(Code::IoError, "simulated storage failure");
    }
};
void writeFailureAndOffline(const QString &root) {
    AtomicFilePort blank{QString()};
    ArtistPersonalState invalidRoot(blank);
    status(invalidRoot.open("any-profile"), Code::InvalidState,
           "implicit current-directory root prohibited");
    RejectWrites blocked;
    ArtistPersonalState fail(blocked);
    status(fail.open("denied"), Code::Ok, "failed writer setup");
    status(fail.saveArtist(A, 12), Code::IoError, "simulated write failure");
    need(!fail.isSavedArtist(A), "commit failure does not mutate memory");

    Fixture f(root);
    ArtistPersonalState offline(f.store);
    status(offline.open("offline"), Code::Ok, "offline open");
    status(offline.saveArtist(A, 1000), Code::Ok,
           "catalogue entirely unavailable, canonical save valid");
    status(offline.createPlaylist("pl-offline", "Manual offline", 1001), Code::Ok,
           "offline playlist");
    status(offline.addRecording("pl-offline", "item1", R, 1002), Code::Ok,
           "offline recording with no catalogue snapshot");
    need(offline.isSavedArtist(A), "canonical saved without metadata");
    need(!offline.findSavedArtist(A)->displaySnapshot().has_value(),
         "no fabricated display identity");
    ArtistPersonalState restored(f.store);
    status(restored.open("offline"), Code::Ok, "offline restart");
    need(restored.isSavedArtist(A) && restored.findPlaylist("pl-offline")->size() == 1,
         "offline state persisted");
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream output(stdout);
    const QString root = QDir(QCoreApplication::applicationDirPath())
        .filePath("fx");
    QDir cleanup(root);
    if (cleanup.exists() && !cleanup.removeRecursively()) {
        output << "FAIL fixture clean under binary-owned path\n";
        return 2;
    }
    if (!QDir().mkpath(root)) {
        output << "FAIL fixture create\n";
        return 2;
    }
    int failed = 0, passed = 0;
    const auto group = [&](const char *name,
                           const std::function<void(const QString &)> &fn) {
        try {
            const QString scoped = QDir(root).filePath(name);
            need(QDir().mkpath(scoped), "create suite fixture");
            fn(scoped);
            ++passed; output << "PASS " << name << '\n';
        } catch (const std::exception &e) {
            ++failed; output << "FAIL " << name << " : " << e.what() << '\n';
        }
        output.flush();
    };
    group("save_toggle_restart", saveToggleRestart);
    group("playlist_order_mutations", playlistOrderAndMutations);
    group("profile_partition_CAS", profilesAndConcurrentChange);
    group("corruption_migration", corruptionAndMigration);
    group("write_failure_catalogue_offline", writeFailureAndOffline);
    output << "RESULT " << passed << " PASS, " << failed << " FAIL"
           << " / Qt " << qVersion() << '\n';
    output.flush();
    return failed ? 1 : 0;
}
