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
#include <QCryptographicHash>

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

void write(const QString &path, const QByteArray &data) {
    QFile f(path); check(f.open(QIODevice::WriteOnly|QIODevice::Truncate), "open fixture write");
    check(f.write(data)==data.size(), "complete fixture write");
}
void ordinarySaveIsEditableAndIndependent(const QString &path, const QVariantMap &snapshot) {
    NamedCollectionsStore store(path);
    const QString share(43,'o');
    const auto saved=store.saveSharedCollection(share,1,snapshot); check(saved.value("ok").toBool(),"ordinary Save must create an independent collection"); const QString savedId=id(saved);
    check(!saved.value("alreadySaved").toBool(), "ordinary first Save is new");
    const auto row=store.collection(savedId);
    check(!row.value("keptGift").toBool() && row.value("giftSnapshot").toMap().isEmpty(), "ordinary Save has no gift immutability");
    check(row.value("description")==snapshot.value("description") && row.value("presentation")==snapshot.value("presentation"), "ordinary Save retains published description/presentation");
    QVariantMap expected; check(NamedCollectionsStore::sharedMemberReference(snapshot.value("members").toList()[0].toMap(),&expected), "wire conversion validated");
    check(row.value("members").toList()[0].toMap()==expected, "ordinary Save uses exact shared reference");
    check(row.value("savedShareId")==share && row.value("savedRevision").toInt()==1, "ordinary provenance returned");
    check(row.value("savedSnapshotHash").toString()==QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(QJsonObject::fromVariantMap(snapshot)).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex()), "canonical original snapshot hash");
    auto edit=store.beginEdit(savedId); check(!edit.isEmpty(), "ordinary editable");
    const auto key=expected.value("key").toString();
    check(store.saveEdit(edit.value("token").toString(),"Recipient edits","Recipient description",edit.value("items").toStringList(),{{key,"Recipient note"}}), "ordinary recipient edit");
    const auto before=bytes(path); const int generation=store.revision();
    const auto retry=store.saveSharedCollection(share,1,snapshot);
    check(id(retry)==savedId && retry.value("alreadySaved").toBool(), "repeatedSavePreservesRecipientEdits selects prior copy");
    check(bytes(path)==before && store.revision()==generation && store.collection(savedId).value("name")=="Recipient edits", "retry does not rewrite edited copy");
    NamedCollectionsStore reload(path); check(reload.healthy() && reload.collection(savedId)==store.collection(savedId), "schema4RoundTrips all edits/provenance offline");
    auto changed=snapshot; changed["name"]="Changed sender";
    check(!store.saveSharedCollection(share,1,changed).value("ok").toBool(), "conflictingSameRevisionIsRejected");
    check(bytes(path)==before && store.revision()==generation, "conflicting save preserves disk/memory generation");
    const auto next=store.saveSharedCollection(share,2,changed);
    check(id(next)!=savedId && store.collections().size()==2, "newRevisionMakesSeparateCopy");
    check(store.collection(savedId).value("name")=="Recipient edits", "new revision preserves older recipient edits");
    // Every repeat path must still take the disk-generation and lock check.
    NamedCollectionsStore stale(path); NamedCollectionsStore writer(path);
    check(!writer.createCollection("External writer").isEmpty(), "external disk generation");
    const auto external=bytes(path); const int staleGeneration=stale.revision();
    check(!stale.saveSharedCollection(share,1,snapshot).value("ok").toBool(), "same-revision no-op verifies disk generation");
    check(bytes(path)==external && stale.revision()==staleGeneration && stale.collections().size()==2, "stale no-op preserves both memory/disk");
    check(stale.reload(), "reload stale ordinary store");
    const auto blocked=QFileInfo(path).absolutePath()+"/blocked-parent";write(blocked,"file");
    NamedCollectionsStore unwritable(blocked+"/collections.json");
    check(!unwritable.saveSharedCollection(share,1,snapshot).value("ok").toBool() && unwritable.collections().isEmpty()
        && unwritable.revision()==0 && bytes(blocked)=="file", "failed directory write creates no members or provenance");
    const auto lockedBytes=bytes(path); const auto lockedRows=stale.collections(); const int lockedGeneration=stale.revision();
    { QLockFile lock(path+".lock"); check(lock.tryLock(), "lock ordinary fixture");
      check(!stale.saveSharedCollection(share,1,snapshot).value("ok").toBool(), "repeat verifies writer lock");
      check(!stale.saveSharedCollection(QString(43,'q'),1,snapshot).value("ok").toBool(), "atomic failed Save rejects before insertion"); }
    check(bytes(path)==lockedBytes && stale.collections()==lockedRows && stale.revision()==lockedGeneration, "invalidOrFailedWritePreservesOriginal");
    check(!stale.saveSharedCollection(share,0,snapshot).value("ok").toBool(), "zero revision rejected");
    auto bad=snapshot;bad["name"]=false;
    check(!stale.saveSharedCollection(share,3,bad).value("ok").toBool() && bytes(path)==lockedBytes, "malformed snapshot preserved");
    QVariantMap unsafe=snapshot.value("members").toList()[0].toMap();unsafe["source_id"]=false;
    QVariantMap untouched{{"sentinel",true}}; check(!NamedCollectionsStore::sharedMemberReference(unsafe,&untouched) && untouched.value("sentinel").toBool(), "shared converter rejects primitive coercion without output mutation");
}
void adoptionPreservesDivergentSavedCopies(const QString &root,const QVariantMap &snapshot) {
    const auto a=root+"/adopt-a.json",b=root+"/adopt-b.json",out=root+"/adopt-out.json";
    NamedCollectionsStore first(a);const QString share(43,'v'); const auto savedId=id(first.saveSharedCollection(share,1,snapshot));
    write(b,bytes(a));NamedCollectionsStore second(b);
    auto edit=second.beginEdit(savedId);check(second.saveEdit(edit.value("token").toString(),"Divergent","Other description",edit.value("items").toStringList(),{}),"divergent recipient edits");
    const auto originalA=bytes(a),originalB=bytes(b);QByteArray merged;QString error;
    check(NamedCollectionsStore::mergeForAdoption(originalA,originalB,&merged,&error),"ordinary adoption succeeds");write(out,merged);NamedCollectionsStore combined(out);
    check(combined.healthy() && combined.collections().size()==2,"adoption retains both same-hash divergent editable copies");
    for(const auto &value:combined.collections()){const auto row=value.toMap();check(row.value("savedShareId")==share && !combined.beginEdit(row.value("id").toString()).isEmpty(),"adopted provenance/editability");}
    QByteArray repeated;check(NamedCollectionsStore::mergeForAdoption(merged,originalB,&repeated,&error) && repeated==merged,"adoption retry stable");
    const auto beforeRetry=bytes(out); const int generation=combined.revision();
    check(combined.saveSharedCollection(share,1,snapshot).value("alreadySaved").toBool() && bytes(out)==beforeRetry && combined.revision()==generation,"Save after adoption preserves both divergent copies");
    NamedCollectionsStore different(root+"/adopt-conflict.json");auto altered=snapshot;altered["description"]="Conflicting received original";
    id(different.saveSharedCollection(share,1,altered));QByteArray sentinel="untouched";
    check(!NamedCollectionsStore::mergeForAdoption(originalA,bytes(different.filePath()),&sentinel,&error) && sentinel=="untouched","adoption conflicting original hashes rejected atomically");
    check(bytes(a)==originalA && bytes(b)==originalB,"adoption originals preserved");
    NamedCollectionsStore external(out);check(!external.createCollection("After adoption").isEmpty(),"adoption external writer");
    const auto externalBytes=bytes(out);const auto oldRows=combined.collections();const int oldGeneration=combined.revision();
    check(!combined.saveSharedCollection(share,1,snapshot).value("ok").toBool(),"adopted duplicate no-op checks disk generation");
    check(bytes(out)==externalBytes && combined.collections()==oldRows && combined.revision()==oldGeneration,"stale adopted retry preserves both divergent copies and newer disk");
}
void schema2And3UpgradeLosslessly(const QString &root,const QByteArray &ordinary,const QByteArray &gift) {
    for(int schema:{2,3}) {
        auto object=QJsonDocument::fromJson(schema==2?ordinary:gift).object();object["schemaVersion"]=schema;
        auto rows=object["collections"].toArray();for(int i=0;i<rows.size();++i){auto row=rows[i].toObject();
            for(const auto &field:{"savedShareId","savedRevision","savedSnapshotHash"})row.remove(field);
            if(schema==2)for(const auto &field:{"presentation","giftSnapshot","shareId","editionRevision","copyOperation","copySource","copyRequestName"})row.remove(field);
            rows[i]=row;}object["collections"]=rows;
        const auto original=QJsonDocument(object).toJson(QJsonDocument::Compact);const auto path=root+QString("/schema%1.json").arg(schema);write(path,original);
        NamedCollectionsStore old(path);check(old.healthy() && bytes(path)==original,"old schema reads without rewrite");const auto previous=old.collections();
        if(schema==3){const auto giftRow=previous[0].toMap();check(old.keepGift(giftRow.value("shareId").toString(),1,giftRow.value("giftSnapshot").toMap()).value("ok").toBool() && bytes(path)==original,"schema3 idempotent keep does not rewrite/upgrade");}
        else {const auto row=previous[0].toMap();const auto draft=old.beginEdit(row.value("id").toString());check(old.saveEdit(draft.value("token").toString(),row.value("name").toString(),row.value("description").toString(),row.value("items").toStringList(),row.value("notes").toMap()) && bytes(path)==original,"schema2 no-op edit does not rewrite/upgrade");}
        check(!old.createCollection("Successful upgrade").isEmpty(),"old schema mutation succeeds");
        check(QJsonDocument::fromJson(bytes(path)).object()["schemaVersion"].toInt()==4,"successful mutation upgrades to4");
        for(const auto &row:previous)check(old.collection(row.toMap().value("id").toString())==row.toMap(),"old collections upgraded losslessly");
        NamedCollectionsStore reload(path);check(reload.healthy() && reload.collections()==old.collections(),"upgraded file reloads exactly");
    }
}
void malformedProvenanceNeverRewrites(const QString &root,const QByteArray &valid) {
    auto object=QJsonDocument::fromJson(valid).object();auto rows=object["collections"].toArray();
    const auto original=rows[0].toObject();int index=0;
    QList<QPair<QString,QJsonValue>> cases={{"savedShareId",true},{"savedShareId",QString(42,'a')},{"savedShareId",QString()},
        {"savedRevision","1"},{"savedRevision",true},{"savedRevision",1.5},{"savedRevision",0},{"savedRevision",-1},
        {"savedSnapshotHash",true},{"savedSnapshotHash",QString(64,'A')},{"savedSnapshotHash",QString(63,'a')},{"savedSnapshotHash",QString()}};
    for(const auto &item:cases){auto row=original;row[item.first]=item.second;auto badRows=rows;badRows[0]=row;auto bad=object;bad["collections"]=badRows;
        const auto path=root+QString("/malformed%1.json").arg(index++);const auto data=QJsonDocument(bad).toJson();write(path,data);NamedCollectionsStore rejected(path);
        check(!rejected.healthy() && !rejected.saveSharedCollection(QString(43,'k'),1,QVariantMap()).value("ok").toBool() && bytes(path)==data,"malformed provenance rejected without rewrite");}
    for(int schema:{1,5,99}){auto bad=object;bad["schemaVersion"]=schema;const auto path=root+QString("/future%1.json").arg(schema);const auto data=QJsonDocument(bad).toJson();write(path,data);NamedCollectionsStore rejected(path);
        check(!rejected.healthy() && rejected.createCollection("Never").isEmpty() && bytes(path)==data,"unknown schema preserved");}
}
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
    ordinarySaveIsEditableAndIndependent(dir.filePath("ordinary.json"),snapshot);
    adoptionPreservesDivergentSavedCopies(dir.path(),snapshot);
    const QString shareId(43, 'a');
    const QString kept = id(recipient.keepGift(shareId, 1, snapshot));
    check(recipient.collection(kept).value("keptGift").toBool(), "kept marker");
    const QByteArray keptBytes = bytes(recipientPath);
    schema2And3UpgradeLosslessly(dir.path(),ownerBefore,keptBytes);
    NamedCollectionsStore provenance(dir.filePath("provenance.json"));id(provenance.saveSharedCollection(QString(43,'a'),1,snapshot));
    malformedProvenanceNeverRewrites(dir.path(),bytes(provenance.filePath()));
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
    for (int i = 0; i < rows.size(); ++i) { auto row = rows[i].toObject(); for (const auto &key : {"presentation","giftSnapshot","shareId","editionRevision","copyOperation","copySource","copyRequestName","savedShareId","savedRevision","savedSnapshotHash"}) row.remove(key); rows[i] = row; }
    legacy["collections"] = rows;
    QFile legacyFile(dir.filePath("legacy.json")); check(legacyFile.open(QIODevice::WriteOnly), "legacy fixture"); legacyFile.write(QJsonDocument(legacy).toJson()); legacyFile.close();
    NamedCollectionsStore legacyStore(legacyFile.fileName()); check(legacyStore.healthy(), "schema 2 readable");
    check(!legacyStore.createCollection("Upgrade").isEmpty(), "legacy mutation");
    check(QJsonDocument::fromJson(bytes(legacyFile.fileName())).object()["schemaVersion"].toInt() == 4, "schema upgraded on commit");
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
    {
        QFile sharedFixture(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(COLOSSEUM_MIXED_SHARE_FIXTURE));
        check(sharedFixture.open(QIODevice::ReadOnly), "open shared wire fixture");
        auto shared = QJsonDocument::fromJson(sharedFixture.readAll()).object().toVariantMap();
        check(NamedCollectionsStore::validateShareSnapshot(shared), "cross-language mixed media wire fixture valid");
        const auto ordinaryMixedId=id(restarted.saveSharedCollection(QString(43,'e'),2,shared));
        auto wireMembers=shared.value("members").toList();
        for(int i=0;i<wireMembers.size();++i){const auto wire=wireMembers[i].toMap();const auto saved=restarted.items(ordinaryMixedId)[i].toMap();
            check(saved.value("world")==wire.value("world") && saved.value("kind")==wire.value("kind") && saved.value("source")==wire.value("source")
                && saved.value("sourceId")==wire.value("source_id") && saved.value("editionId")==wire.value("edition_id") && saved.value("mediaId")==wire.value("canonical_id"),"mixed ordinary exact source/canonical/edition assertions");
            QVariantMap payload;if(!wire.value("canonical_source_id").toString().isEmpty())payload.insert("canonicalSourceId",wire.value("canonical_source_id"));
            if(!wire.value("album_source_id").toString().isEmpty())payload.insert("albumId",wire.value("album_source_id"));
            if(!wire.value("album_browse_id").toString().isEmpty())payload.insert("browseId",wire.value("album_browse_id"));
            if(!wire.value("comic_post_ids").toList().isEmpty())payload.insert("posts",wire.value("comic_post_ids"));
            check(saved.value("payload").toMap()==payload && saved.value("route").toString().isEmpty() && saved.value("cover").toString().isEmpty() && saved.value("note")==wire.value("note"),"mixed ordinary retains exact safe assertions/notes without provider lookup");}
        const auto mixedId = id(restarted.keepGift(QString(43, 'd'), 1, shared));
        NamedCollectionsStore mixedRestart(recipientPath);
        check(mixedRestart.healthy() && mixedRestart.collection(ordinaryMixedId)==restarted.collection(ordinaryMixedId),"mixed ordinary schema4 exact offline reload");
        check(mixedRestart.healthy() && mixedRestart.collection(mixedId).value("giftSnapshot").toMap() == shared, "mixed fixture exact restart");
    }
    qInfo() << "PASS" << checks << "Collections sharing persistence checks";
}
