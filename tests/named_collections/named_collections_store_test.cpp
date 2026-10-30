#include "NamedCollectionsStore.h"
#include "account/ProfilePaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <QUuid>

#include <cstdlib>
#include <iostream>

namespace {
int assertions = 0;
void require(bool condition, const char *message)
{
    ++assertions;
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read evidence file");
    return file.readAll();
}

void write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "open fixture output");
    require(file.write(bytes) == bytes.size(), "write complete fixture");
}

QByteArray snapshot(const NamedCollectionsStore &store)
{
    return QJsonDocument(QJsonArray::fromVariantList(store.collections())).toJson(QJsonDocument::Compact);
}

QVariantMap reference(const QString &id, const QString &edition = {}, bool canonical = true)
{
    const QString mediaId = canonical ? QStringLiteral("ct1:4a000000-0000-5000-8000-") + id.rightJustified(12, QLatin1Char('0')) : QString();
    const QString key = canonical ? mediaId + QLatin1Char('|') + edition
        : QStringLiteral("tankoban|comic|locg|") + id + QLatin1Char('|') + edition;
    return {{QStringLiteral("key"), key}, {QStringLiteral("world"), canonical ? QStringLiteral("theatre") : QStringLiteral("tankoban")},
        {QStringLiteral("kind"), canonical ? QStringLiteral("movie") : QStringLiteral("comic")},
        {QStringLiteral("mediaId"), mediaId}, {QStringLiteral("source"), canonical ? QStringLiteral("imdb") : QStringLiteral("locg")},
        {QStringLiteral("sourceId"), id}, {QStringLiteral("editionId"), edition},
        {QStringLiteral("title"), QStringLiteral("Title ") + id}, {QStringLiteral("cover"), QStringLiteral("https://example.test/cover.png")},
        {QStringLiteral("creator"), QStringLiteral("A Creator")}, {QStringLiteral("year"), QStringLiteral("2001")},
        {QStringLiteral("route"), canonical ? QStringLiteral("theatre") : QStringLiteral("comic")},
        {QStringLiteral("payload"), QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("type"), QStringLiteral("movie")}}}};
}

QString key(const QVariantMap &ref) { return ref.value(QStringLiteral("key")).toString(); }
QString token(const QVariantMap &draft) { return draft.value(QStringLiteral("token")).toString(); }
QStringList keys(const NamedCollectionsStore &store, const QString &id)
{ return store.collection(id).value(QStringLiteral("items")).toStringList(); }

struct Before {
    QByteArray memory;
    QByteArray disk;
    int revision;
    explicit Before(const NamedCollectionsStore &store)
        : memory(snapshot(store)), disk(read(store.filePath())), revision(store.revision()) {}
    void unchanged(const NamedCollectionsStore &store) const {
        require(snapshot(store) == memory, "failed/canceled operation preserves memory");
        require(read(store.filePath()) == disk, "failed/canceled operation preserves bytes");
        require(store.revision() == revision, "failed/canceled operation preserves revision");
    }
};

