#include "NamedCollectionsStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QLockFile>
#include <QUuid>
#include <QDebug>

namespace {
int checks = 0;
void check(bool ok, const char *message) { ++checks; if (!ok) qFatal("%s", message); }
QByteArray bytes(const QString &path) { QFile file(path); check(file.open(QIODevice::ReadOnly), "read saved file"); return file.readAll(); }
QVariantMap ref(const QString &source = "mal", const QString &id = "mal:13") {
    return {{"key", "local-key-" + id}, {"world", "tankoban"}, {"kind", "manga"},
        {"source", source}, {"sourceId", id}, {"editionId", "color"},
        {"title", "One Piece"}, {"creator", "Eiichiro Oda"}, {"year", "1997"},
        {"cover", "file:///private/cover.png"}, {"route", "manga-detail"},
        {"payload", QVariantMap{{"credential", "secret"}, {"path", "/private/book"}}}};
}
QVariantMap presentation() {
    return {{"preset", "sea"}, {"dedication", "For you <script>"}, {"signature", "From me"},
        {"cover_data", "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGMQNA4FAADyAJqKCOUZAAAAAElFTkSuQmCC"}};
}
QString id(const QVariantMap &result) { check(result.value("ok").toBool(), qPrintable(result.value("error").toString())); return result.value("collectionId").toString(); }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString ownerPath = dir.filePath("owner.json"), recipientPath = dir.filePath("recipient.json");
    NamedCollectionsStore owner(ownerPath), recipient(recipientPath);
    const QString collection = owner.createCollection("A gift", ref(), "A personal description");
    check(!collection.isEmpty(), "owner create");
    auto draft = owner.beginEdit(collection);
    check(owner.saveEdit(draft.value("token").toString(), "A gift", "A personal description",
        draft.value("items").toStringList(), {{"local-key-mal:13", "A personal note"}}), "save note");
    check(owner.savePresentation(collection, presentation(), owner.revision()), "save bounded image presentation");
    const QVariantMap snapshot = owner.shareSnapshot(collection);
    check(NamedCollectionsStore::validateShareSnapshot(snapshot), "valid projection");
    const QByteArray projected = QJsonDocument::fromVariant(snapshot).toJson();
    check(!projected.contains("private") && !projected.contains("credential") && !projected.contains("payload")
        && !projected.contains("local-key") && !projected.contains("route") && !projected.contains("file:"), "projection excludes reopen data");
    const QByteArray ownerBefore = bytes(ownerPath);
    const QString shareId(43, 'a');
    const QString kept = id(recipient.keepGift(shareId, 1, snapshot));
    check(recipient.collection(kept).value("keptGift").toBool(), "kept marker");
    const QByteArray keptBytes = bytes(recipientPath);
    const int keptRevision = recipient.revision();
    check(id(recipient.keepGift(shareId, 1, snapshot)) == kept && recipient.revision() == keptRevision, "keep retry deduplicated");
    check(recipient.beginEdit(kept).isEmpty(), "kept gift immutable editor");
    check(!recipient.savePresentation(kept, presentation(), recipient.revision()), "kept gift immutable presentation");
    check(!recipient.saveMemberships(ref(), {kept}).value("ok").toBool(), "kept gift immutable memberships");
    check(bytes(recipientPath) == keptBytes, "failed immutable operations preserve disk");
    NamedCollectionsStore restarted(recipientPath);
    check(restarted.healthy(), "kept gift reload");
    check(restarted.collection(kept).value("giftSnapshot").toMap() == snapshot, "offline snapshot and embedded image exact");
    auto bad = snapshot; bad["name"] = "Changed edition";
    check(!restarted.keepGift(shareId, 1, bad).value("ok").toBool(), "same gift id different bytes refused");
    check(!restarted.keepGift(shareId, 2, snapshot).value("ok").toBool(), "gift revisions immutable");
    const QString operation = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString own = id(restarted.makeOwnVersion(kept, "My version", operation));
    auto ownRow = restarted.collection(own);
    check(!ownRow.value("keptGift").toBool() && ownRow.value("description").toString().isEmpty()
        && ownRow.value("notes").toMap().isEmpty() && ownRow.value("presentation").toMap().isEmpty(), "copy starts with fresh personal text and presentation");
    check(ownRow.value("items") == restarted.collection(kept).value("items"), "copy retains selections and order");
    auto ownDraft = restarted.beginEdit(own);
    check(restarted.saveEdit(ownDraft.value("token").toString(), "Edited version", "My new description",
        ownDraft.value("items").toStringList(), {}), "copy editable");
    check(id(restarted.makeOwnVersion(kept, "My version", operation)) == own, "copy retry doesn't overwrite later edits");
    check(restarted.collection(own).value("name") == "Edited version", "copy retry preserved edit");
    check(!restarted.makeOwnVersion(kept, "Different copy request", operation).value("ok").toBool(), "changed copy retry rejected");
    check(restarted.collection(kept).value("giftSnapshot").toMap() == snapshot && bytes(ownerPath) == ownerBefore, "recipient edits don't affect kept or sender");
    NamedCollectionsStore otherProfile(dir.filePath("other-profile.json"));
    check(otherProfile.collection(kept).isEmpty() && otherProfile.beginEdit(own).isEmpty(), "profile isolation");
    const int revision = restarted.revision();
    check(!restarted.savePresentation(own, presentation(), revision - 1), "stale editor revision rejected");
    {
        QLockFile lock(recipientPath + ".lock"); check(lock.tryLock(), "fixture save lock");
        check(!restarted.keepGift(QString(43, 'b'), 1, snapshot).value("ok").toBool(), "interrupted import no partial save");
    }
    check(restarted.collections().size() == 2, "locked import no in-memory insertion");
    NamedCollectionsStore concurrent(recipientPath);
    check(!concurrent.createCollection("Other writer").isEmpty(), "concurrent write");
    check(!restarted.keepGift(QString(43, 'b'), 1, snapshot).value("ok").toBool(), "stale disk generation rejected");
    check(restarted.reload(), "reload conflict");
    check(!id(restarted.keepGift(QString(43, 'b'), 1, snapshot)).isEmpty(), "retry after reload works");
    auto unknown = ref("missing-provider", "missing:123");
    const auto missingId = owner.createCollection("Unavailable source", unknown);
    check(!missingId.isEmpty(), "unknown local reference accepted");
    auto missingSnapshot = owner.shareSnapshot(missingId);
    check(!missingSnapshot.isEmpty(), "unknown provider identity preserved");
    const QString missing = id(restarted.keepGift(QString(43, 'c'), 1, missingSnapshot));
    auto missingRef = restarted.collection(missing).value("members").toList().first().toMap();
    check(missingRef.value("source") == "missing-provider" && missingRef.value("route").toString().isEmpty(), "unknown source remains visible and unresolved");
    for (const QString &field : {QString("payload"), QString("credentials"), QString("route")}) {
        auto malicious = snapshot; malicious.insert(field, "secret");
        check(!NamedCollectionsStore::validateShareSnapshot(malicious), "unknown snapshot field refused");
    }
    check(!restarted.keepGift(QString(42, 'z'), 1, snapshot).value("ok").toBool(), "short share identifier rejected");
    bad = snapshot; auto unknownWorldMembers = snapshot.value("members").toList();
    auto unknownWorldMember = unknownWorldMembers.first().toMap(); unknownWorldMember["world"] = "unknown";
    unknownWorldMembers[0] = unknownWorldMember; bad["members"] = unknownWorldMembers;
    check(!NamedCollectionsStore::validateShareSnapshot(bad), "unsupported world rejected consistently with service");
    bad = snapshot; bad["schema_version"] = "1"; check(!NamedCollectionsStore::validateShareSnapshot(bad), "typed schema");
    bad = snapshot; bad["schema_version"] = true; check(!NamedCollectionsStore::validateShareSnapshot(bad), "boolean schema refused");
    bad = snapshot; auto members = bad.value("members").toList(); auto member = members[0].toMap();
    member["source_id"] = "file:///home/person/book"; members[0] = member; bad["members"] = members;
    check(!NamedCollectionsStore::validateShareSnapshot(bad), "private-path source rejected");
    bad = snapshot; auto present = presentation(); present["cover_data"] = "data:image/svg+xml;base64,PHN2Zz4="; bad["presentation"] = present;
    check(!NamedCollectionsStore::validateShareSnapshot(bad), "SVG rejected");
    bad = snapshot; present = presentation(); present["dedication"] = QString(2001, 'a'); bad["presentation"] = present;
    check(!NamedCollectionsStore::validateShareSnapshot(bad), "oversized dedication rejected");
    bad = snapshot; members = snapshot.value("members").toList(); members.append(members.first()); bad["members"] = members;
    check(!NamedCollectionsStore::validateShareSnapshot(bad), "duplicate member identity rejected");
    const QString localOnly = owner.createCollection("Local only", ref("vinyl-local", "123"));
    check(!localOnly.isEmpty() && !owner.shareSnapshot(localOnly).isEmpty(), "safe local identity is retained without paths");
    // Existing schema 2 remains readable and upgrades only on an actual write.
    auto legacy = QJsonDocument::fromJson(ownerBefore).object(); legacy["schemaVersion"] = 2;
    auto rows = legacy["collections"].toArray();
    for (int i = 0; i < rows.size(); ++i) { auto row = rows[i].toObject(); for (const auto &key : {"presentation","giftSnapshot","shareId","editionRevision","copyOperation","copySource","copyRequestName"}) row.remove(key); rows[i] = row; }
    legacy["collections"] = rows;
    QFile legacyFile(dir.filePath("legacy.json")); check(legacyFile.open(QIODevice::WriteOnly), "legacy fixture"); legacyFile.write(QJsonDocument(legacy).toJson()); legacyFile.close();
    NamedCollectionsStore legacyStore(legacyFile.fileName()); check(legacyStore.healthy(), "schema 2 readable");
    check(!legacyStore.createCollection("Upgrade").isEmpty(), "legacy mutation");
    check(QJsonDocument::fromJson(bytes(legacyFile.fileName())).object()["schemaVersion"].toInt() == 3, "schema upgraded on commit");
    bad = snapshot; present = presentation();
    auto damaged = QByteArray::fromBase64(present.value("cover_data").toString().section(',', 1).toLatin1());
    damaged[damaged.size() / 2] = char(damaged.at(damaged.size() / 2) ^ 0x33);
    present["cover_data"] = "data:image/png;base64," + QString::fromLatin1(damaged.toBase64()); bad["presentation"] = present;
    check(!NamedCollectionsStore::validateShareSnapshot(bad), "corrupt PNG with valid header rejected");
    NamedCollectionsStore selectedProvider(dir.filePath("selected-provider.json"));
    const QString selectedKept = id(selectedProvider.keepGift(QString(43, 'p'), 1, snapshot));
    auto selectedReference = selectedProvider.collection(selectedKept).value("members").toList().first().toMap();
    selectedReference["route"] = "manga-detail";
    selectedReference["payload"] = QVariantMap{{"malId", "13"}};
    const QString selectedCollection = selectedProvider.createCollection("Recipient provider", selectedReference);
    check(!selectedCollection.isEmpty(), "recipient creates local provider collection");
    check(selectedProvider.collection(selectedCollection).value("members").toList().first().toMap().value("route") == "manga-detail", "kept gift does not replace chosen local reopen reference");
    QByteArray merged; QString mergeError;
    check(NamedCollectionsStore::mergeForAdoption(keptBytes, keptBytes, &merged, &mergeError), "kept gift adoption deduplicates");
    check(QJsonDocument::fromJson(merged).object()["collections"].toArray().size() == 1, "adoption retains one edition");
    if (argc > 1) {
        QFile sharedFixture(QString::fromLocal8Bit(argv[1]));
        check(sharedFixture.open(QIODevice::ReadOnly), "open shared wire fixture");
        auto shared = QJsonDocument::fromJson(sharedFixture.readAll()).object().toVariantMap();
        check(NamedCollectionsStore::validateShareSnapshot(shared), "cross-language mixed media wire fixture valid");
        const auto mixedId = id(restarted.keepGift(QString(43, 'd'), 1, shared));
        NamedCollectionsStore mixedRestart(recipientPath);
        check(mixedRestart.healthy() && mixedRestart.collection(mixedId).value("giftSnapshot").toMap() == shared, "mixed fixture exact restart");
    }
    qInfo() << "PASS" << checks << "Collections sharing persistence checks";
}