void normalLifecycle(const QString &path)
{
    NamedCollectionsStore store(path);
    require(store.healthy() && store.collections().isEmpty(), "fresh store healthy and empty");
    require(store.createCollection(QStringLiteral("   ")).isEmpty(), "blank name rejected");
    require(!QFileInfo::exists(path), "invalid first create does not create data file");
    const QVariantMap film = reference(QStringLiteral("1"));
    const QString one = store.createCollection(QStringLiteral("  Stories   that stay  "), film, QStringLiteral("My description"));
    require(!QUuid(one).isNull(), "collection owns stable UUID");
    require(store.contains(one, key(film)) && keys(store, one).size() == 1, "create-and-add single collection and member");
    require(store.collection(one).value(QStringLiteral("description")).toString() == QLatin1String("My description"), "description stored");
    Before before(store);
    require(store.createCollection(QStringLiteral("STORIES that   STAY")).isEmpty(), "casefold and whitespace duplicate rejected");
    before.unchanged(store);
    require(store.createCollection(QString::fromUtf8("\xCE\xA3\xCE\x99\xCE\x93\xCE\x9C\xCE\x91")).size() > 0, "Unicode collection name accepted");
    require(store.createCollection(QString::fromUtf8("\xCF\x83\xCE\xB9\xCE\xB3\xCE\xBC\xCE\xB1")).isEmpty(), "Unicode casefold duplicate rejected");
    const QString composed = QString::fromUtf8("Caf\xC3\xA9");
    require(!store.createCollection(composed).isEmpty(), "NFC name accepted");
    require(store.createCollection(QStringLiteral("Cafe") + QChar(0x0301)).isEmpty(), "canonical Unicode equivalent name rejected");
    const QString two = store.createCollection(QStringLiteral("Second"));
    require(!two.isEmpty() && keys(store, two).isEmpty(), "overview creation starts empty");
    const auto membership = store.saveMemberships(film, {one, two}, QStringLiteral("Third"));
    const QString three = membership.value(QStringLiteral("collectionId")).toString();
    require(membership.value(QStringLiteral("ok")).toBool() && !three.isEmpty(), "create-and-membership transaction succeeds");
    require(store.memberships(key(film)).size() == 3, "same title saved in multiple collections");
    const Before repeated(store);
    require(store.saveMemberships(film, {one, two, three}).value(QStringLiteral("ok")).toBool(), "repeat memberships succeed");
    repeated.unchanged(store);

    QVariantMap canonicalAlias = film;
    canonicalAlias.insert(QStringLiteral("source"), QStringLiteral("another-provider"));
    canonicalAlias.insert(QStringLiteral("sourceId"), QStringLiteral("alias-1"));
    canonicalAlias.insert(QStringLiteral("world"), QStringLiteral("vault"));
    canonicalAlias.insert(QStringLiteral("kind"), QStringLiteral("film"));
    canonicalAlias.insert(QStringLiteral("route"), QStringLiteral("vault"));
    canonicalAlias.insert(QStringLiteral("title"), QStringLiteral("Alias display title"));
    require(store.saveMemberships(canonicalAlias, {one, two, three}).value(QStringLiteral("ok")).toBool(), "canonical cross-world/provider aliases are idempotent");
    repeated.unchanged(store);
    require(store.items(one).first().toMap().value(QStringLiteral("source")).toString() == QLatin1String("imdb"), "first reopen snapshot preserved");

    const QVariantMap differentEdition = reference(QStringLiteral("1"), QStringLiteral("extended"));
    require(store.saveMemberships(differentEdition, {one}).value(QStringLiteral("ok")).toBool(), "edition is distinct");
    QVariantMap sourceOwned = reference(QStringLiteral("1"), {}, false);
    sourceOwned.insert(QStringLiteral("route"), QString());
    require(store.saveMemberships(sourceOwned, {one}).value(QStringLiteral("ok")).toBool(), "unresolved source-owned reference retained");
    QVariantMap otherSource = sourceOwned;
    otherSource.insert(QStringLiteral("key"), QStringLiteral("tankoban|comic|gcd|1|"));
    otherSource.insert(QStringLiteral("source"), QStringLiteral("gcd"));
    require(store.saveMemberships(otherSource, {one}).value(QStringLiteral("ok")).toBool(), "same bare ID in different source stays distinct");
    require(keys(store, one).size() == 4, "canonical, edition, and source references coexist");
    const Before sourceCollision(store);
    QVariantMap collidingSource = sourceOwned;
    collidingSource.insert(QStringLiteral("sourceId"), QStringLiteral("a-different-source-identity"));
    require(!store.saveMemberships(collidingSource, {one}).value(QStringLiteral("ok")).toBool(), "same source-owned key cannot change identity");
    sourceCollision.unchanged(store);

    const QVariantMap draft = store.beginEdit(one);
    require(!token(draft).isEmpty(), "native edit token issued");
    const Before canceled(store);
    require(store.cancelEdit(token(draft)), "draft cancel accepted");
    canceled.unchanged(store);
    require(!store.saveEdit(token(draft), QStringLiteral("Cannot save"), {}, {}, {}), "canceled token cannot save");
    canceled.unchanged(store);

    const QVariantMap edit = store.beginEdit(one);
    const QStringList order{key(sourceOwned), key(differentEdition), key(film)};
    const QVariantMap notes{{key(film), QStringLiteral("A personal note\nSecond line")}, {key(sourceOwned), QStringLiteral("Keep until source returns")}};
    require(store.saveEdit(token(edit), QStringLiteral("Renamed"), QStringLiteral("Updated description"), order, notes), "draft metadata, order, notes, and removal saved atomically");
    require(keys(store, one) == order, "saved ordering exact");
    require(store.collection(one).value(QStringLiteral("notes")).toMap().value(key(film)).toString() == QLatin1String("A personal note\nSecond line"), "personal note saved");
    require(store.contains(two, key(film)) && store.contains(three, key(film)), "curation does not remove other memberships");

    const QByteArray restartState = snapshot(store);
    NamedCollectionsStore restarted(path);
    require(restarted.healthy() && snapshot(restarted) == restartState, "all collection state survives restart exactly");
    require(restarted.items(one).first().toMap().value(QStringLiteral("route")).toString().isEmpty(), "unresolved route preserved across restart");
    require(restarted.collection(one).value(QStringLiteral("id")).toString() == one, "UUID stable after rename and restart");
    require(restarted.deleteCollection(two), "delete one collection succeeds");
    require(restarted.collection(two).isEmpty() && restarted.contains(three, key(film)), "delete does not touch another collection");
    std::cout << "PASS lifecycle, canonical identity, drafts, and restart\n";
}

void rejectedMutations(const QString &path)
{
    NamedCollectionsStore store(path);
    const QVariantMap film = reference(QStringLiteral("2"));
    const QString id = store.createCollection(QStringLiteral("Shelf"), film);
    const Before before(store);
    require(store.createCollection(QString(101, QLatin1Char('x'))).isEmpty(), "overlong name rejected");
    require(store.createCollection(QStringLiteral("Bad\nname")).isEmpty(), "control-character name rejected");
    QString brokenSurrogate(1, QChar(0xD800));
    require(store.createCollection(brokenSurrogate).isEmpty(), "invalid Unicode rejected");
    require(!store.deleteCollection(QStringLiteral("unknown")), "unknown delete rejected");
    QVariantMap invalid = film;
    invalid.remove(QStringLiteral("key"));
    require(store.createCollection(QStringLiteral("No partial create"), invalid).isEmpty(), "invalid initial reference rejects entire create");
    require(!store.saveMemberships(invalid, {id}).value(QStringLiteral("ok")).toBool(), "invalid membership reference rejected");
    invalid = film;
    invalid.insert(QStringLiteral("key"), QString(100000, QLatin1Char('k')));
    require(!store.saveMemberships(invalid, {id}).value(QStringLiteral("ok")).toBool(), "overlarge identity rejected");
    invalid = film;
    invalid.insert(QStringLiteral("payload"), QVariantMap{{QStringLiteral("large"), QString(100000, QLatin1Char('p'))}});
    require(!store.saveMemberships(invalid, {id}).value(QStringLiteral("ok")).toBool(), "overlarge reference rejected");
    invalid = film;
    invalid.insert(QStringLiteral("year"), 2001);
    require(!store.saveMemberships(invalid, {id}).value(QStringLiteral("ok")).toBool(), "reference field type mismatch rejected");
    invalid = film;
    invalid.insert(QStringLiteral("key"), key(film) + QLatin1Char('\n'));
    require(!store.saveMemberships(invalid, {id}).value(QStringLiteral("ok")).toBool(), "identity control characters are not silently trimmed away");
    invalid = film;
    invalid.insert(QStringLiteral("payload"), QVariantMap{{QStringLiteral("badUnicode"), brokenSurrogate}});
    require(!store.saveMemberships(invalid, {id}).value(QStringLiteral("ok")).toBool(), "malformed Unicode inside payload rejected");
    invalid = film;
    invalid.insert(QStringLiteral("payload"), QVariantMap{{QStringLiteral("badControl"), QStringLiteral("x") + QChar(0) + QStringLiteral("y")}});
    require(!store.saveMemberships(invalid, {id}).value(QStringLiteral("ok")).toBool(), "control characters inside payload rejected");
    invalid = film;
    invalid.insert(QStringLiteral("mediaId"), QStringLiteral("different-canonical-id"));
    require(!store.saveMemberships(invalid, {id}).value(QStringLiteral("ok")).toBool(), "same-key conflicting canonical identity rejected");
    require(!store.saveMemberships(film, {id, id}, QStringLiteral("No duplicate selection")).value(QStringLiteral("ok")).toBool(), "duplicate selected collection rejects transaction");
    require(!store.saveMemberships(film, {QStringLiteral("unknown")}, QStringLiteral("No partial collection")).value(QStringLiteral("ok")).toBool(), "unknown collection rejects create-and-memberships atomically");
    require(!store.saveMemberships(film, {id}, QStringLiteral("  ")).value(QStringLiteral("ok")).toBool(), "blank requested new name rejected");
    before.unchanged(store);
    require(!store.error().isEmpty(), "rejected mutation exposes error");
    require(store.saveMemberships(film, {id}).value(QStringLiteral("ok")).toBool() && store.error().isEmpty(), "verified idempotent save clears previous error");
    before.unchanged(store);

    const QVariantMap edit = store.beginEdit(id);
    require(!store.saveEdit(token(edit), QStringLiteral("Shelf"), QString(301, QLatin1Char('d')), keys(store,id), {}), "overlong description rejected");
    require(!store.saveEdit(token(edit), QStringLiteral("Shelf"), {}, {key(film), key(film)}, {}), "duplicate draft member rejected");
    require(!store.saveEdit(token(edit), QStringLiteral("Shelf"), {}, {QStringLiteral("injected")}, {}), "editor cannot inject references");
    require(!store.saveEdit(token(edit), QStringLiteral("Shelf"), {}, keys(store,id), {{key(film), QString(501, QLatin1Char('n'))}}), "overlong note rejected");
    require(!store.saveEdit(token(edit), QStringLiteral("Shelf"), {}, keys(store,id), {{QStringLiteral("alien-key"), QStringLiteral("note")}}), "notes cannot target unknown members");
    require(!store.reload(), "reload does not discard open drafts implicitly");
    before.unchanged(store);
    require(store.saveEdit(token(edit), QStringLiteral("Shelf"), {}, keys(store,id), {{key(film), QStringLiteral("Retried after validation")}}), "invalid save retains draft for retry");

    const QVariantMap stale = store.beginEdit(id);
    require(!store.createCollection(QStringLiteral("Another shelf")).isEmpty(), "concurrent collection mutation succeeds");
    const Before changed(store);
    require(!store.saveEdit(token(stale), QStringLiteral("Stale edit"), {}, keys(store,id), {}), "stale draft revision rejected");
    changed.unchanged(store);
    require(store.cancelEdit(token(stale)), "stale draft remains cancelable");
    require(!store.saveEdit(QStringLiteral("foreign-token"), QStringLiteral("Foreign edit"), {}, keys(store,id), {}), "unknown/profile-foreign token rejected");
    changed.unchanged(store);
    std::cout << "PASS rejection, rollback, and draft conflict boundaries\n";
}

void displayMetadataBoundary(const QString &path)
{
    NamedCollectionsStore store(path);
    QVariantMap maximum = reference(QStringLiteral("7"));
    maximum.insert(QStringLiteral("title"), QString(1024, QLatin1Char('t')));
    maximum.insert(QStringLiteral("creator"), QString(1024, QLatin1Char('c')));
    const QString id = store.createCollection(QStringLiteral("Full owner metadata"), maximum);
    require(!id.isEmpty() && store.contains(id, key(maximum)), "owner title and creator at 1024 characters accepted");
    const Before before(store);
    QVariantMap excessive = maximum;
    excessive.insert(QStringLiteral("title"), QString(1025, QLatin1Char('t')));
    require(!store.saveMemberships(excessive, {id}, QStringLiteral("No overlong title")).value(QStringLiteral("ok")).toBool(), "1025-character title rejected atomically");
    before.unchanged(store);
    excessive = maximum;
    excessive.insert(QStringLiteral("creator"), QString(1025, QLatin1Char('c')));
    require(!store.saveMemberships(excessive, {id}, QStringLiteral("No overlong creator")).value(QStringLiteral("ok")).toBool(), "1025-character creator rejected atomically");
    before.unchanged(store);
    NamedCollectionsStore restarted(path);
    require(restarted.healthy() && restarted.items(id).size() == 1, "maximum metadata reopens as one valid membership");
    const QVariantMap restored = restarted.items(id).first().toMap();
    require(restored.value(QStringLiteral("title")) == maximum.value(QStringLiteral("title"))
        && restored.value(QStringLiteral("creator")) == maximum.value(QStringLiteral("creator")), "full 1024-character owner metadata survives restart without truncation");
    std::cout << "PASS owner display metadata boundaries\n";
}

void corruptAndUnsupported(const QString &directory)
{
    const QString seedPath = QDir(directory).filePath(QStringLiteral("valid-seed.json"));
    NamedCollectionsStore seed(seedPath);
    const QString id = seed.createCollection(QStringLiteral("Preserved"), reference(QStringLiteral("3")));
    require(!id.isEmpty(), "schema fixture seed created");
    const QJsonObject valid = QJsonDocument::fromJson(read(seedPath)).object();
    QList<QByteArray> corruptions{QByteArray("{not JSON"), QByteArray("[]"), QByteArray("{}"), QByteArray(8*1024*1024+1, 'x')};
    QJsonObject version = valid;
    version.insert(QStringLiteral("schemaVersion"), 99);
    corruptions.append(QJsonDocument(version).toJson());
    QJsonObject fractional = valid;
    fractional.insert(QStringLiteral("schemaVersion"), 2.5);
    corruptions.append(QJsonDocument(fractional).toJson());
    QJsonArray rows = valid.value(QStringLiteral("collections")).toArray();
    QJsonObject duplicated = valid;
    rows.append(rows.first());
    duplicated.insert(QStringLiteral("collections"), rows);
    corruptions.append(QJsonDocument(duplicated).toJson());
    rows = valid.value(QStringLiteral("collections")).toArray();
    QJsonObject duplicateMember = rows.first().toObject();
    QJsonArray members = duplicateMember.value(QStringLiteral("items")).toArray();
    members.append(members.first());
    duplicateMember.insert(QStringLiteral("items"), members);
    QJsonObject malformed = valid;
    malformed.insert(QStringLiteral("collections"), QJsonArray{duplicateMember});
    corruptions.append(QJsonDocument(malformed).toJson());
    duplicateMember = rows.first().toObject();
    duplicateMember.insert(QStringLiteral("id"), QStringLiteral("not-a-uuid"));
    malformed.insert(QStringLiteral("collections"), QJsonArray{duplicateMember});
    corruptions.append(QJsonDocument(malformed).toJson());
    duplicateMember = rows.first().toObject();
    duplicateMember.insert(QStringLiteral("description"), QJsonArray());
    malformed.insert(QStringLiteral("collections"), QJsonArray{duplicateMember});
    corruptions.append(QJsonDocument(malformed).toJson());
    QJsonObject collisionList = rows.first().toObject();
    collisionList.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    collisionList.insert(QStringLiteral("name"), QStringLiteral("A different collection"));
    QJsonObject collisionMember = collisionList.value(QStringLiteral("items")).toArray().first().toObject();
    QJsonObject collisionReference = collisionMember.value(QStringLiteral("reference")).toObject();
    collisionReference.insert(QStringLiteral("mediaId"), QStringLiteral("conflicting-canonical-identity"));
    collisionMember.insert(QStringLiteral("reference"), collisionReference);
    collisionList.insert(QStringLiteral("items"), QJsonArray{collisionMember});
    malformed.insert(QStringLiteral("collections"), QJsonArray{rows.first(), collisionList});
    corruptions.append(QJsonDocument(malformed).toJson());

    int index = 0;
    for (const QByteArray &bytes : corruptions) {
        const QString path = QDir(directory).filePath(QStringLiteral("invalid-%1.json").arg(index++));
        write(path, bytes);
        NamedCollectionsStore rejected(path);
        require(!rejected.healthy() && !rejected.error().isEmpty(), "malformed/unsupported file fails closed");
        require(rejected.createCollection(QStringLiteral("Never overwrite")).isEmpty(), "unhealthy store cannot overwrite input");
        require(read(path) == bytes && rejected.collections().isEmpty(), "original invalid bytes preserved without partial load");
    }
    const QByteArray goodMemory = snapshot(seed);
    write(seedPath, QByteArray("corrupted after initial load"));
    require(!seed.reload() && !seed.healthy(), "reload detects corruption");
    require(snapshot(seed) == goodMemory, "failed reload preserves last good memory");
    require(seed.createCollection(QStringLiteral("Cannot overwrite corruption")).isEmpty(), "corrupt reload disables writes");
    require(read(seedPath) == QByteArray("corrupted after initial load"), "reload does not repair by destroying original bytes");
    write(seedPath, QJsonDocument(valid).toJson());
    require(seed.reload() && seed.healthy(), "explicit reload recovers after file repaired");
    require(snapshot(seed) == goodMemory, "repaired reload restores exact logical state");
    std::cout << "PASS malformed data, unknown schema, corruption, and recovery\n";
}

void adoptionKeepsVersionsAndIsIdempotent(const QString &directory)
{
    const QString accountPath = QDir(directory).filePath(QStringLiteral("adoption-account.json"));
    const QString localPath = QDir(directory).filePath(QStringLiteral("adoption-local.json"));
    const QString mergedPath = QDir(directory).filePath(QStringLiteral("adoption-merged.json"));
    NamedCollectionsStore account(accountPath);
    const auto film = reference(QStringLiteral("55"));
    const QString longName = QString(85, QLatin1Char('a')) + QString::fromUtf8("\xf0\x9f\x98\x80")
        + QString(13, QLatin1Char('x'));
    const QString id = account.createCollection(longName, film, QStringLiteral("Account meaning"));
    require(!id.isEmpty(), "create account adoption source");
    write(localPath, read(accountPath));
    NamedCollectionsStore local(localPath);
    const auto draft = local.beginEdit(id);
    require(local.saveEdit(token(draft), longName, QStringLiteral("Local meaning"),
        {key(film)}, {{key(film), QStringLiteral("A local personal note")}}), "edit divergent local version");
    const QByteArray localBytes = read(localPath);
    QByteArray merged;
    QString error;
    require(NamedCollectionsStore::mergeForAdoption(read(accountPath), localBytes, &merged, &error),
        "adoption combines divergent collections without deleting either version");
    write(mergedPath, merged);
    NamedCollectionsStore combined(mergedPath);
    require(combined.healthy() && combined.collections().size() == 2, "merged identities and Unicode names remain valid");
    require(combined.collection(id).value(QStringLiteral("description")) == QLatin1String("Account meaning"),
        "existing account meaning remains unchanged");
    QString importedId;
    for (const auto &list : combined.collections())
        if (list.toMap().value(QStringLiteral("id")).toString() != id)
            importedId = list.toMap().value(QStringLiteral("id")).toString();
    require(!importedId.isEmpty(), "divergent local version owns a separate stable identity");
    require(combined.items(importedId).first().toMap().value(QStringLiteral("note"))
        == QLatin1String("A local personal note"), "adoption preserves local notes and references");
    QByteArray repeated;
    require(NamedCollectionsStore::mergeForAdoption(merged, localBytes, &repeated, &error) && repeated == merged,
        "retrying the same imported version is idempotent even after a name collision");
    const auto edit = combined.beginEdit(importedId);
    require(combined.saveEdit(token(edit), QStringLiteral("My later rename"), QStringLiteral("Later account edit"),
        {key(film)}, {{key(film), QStringLiteral("My later note")}}), "edit imported version in account");
    const QByteArray edited = read(mergedPath);
    require(NamedCollectionsStore::mergeForAdoption(edited, localBytes, &repeated, &error) && repeated == edited,
        "retry preserves later edits to the already imported version");
    repeated = QByteArrayLiteral("unchanged output");
    require(!NamedCollectionsStore::mergeForAdoption(edited,
        QByteArrayLiteral("{\"schemaVersion\":99,\"collections\":[]}"), &repeated, &error),
        "unreadable conflicting source blocks adoption merge");
    require(repeated == QByteArrayLiteral("unchanged output") && read(mergedPath) == edited
        && read(localPath) == localBytes, "failed merge preserves output and both original files");
    std::cout << "PASS lossless/idempotent adoption versions\n";
}

void persistenceFailuresAndProfiles(const QString &directory)
{
    const QString blocked = QDir(directory).filePath(QStringLiteral("not-a-directory"));
    write(blocked, "regular file");
    NamedCollectionsStore unwritable(blocked + QStringLiteral("/collections.json"));
    require(unwritable.healthy(), "missing destination initializes without touching disk");
    require(unwritable.createCollection(QStringLiteral("Cannot save"), reference(QStringLiteral("4"))).isEmpty(), "unwritable directory rejects create-and-add");
    require(unwritable.collections().isEmpty() && unwritable.revision() == 0, "failed persistence rolls back memory and revision");
    const QString path = QDir(directory).filePath(QStringLiteral("concurrent.json"));
    NamedCollectionsStore first(path);
    const QString id = first.createCollection(QStringLiteral("Original"), reference(QStringLiteral("5")));
    NamedCollectionsStore second(path);
    require(!first.createCollection(QStringLiteral("New state")).isEmpty(), "first owner commits newer state");
    const QByteArray latestBytes = read(path);
    const QByteArray staleMemory = snapshot(second);
    const int staleRevision = second.revision();
    require(second.createCollection(QStringLiteral("Must not overwrite newer state")).isEmpty(), "stale disk fingerprint rejects overwrite");
    require(!second.saveMemberships(reference(QStringLiteral("5")), {id}).value(QStringLiteral("ok")).toBool(), "no-op save also rejects stale persistence instead of clearing conflict");
    require(read(path) == latestBytes && snapshot(second) == staleMemory && second.revision() == staleRevision, "conflict preserves newer disk and previous memory");
    require(second.reload(), "explicit reload refreshes stale instance");
    require(snapshot(second) == snapshot(first), "reload sees exact newer state");
    QLockFile lock(path + QStringLiteral(".lock"));
    require(lock.tryLock(0), "test holds writer lock");
    const Before locked(second);
    require(!second.deleteCollection(id), "concurrent writer lock prevents deletion");
    locked.unchanged(second);
    lock.unlock();
    require(second.deleteCollection(id), "operation can retry after lock released");

    const ProfilePaths local = ProfilePaths::localOnly(directory);
    const auto account = ProfilePaths::account(QStringLiteral("d0640494-240c-4c63-b88b-c7a02d47f416"), directory);
    require(account.has_value(), "account profile path accepted");
    require(local.namedCollectionsPath() != account->namedCollectionsPath(), "local/account collection paths isolated");
    require(ProfilePaths::sealed(directory).namedCollectionsPath().isEmpty(), "sealed profile does not expose durable collections path");
    require(ProfilePaths::legacyLocal().namedCollectionsPath().isEmpty(), "legacy path selection remains runtime-owned");
    NamedCollectionsStore localStore(local.namedCollectionsPath());
    NamedCollectionsStore accountStore(account->namedCollectionsPath());
    const QString localId = localStore.createCollection(QStringLiteral("Personal"), reference(QStringLiteral("6")));
    const QString accountId = accountStore.createCollection(QStringLiteral("Personal"));
    require(!localId.isEmpty() && !accountId.isEmpty() && localId != accountId, "same name in different profiles creates independent UUIDs");
    const QVariantMap localDraft = localStore.beginEdit(localId);
    require(!accountStore.saveEdit(token(localDraft), QStringLiteral("Wrong owner"), {}, {}, {}), "draft token cannot cross profile store instances");
    require(localStore.cancelEdit(token(localDraft)), "origin profile still owns draft");
    NamedCollectionsStore localRestart(local.namedCollectionsPath());
    NamedCollectionsStore accountRestart(account->namedCollectionsPath());
    require(localRestart.contains(localId, key(reference(QStringLiteral("6")))) && keys(accountRestart,accountId).isEmpty(), "profile isolation survives restart");
    require(!QFileInfo::exists(local.collectionIniPath()) && !QFileInfo::exists(local.progressIniPath()), "named collections never create Library/progress stores");
    write(local.collectionIniPath(), QByteArray("existing per-world Library bytes"));
    write(local.progressIniPath(), QByteArray("existing progress bytes"));
    require(!localRestart.createCollection(QStringLiteral("Independent change")).isEmpty(), "named collection mutation with existing Library/progress files succeeds");
    require(read(local.collectionIniPath()) == QByteArray("existing per-world Library bytes")
        && read(local.progressIniPath()) == QByteArray("existing progress bytes"), "existing Library/progress bytes remain untouched");

    const QString appearingPath = QDir(directory).filePath(QStringLiteral("appeared-externally.json"));
    NamedCollectionsStore beforeAppearance(appearingPath);
    write(appearingPath, QByteArray("external file must survive"));
    require(beforeAppearance.createCollection(QStringLiteral("Must not replace external file")).isEmpty(), "new external file after initial load is not overwritten");
    require(read(appearingPath) == QByteArray("external file must survive")
        && beforeAppearance.collections().isEmpty(), "appearance conflict preserves external bytes and memory");
    std::cout << "PASS writer failures, conflicts, and profile isolation\n";
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary isolated test root available");
    normalLifecycle(temporary.filePath(QStringLiteral("lifecycle.json")));
    rejectedMutations(temporary.filePath(QStringLiteral("rejections.json")));
    displayMetadataBoundary(temporary.filePath(QStringLiteral("display-boundaries.json")));
    corruptAndUnsupported(temporary.path());
    persistenceFailuresAndProfiles(temporary.path());
    adoptionKeepsVersionsAndIsIdempotent(temporary.path());
    std::cout << "NAMED_COLLECTIONS_STORE_OK " << assertions << " assertions\n";
    return 0;
}
