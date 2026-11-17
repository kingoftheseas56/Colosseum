#include <QtTest>

#include "engine/BiblioCatalogStore.h"
#include "engine/BiblioCatalog.h"
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include "engine/VaultIdentifier.h"
#include "engine/VaultIndex.h"
#include "reader/BookStores.h"
#include "engine/VaultConfig.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QFile>
#include <QTemporaryDir>
#include <QUuid>
#include <memory>

namespace {

BiblioWork work(const QString &id, const QString &title = QStringLiteral("The Example"),
                const QString &author = QStringLiteral("Jane A. Writer"))
{
    BiblioWork w;
    w.canonicalId = id;
    w.title = title;
    w.author = author;
    return w;
}

BiblioCatalogSnapshot snapshot(const QList<BiblioWork> &works)
{
    BiblioCatalogSnapshot s;
    s.capturedAt = QDateTime::currentDateTimeUtc();
    s.works = works;
    for (const auto &w : works)
        s.sources.append({w.canonicalId, {QStringLiteral("title"),
            QStringLiteral("openlibrary"), QStringLiteral("/works/") + w.canonicalId,
            s.capturedAt}});
    return s;
}

bool execute(const QString &path, const QString &sql)
{
    const QString name = QUuid::createUuid().toString();
    bool ok = false;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (db.open()) { QSqlQuery q(db); ok = q.exec(sql); }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

VaultIndex::FileRow book(const QString &id, const QString &filename)
{
    VaultIndex::FileRow row;
    row.id = id;
    row.kind = QStringLiteral("book");
    row.rootPath = QStringLiteral("D:/Books");
    row.path = row.rootPath + QLatin1Char('/') + id + QLatin1Char('/') + filename;
    row.groupKey = row.subtreePath = row.path;
    row.realName = filename;
    row.displayTitle = row.groupTitle = QFileInfo(filename).completeBaseName();
    row.format = QFileInfo(filename).suffix().toLower();
    row.size = 100;
    row.mtimeMs = 1;
    return row;
}

void wire(VaultIdentifier &identifier, QObject &owner, const BiblioCatalogStore &store)
{
    identifier.setBookCatalogLookup(&owner, [&store](const QString &title, const QString &author) {
        return store.exactWorkMatches(title, author);
    });
}

} // namespace

class BookFakeReply : public BiblioTransportReply {
public:
    using BiblioTransportReply::BiblioTransportReply;
    void cancel() override { cancelled = true; }
    bool cancelled = false;
};
class BookFakeTransport : public IBiblioTransport {
public:
    QByteArray apple = R"({"results":[{"trackId":123,"trackName":"The Example","artistName":"Jane A. Writer","kind":"ebook","artworkUrl100":"https://example.invalid/cover"}]})";
    QByteArray ol = R"({"docs":[]})";
    bool offline = false;
    bool olOffline = false;
    QList<QUrl> urls;
    int calls = 0;
    BiblioTransportReply *get(const QUrl &url, const QVariantMap &) override {
        ++calls; urls.append(url);
        const bool failed = offline || (olOffline && url.host() == QStringLiteral("openlibrary.org"));
        auto *reply = new BookFakeReply;
        const auto body = url.host() == QStringLiteral("itunes.apple.com") ? apple : ol;
        QTimer::singleShot(0, reply, [failed, reply, body] {
            if (!reply->cancelled) emit reply->finished(failed ? 0 : 200, body,
                failed ? QStringLiteral("offline") : QString());
        });
        return reply;
    }
};

class BoundedBookTransport : public IBiblioTransport {
public:
    std::unique_ptr<BiblioNetworkTransport> network;
    bool replayOnly = false;
    QHash<QUrl,QJsonObject> recordings;
    int calls = 0, active = 0;
    QString output;
    BiblioTransportReply *get(const QUrl &url, const QVariantMap &headers) override {
        const bool allowed = (url.host() == QStringLiteral("itunes.apple.com") && url.path() == QStringLiteral("/search"))
            || (url.host() == QStringLiteral("openlibrary.org") && url.path() == QStringLiteral("/search.json"));
        const int budget = qEnvironmentVariableIntValue("COLOSSEUM_BOOK_LIVE_BUDGET");
        if (!allowed) qFatal("Live book transport refused URL");
        if (calls >= (budget > 0 ? qMin(60,budget) : 60)) {
            auto *reply = new BookFakeReply;
            QTimer::singleShot(0, reply, [reply] { emit reply->finished(0, {}, QStringLiteral("replay-budget-exhausted")); });
            return reply;
        }
        const int number = ++calls; ++active;
        BiblioTransportReply *reply = nullptr;
        if (replayOnly) {
            // Fail closed: a missing recording must NEVER fall back to HTTP.
            const auto recorded = recordings.value(url, QJsonObject{{"status",0},{"error","no-recorded-response"}});
            auto *fake = new BookFakeReply;
            reply = fake;
            QTimer::singleShot(0, fake, [fake,recorded] {
                if (!fake->cancelled) emit fake->finished(recorded.value("status").toInt(),
                    recorded.value("body").toString().toUtf8(),recorded.value("error").toString());
            });
        } else {
            if (!network) network = std::make_unique<BiblioNetworkTransport>();
            reply = network->get(url, headers);
        }
        QObject::connect(reply, &BiblioTransportReply::finished, reply,
            [this, url, number](int status, const QByteArray &body, const QString &error) {
                --active;
                QFile file(output + QStringLiteral("/request-%1.json").arg(number));
                if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(QJsonObject{
                    {"url",url.toString()},{"status",status},{"error",error},{"body",QString::fromUtf8(body)}}).toJson());
            });
        return reply;
    }
};

class VaultBookIdentityTest : public QObject
{
    Q_OBJECT
private slots:
    void duplicatesJunkReplay();
    void epubSharedWorkAndProgress();
    void librarySiblingPreference_data();
    void librarySiblingPreference();
    void migratedSplitConvergence_data();
    void migratedSplitConvergence();
    void contextSiblingCopies();
    void epubConflictAndFallback();
    void liveReplay();
    void surnameSourceContradiction();
    void decidedRules_data();
    void decidedRules();
    void generalizedDecoration_data();
    void generalizedDecoration();
    void onlineLookup_data();
    void onlineLookup();
    void onlineOfflineRetries();
    void onlineVaultAdoptionAndRescan();
    void personalPdfsNeverGoOnline();
    void exactPairCountsWorksAndCarriesProvenance();
    void distinctWorksStayAmbiguous();
    void groupedInitialsCompareWithoutDroppingNames();
    void qualifiedVariantsAbstain_data();
    void qualifiedVariantsAbstain();
    void unavailableAndReadErrorDifferFromZero();
    void onlyActiveSnapshotIsRead();
    void filenameEvidence_data();
    void filenameEvidence();
    void titleOnlySurnameMustRemainUnambiguous();
    void underscorePrefixMustNotChooseBetweenWorks();
    void missingCatalogueGetsLookedUp();
    void copiesAndExistingChoicesRemainPhysical();
    void conflictingEmbeddedFactsAbstain();
    void revisionRetryGuards_data();
    void revisionRetryGuards();
    void staleMatchAndOwnerReplacementAreRejected();
};

void VaultBookIdentityTest::duplicatesJunkReplay()
{
    const QString input = qEnvironmentVariable("VAULT_JUNK_INPUT");
    if (input.isEmpty()) QSKIP("Copies-only explicit replay");
    const QString output = qEnvironmentVariable("VAULT_JUNK_OUTPUT");
    QVERIFY(!output.isEmpty()); QDir().mkpath(output);
    const QString indexPath = output + "/index-v1.sqlite", catPath = output + "/biblio-v1.sqlite";
    QVERIFY(QFile::copy(input + "/index-v1.sqlite", indexPath));
    QVERIFY(QFile::copy(input + "/biblio-v1.sqlite", catPath));
    QDir().mkpath(output + "/config");
    QVERIFY(QFile::copy(input + "/config.json", output + "/config/config.json"));
    BoundedBookTransport transport; transport.output = output;
    const auto recordedDir = qEnvironmentVariable("VAULT_JUNK_RECORDINGS");
    if (!recordedDir.isEmpty()) {
        transport.replayOnly = true;
        for (const auto& name : QDir(recordedDir).entryList({"request-*.json"}, QDir::Files)) {
            QFile f(recordedDir + '/' + name); QVERIFY(f.open(QIODevice::ReadOnly));
            const auto response = QJsonDocument::fromJson(f.readAll()).object();
            transport.recordings.insert(QUrl(response.value("url").toString()),response);
        }
    }
    BiblioCatalog catalog(catPath, &transport);
    VaultIndex index(indexPath); VaultConfig config(output + "/config");
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    identifier.setBookCatalogLookup(&catalog,
        [&catalog](const QString& t, const QString& a) { return catalog.exactWorkMatches(t,a); },
        [&catalog](const QString& t, const QString& a, const QString& f) { return catalog.onlineWorkMatches(t,a,f); },
        [&catalog](const BiblioIdentityCandidate& c) { return catalog.retainVaultWork(c); });
    QJsonArray decisions;
    for (const auto& row : index.rowsForKind("book")) {
        if (!row.identityId.isEmpty() && !VaultIdentifier::automaticPrivateEpub(row)) continue;
        if (row.identitySuppressed) continue;
        auto match = identifier.matchGroup(row.groupKey);
        QElapsedTimer clock; clock.start(); QTest::qWait(20);
        while (transport.active && clock.elapsed() < 45000) QTest::qWait(50);
        match = identifier.matchGroup(row.groupKey);
        if (match.adopted) QVERIFY(identifier.applyGroup(row.groupKey, match));
        decisions.append(QJsonObject{{"path",row.path},{"before",row.identityId},{"after",match.adopted ? match.sourceId : row.identityId}, {"reason",match.bookLookupReason}});
    }
    // Launch convergence must work for arbitrary libraries, without receipts.
    const int converged = identifier.autoIdentifyExisting();
    QCOMPARE(identifier.autoIdentifyExisting(), 0);
    decisions.append(QJsonObject{{"converged",converged}});
    const auto videos = index.rowsForKind("video");
    const auto parents = VaultIdentifier::physicalExtrasParents(videos);
    QJsonArray moved;
    for (const auto& row : videos) {
        if (!config.extrasParent(row.id).isEmpty() || config.extrasDeclined(row.id) || config.isHidden(row.id)) continue;
        const auto reason = VaultIdentifier::junkClipReason(row);
        if (parents.contains(row.id)) {
            QVERIFY(config.setExtrasParent({row.id}, parents.value(row.id)));
            moved.append(QJsonObject{{"path",row.path},{"destination","extras"},{"parent",parents.value(row.id)}, {"reason",reason.isEmpty() ? "bonus-folder-nearest-matched-ancestor" : reason}});
        } else if (!reason.isEmpty()) {
            config.setHidden(row.id,true);
            moved.append(QJsonObject{{"path",row.path},{"destination","Hidden"},{"reason",reason}});
        }
    }
    QFile file(output + "/replay.json"); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"books",decisions},{"moved",moved},{"requests",transport.calls}}).toJson());
    if (transport.replayOnly) QVERIFY(!transport.network);
    else QVERIFY(transport.calls <= 10);
}

void VaultBookIdentityTest::librarySiblingPreference_data()
{
    QTest::addColumn<QString>("embedded");
    QTest::newRow("plain") << "The Work";
    for (const auto &number : {QString("One"), QString("Five"), QString("Twenty"), QString("V"), QString("Fifth Book"), QString("Tenth Book")})
        QTest::newRow(qPrintable(number)) << ("The Work: Arbitrary Series: " + (number.endsWith("Book") ? number : "Book " + number));
    QTest::newRow("dance") << "A Dance with Dragons: A Song of Ice and Fire: Book Five";
}

void VaultBookIdentityTest::librarySiblingPreference()
{
    QFETCH(QString, embedded);
    const QString title = embedded.startsWith("A Dance") ? "A Dance With Dragons" : "The Work";
    QTemporaryDir d; VaultIndex index(d.filePath("index.sqlite"));
    auto epub = book("epub", title + " - George RR Martin.epub");
    epub.metadataSource = "EPUB"; epub.embeddedTitle = embedded; epub.author = "George R. R. Martin";
    auto mobi = book("mobi", title + " - George R. R. Martin.mobi");
    mobi.identityId = "biblio:X"; mobi.identityTitle = title; mobi.identitySource = "BIBLIO"; mobi.identityState = "adopted";
    QVERIFY(index.publish({epub, mobi}));
    QObject owner; VaultIdentifier identifier(&index, nullptr, nullptr, nullptr); int calls = 0;
    identifier.setBookCatalogLookup(&owner, [&calls](const QString&, const QString&) { ++calls; return BiblioIdentityLookup{}; },
        [&calls](const QString&, const QString&, const QString&) { ++calls; return BiblioIdentityLookup{}; });
    const auto match = identifier.matchGroup(epub.groupKey);
    QVERIFY2(match.adopted, qPrintable(match.bookLookupReason)); QCOMPARE(match.sourceId, QString("biblio:X")); QCOMPARE(calls, 0);
    QVERIFY(identifier.applyGroup(epub.groupKey, match));
    // A second, genuinely distinct identity must not be silently selected.
    auto pdf = book("pdf", title + " - George RR Martin.pdf");
    pdf.identityId = "biblio:Y"; pdf.identityTitle = title; pdf.identitySource = "BIBLIO";
    QVERIFY(index.upsertMany({epub, pdf}));
    const auto ambiguous = identifier.matchGroup(epub.groupKey);
    QVERIFY(!ambiguous.adopted); QCOMPARE(ambiguous.candidateCount, 2); QCOMPARE(calls, 0);
    QVERIFY(ambiguous.bookLookupWarning.contains("biblio:X")); QVERIFY(ambiguous.bookLookupWarning.contains("biblio:Y"));
}

void VaultBookIdentityTest::contextSiblingCopies()
{
    const QList<QStringList> cases{
        {"Lord of the Rings - 01 - The Fellowship of the Ring - J. R. R. Tolkien.epub", "Lord of the Rings - 01 - The Fellowship of the Ring", "J. R. R. Tolkien", "The Fellowship of the Ring", "The Fellowship of the Ring - J. R. R. Tolkien.mobi"},
        {"Star Wars_ Heir to the Empire - Timothy Zahn.epub", "Star Wars: The Thrawn Trilogy I: Heir to the Empire", "Timothy Zahn", "Heir to the Empire", "Heir to the Empire - Timothy Zahn.mobi"},
        {"Words of Radiance (Stormlight Archive, The) - Sanderson, Brandon.epub", "Words of Radiance (Stormlight Archive, The)", "Sanderson, Brandon", "Words of Radiance", "Words_of_Radiance_Stormlight_Archive_The_-_Sanders.mobi"}
    };
    for (const auto& c : cases) {
        QTemporaryDir d; VaultIndex index(d.filePath("index.sqlite"));
        auto epub = book("epub", c[0]); epub.metadataSource = "EPUB"; epub.embeddedTitle = c[1]; epub.author = c[2];
        auto mobi = book("mobi", c[4]); mobi.identityId = "biblio:X"; mobi.identitySource = "BIBLIO"; mobi.identityTitle = c[3];
        QVERIFY(index.publish({epub,mobi}));
        VaultIdentifier identifier(&index,nullptr,nullptr,nullptr);
        const auto match = identifier.matchGroup(epub.groupKey);
        QVERIFY2(match.adopted && match.sourceId == "biblio:X", qPrintable(c[0] + ": " + match.bookLookupReason));
    }
}

void VaultBookIdentityTest::migratedSplitConvergence_data()
{
    QTest::addColumn<QString>("shape");
    QTest::addColumn<int>("changes");
    QTest::newRow("live-openlibrary-no-receipt") << "live" << 1;
    QTest::newRow("manual") << "manual" << 0;
    QTest::newRow("unknown-provenance") << "unknown" << 0;
    QTest::newRow("other-source") << "other" << 0;
    QTest::newRow("two-snapshots") << "ambiguous" << 0;
    QTest::newRow("online-majority") << "majority" << 1;
    QTest::newRow("online-minority") << "minority" << 1;
    QTest::newRow("online-tie-lowest-id") << "tie" << 1;
}

void VaultBookIdentityTest::migratedSplitConvergence()
{
    QTemporaryDir d; VaultIndex index(d.filePath("index.sqlite"));
    auto original = book("epub", "A Feast for Crows - George RR Martin.epub");
    original.metadataSource = "EPUB"; original.embeddedTitle = "A Feast for Crows"; original.author = "George R. R. Martin";
    original.identityId = "epub:" + original.id; original.identitySource = "EPUB"; original.progressed = true;
    QFETCH(QString, shape); QFETCH(int, changes);
    BiblioCatalogStore store; QVERIFY(store.open(d.filePath("catalog.sqlite")));
    auto online = [&](QString id) {
        return BiblioIdentityCandidate{id, "A Feast for Crows", "George R. R. Martin", {},
            {{"title", "openlibrary", id.mid(QString("user:openlibrary:").size()), QDateTime::currentDateTimeUtc()}}};
    };
    const QString onlineId = "user:openlibrary:/works/OL257948W";
    if (shape != "unknown") QVERIFY(store.retainVaultWork(online(onlineId)));
    auto migrated = original; migrated.identityId = "biblio:" + onlineId; migrated.identitySource = "BIBLIO";
    migrated.identityState = "adopted"; migrated.identityTitle = original.embeddedTitle;
    auto sibling = book("mobi", "A Feast for Crows - George RR Martin.mobi");
    sibling.identityId = "biblio:ta-feast"; sibling.identityTitle = original.embeddedTitle; sibling.identitySource = "BIBLIO";
    sibling.identityState = "adopted";
    auto protectedRow = migrated; protectedRow.id = "old"; protectedRow.path += ".old"; protectedRow.groupKey = protectedRow.path;
    protectedRow.identityTitle = protectedRow.embeddedTitle = "An Older Work";
    auto pdf = sibling; pdf.id = "pdf"; pdf.path += ".pdf"; pdf.groupKey = pdf.path;
    if (shape == "manual") migrated.identityState = "manual";
    if (shape == "other") migrated.identitySource = "OTHER";
    if (shape == "ambiguous") pdf.identityId = "biblio:ta-other";
    const bool bothOnline = shape == "majority" || shape == "minority" || shape == "tie";
    if (bothOnline) {
        const QString target = "user:openlibrary:/works/OL000001W";
        QVERIFY(store.retainVaultWork(online(target)));
        sibling.identityId = "biblio:" + target; sibling.identityState = "adopted";
        pdf.identityId = shape == "minority" ? migrated.identityId : sibling.identityId;
    }
    QVERIFY(index.publish(shape == "tie" ? QList<VaultIndex::FileRow>{migrated,sibling,protectedRow}
        : QList<VaultIndex::FileRow>{migrated,sibling,pdf,protectedRow}));
    BookStores::ScopedStore personal; personal.useProfileRoot(d.filePath("personal"));
    personal.save("progress.json", BookStores::keyFor(original.path), QJsonObject{{"fraction",0.42}});
    personal.listSave("bookmarks.json", BookStores::keyFor(original.path), QJsonObject{{"id","saved"}});
    const auto progress = personal.readStore("progress.json"), bookmarks = personal.readStore("bookmarks.json");
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    QObject owner; wire(identifier, owner, store);
    QCOMPARE(identifier.autoIdentifyExisting(), changes);
    const auto after = index.rowsForGroup(original.groupKey).first();
    const QString expected = changes && shape != "minority" ? sibling.identityId : migrated.identityId;
    QCOMPARE(after.identityId, expected); QCOMPARE(after.id, original.id); QCOMPARE(after.path, original.path); QVERIFY(after.progressed);
    QCOMPARE(index.rowsForGroup(protectedRow.groupKey).first().identityId, protectedRow.identityId);
    QCOMPARE(personal.readStore("progress.json"), progress); QCOMPARE(personal.readStore("bookmarks.json"), bookmarks);
    QCOMPARE(identifier.autoIdentifyExisting(), 0);
    auto noSiblingOriginal = original; noSiblingOriginal.id = "alone";
    noSiblingOriginal.path += ".alone"; noSiblingOriginal.groupKey = noSiblingOriginal.path;
    noSiblingOriginal.identityId = "epub:alone"; noSiblingOriginal.embeddedTitle = "No Sibling";
    auto alone = noSiblingOriginal; alone.identityId = "biblio:alone"; alone.identitySource = "BIBLIO";
    alone.identityState = "adopted"; alone.identityTitle = "No Sibling";
    QVERIFY(index.upsertMany({alone}));
    QCOMPARE(identifier.autoIdentifyExisting(), 0);
    QCOMPARE(index.rowsForGroup(alone.groupKey).first().identityId, alone.identityId);
}

void VaultBookIdentityTest::epubSharedWorkAndProgress()
{
    QTemporaryDir d;
    BiblioCatalogStore store;
    QVERIFY(store.open(d.filePath("catalog.sqlite")));
    QVERIFY(store.publish(snapshot({work("example")})));
    VaultIndex index(d.filePath("index.sqlite"));
    auto epub = book("epub", "The Example - Jane A. Writer.epub");
    epub.metadataSource = "EPUB"; epub.embeddedTitle = "The Example"; epub.author = "Jane A. Writer";
    epub.identityId = "epub:" + epub.id; epub.identitySource = "EPUB"; epub.identityState.clear(); // legacy automatic rows in the real copy
    epub.progressed = true;
    auto mobi = book("mobi", "The Example - Jane A. Writer.mobi");
    QVERIFY(index.publish({epub, mobi}));
    BookStores::ScopedStore personal;
    personal.useProfileRoot(d.filePath("personal"));
    const auto key = BookStores::keyFor(epub.path);
    const QJsonObject saved{{"fraction", 0.42}, {"cfi", "saved-cfi"}};
    personal.save("progress.json", key, saved);
    personal.listSave("bookmarks.json", key, QJsonObject{{"id", "bookmark"}, {"cfi", "saved-cfi"}});
    const auto beforeProgress = personal.readStore("progress.json");
    const auto beforeBookmarks = personal.readStore("bookmarks.json");
    QObject owner;
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    wire(identifier, owner, store);
    QCOMPARE(identifier.autoIdentifyExisting(), 2);
    QCOMPARE(index.rowsForGroup(epub.groupKey).first().identityId, QString("biblio:example"));
    QCOMPARE(index.rowsForGroup(mobi.groupKey).first().identityId, QString("biblio:example"));
    const auto after = index.rowsForGroup(epub.groupKey).first();
    QCOMPARE(after.id, epub.id); QCOMPARE(after.path, epub.path); QVERIFY(after.progressed);
    QCOMPARE(personal.get("progress.json", BookStores::keyFor(after.path)), saved);
    QCOMPARE(personal.readStore("progress.json"), beforeProgress);
    QCOMPARE(personal.readStore("bookmarks.json"), beforeBookmarks);
    QCOMPARE(identifier.autoIdentifyExisting(), 0);
}

void VaultBookIdentityTest::epubConflictAndFallback()
{
    QTemporaryDir d;
    VaultIndex index(d.filePath("index.sqlite"));
    auto epub = book("epub", "Other Title - Jane A. Writer.epub");
    epub.metadataSource = "EPUB"; epub.embeddedTitle = "The Example"; epub.author = "Jane A. Writer";
    QVERIFY(index.publish({epub}));
    QObject owner;
    int calls = 0;
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    identifier.setBookCatalogLookup(&owner, [&calls](const QString&, const QString&) {
        ++calls; BiblioIdentityLookup result; result.status = BiblioIdentityLookup::Status::Available; return result;
    });
    auto match = identifier.matchGroup(epub.groupKey);
    QVERIFY(!match.adopted); QCOMPARE(match.bookLookupReason, QString("conflicting-file-facts")); QCOMPARE(calls, 0);
    epub.realName = "The Example - Jane A. Writer.epub";
    QVERIFY(index.upsertMany({epub}));
    match = identifier.matchGroup(epub.groupKey);
    QVERIFY(match.adopted); QCOMPARE(match.sourceId, "epub:" + epub.id);
    QVERIFY(identifier.applyGroup(epub.groupKey, match));
    identifier.autoIdentifyExisting(); const int once = calls;
    identifier.autoIdentifyExisting(); QCOMPARE(calls, once);
    auto changed = index.rowsForGroup(epub.groupKey).first();
    ++changed.mtimeMs; QVERIFY(index.upsertMany({changed}));
    identifier.autoIdentifyExisting(); QCOMPARE(calls, once + 1);
    const int afterEvidence = calls;
    for (const QString& source : {QString("BIBLIO"), QString("OTHER"), QString("EPUB")}) {
        epub.identityId = source == "EPUB" ? "epub:chosen" : source.toLower() + ":chosen";
        epub.identitySource = source; epub.identityState = source == "EPUB" ? "manual" : "adopted";
        QVERIFY(index.upsertMany({epub}));
        identifier.autoIdentifyExisting();
        QCOMPARE(index.rowsForGroup(epub.groupKey).first().identityId, epub.identityId);
    }
    QCOMPARE(calls, afterEvidence);
}

void VaultBookIdentityTest::liveReplay()
{
    const QString inputs = qEnvironmentVariable("COLOSSEUM_BOOK_LIVE_INPUT");
    if (inputs.isEmpty()) QSKIP("Explicit bounded live or offline replay only");
    const QString output = qEnvironmentVariable("COLOSSEUM_BOOK_LIVE_OUTPUT");
    QVERIFY(!output.isEmpty()); QDir().mkpath(output);
    auto hashFile = [](const QString &p) { QFile f(p); if (!f.open(QIODevice::ReadOnly)) return QByteArray();
        return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex(); };
    const auto catSource = inputs + QStringLiteral("/biblio-v1.sqlite");
    const auto indexSource = inputs + QStringLiteral("/index-v1.sqlite");
    const auto catHash = hashFile(catSource), indexHash = hashFile(indexSource);
    QVERIFY(!catHash.isEmpty()); QVERIFY(!indexHash.isEmpty());
    QTemporaryDir copy;
    const auto catPath = copy.filePath(QStringLiteral("catalog.sqlite"));
    const auto indexPath = copy.filePath(QStringLiteral("index.sqlite"));
    QVERIFY(QFile::copy(catSource,catPath)); QVERIFY(QFile::copy(indexSource,indexPath));
    BoundedBookTransport transport; transport.output = output;
    const QString recordings = qEnvironmentVariable("COLOSSEUM_BOOK_REPLAY_RESPONSES");
    QHash<QString,QJsonObject> expected;
    if (!recordings.isEmpty()) {
        transport.replayOnly = true;
        const auto files = QDir(recordings).entryList({"request-*.json"},QDir::Files);
        QVERIFY(!files.isEmpty());
        for (const auto &name : files) {
            QFile file(QDir(recordings).filePath(name)); QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonParseError error;
            const auto response = QJsonDocument::fromJson(file.readAll(),&error).object();
            QCOMPARE(error.error,QJsonParseError::NoError);
            const QUrl url(response.value("url").toString());
            QVERIFY(url.isValid()); QVERIFY(response.contains("status")); QVERIFY(response.contains("body"));
            if (transport.recordings.contains(url)) QCOMPARE(transport.recordings.value(url),response);
            transport.recordings.insert(url,response);
        }
        QFile baseline(QDir(recordings).filePath("replay.json")); QVERIFY(baseline.open(QIODevice::ReadOnly));
        for (const auto &value : QJsonDocument::fromJson(baseline.readAll()).object().value("files").toArray()) {
            const auto row = value.toObject(); expected.insert(row.value("filename").toString(),row);
        }
        QCOMPARE(expected.size(),17);
    }
    BiblioCatalog catalog(catPath, &transport); VaultIndex index(indexPath);
    VaultIdentifier identifier(&index,nullptr,nullptr,nullptr);
    identifier.setBookCatalogLookup(&catalog,
        [&catalog](const QString &t,const QString &a) { return catalog.exactWorkMatches(t,a); },
        [&catalog](const QString &t,const QString &a,const QString &f) { return catalog.onlineWorkMatches(t,a,f); },
        [&catalog](const BiblioIdentityCandidate &c) { return catalog.retainVaultWork(c); });
    QJsonArray rows;
    for (const auto &row : index.rowsForKind(QStringLiteral("book"))) {
        if (!row.identityId.isEmpty() || row.identitySuppressed) continue;
        auto match = identifier.matchGroup(row.groupKey);
        // One burst, then stop on its actual failure rather than repeatedly
        // scanning while offline. No refresh, artwork fetch, or live profile IO.
        QElapsedTimer clock; clock.start();
        QTest::qWait(20);
        while (transport.active && clock.elapsed() < 45000) QTest::qWait(50);
        match = identifier.matchGroup(row.groupKey);
        const bool applied = match.adopted && identifier.applyGroup(row.groupKey,match);
        rows.append(QJsonObject{{"filename",row.realName},{"adopted",applied},
            {"reason",match.bookLookupReason},{"title",match.title},{"author",match.bookWork.author},
            {"sourceId",match.sourceId},{"candidateCount",match.candidateCount}});
    }
    const bool unchanged = hashFile(catSource)==catHash && hashFile(indexSource)==indexHash;
    QFile file(output + QStringLiteral("/replay.json")); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"files",rows},{"requests",transport.calls},
        {"sourcesUnchanged",unchanged},{"catalogHash",QString::fromLatin1(catHash)},
        {"indexHash",QString::fromLatin1(indexHash)}}).toJson());
    QCOMPARE(rows.size(),17); QVERIFY(unchanged); QVERIFY(transport.calls <= 60);
    if (transport.replayOnly) {
        QVERIFY(!transport.network); // no HTTP transport was even constructed
        int adopted = 0;
        for (const auto &value : rows) {
            const auto row = value.toObject();
            const auto filename = row.value("filename").toString();
            QVERIFY2(expected.contains(filename),qPrintable(filename));
            QVERIFY2(row == expected.take(filename),qPrintable(filename));
            if (row.value("adopted").toBool()) ++adopted;
        }
        QVERIFY(expected.isEmpty()); QCOMPARE(adopted,8); QCOMPARE(rows.size()-adopted,9);
    }
}

void VaultBookIdentityTest::surnameSourceContradiction()
{
    QTemporaryDir dir; BookFakeTransport transport;
    transport.apple = R"({"results":[{"trackId":9,"trackName":"Words of Radiance","artistName":"Brandon Sanderson"}]})";
    transport.ol = R"({"docs":[{"key":"/works/OL1W","title":"Words of Radiance","author_name":["Other Sanders"]}]})";
    BiblioCatalog catalog(dir.filePath("books.sqlite"),&transport);
    QSignalSpy done(&catalog,&BiblioCatalog::vaultLookupFinished);
    catalog.onlineWorkMatches("Words of Radiance","Sanders");
    QTRY_COMPARE(done.size(),1);
    const auto result = catalog.onlineWorkMatches("Words of Radiance","Sanders");
    QCOMPARE(result.candidates.size(),2);
    QVERIFY(result.candidates.first().canonicalId != result.candidates.last().canonicalId);
}

void VaultBookIdentityTest::decidedRules_data()
{
    QTest::addColumn<QString>("filename");
    QTest::addColumn<QString>("sourceTitle");
    QTest::addColumn<QString>("sourceAuthor");
    QTest::addColumn<QString>("olTitle");
    QTest::addColumn<bool>("olOffline");
    QTest::addColumn<bool>("duplicate");
    QTest::addColumn<bool>("differentAuthor");
    QTest::addColumn<QString>("expectedTitle");
    QTest::addColumn<QString>("expectedId");
    QTest::newRow("thrawn") << "Star Wars_ Dark Force Rising - Timothy Zahn.mobi" << "Dark Force Rising: Star Wars Legends (The Thrawn Trilogy)" << "Timothy Zahn" << "Star Wars - Thrawn Trilogy - Dark Force Rising" << false << false << false << "Dark Force Rising" << "user:openlibrary:/works/OL1W";
    QTest::newRow("box-set") << "Star Wars_ Heir to the Empire - Timothy Zahn.mobi" << "Heir to the Empire: Star Wars Legends 20th Anniversary Edition Box Set" << "Timothy Zahn" << "" << false << false << false << "" << "";
    QTest::newRow("surname-title-only") << "Words_of_Radiance_Stormlight_Archive_The_-_Sanders.mobi" << "Words of Radiance (The Stormlight Archive, Book 2)" << "Brandon Sanderson" << "" << false << false << false << "Words of Radiance" << "user:apple:apple:9";
    QTest::newRow("surname-wrong") << "Words_of_Radiance_Stormlight_Archive_The_-_Sanders.mobi" << "Words of Radiance (The Stormlight Archive, Book 2)" << "Other Writer" << "" << false << false << false << "" << "";
    QTest::newRow("filename-series") << "Example Cycle - 2 - The Work - Jane Writer.mobi" << "The Work (Example Cycle, Book 2)" << "Jane Writer" << "" << false << false << false << "The Work" << "user:apple:apple:9";
    QTest::newRow("fraction-not-book-number") << "A Dance With Dragons - George RR Martin.mobi" << "A Dance With Dragons [1/3]" << "George R. R. Martin" << "" << false << false << false << "" << "";
    QTest::newRow("not-substring") << "Dune - Frank Herbert.mobi" << "Dune Messiah" << "Frank Herbert" << "" << false << false << false << "" << "";
    QTest::newRow("graphic") << "The Hobbit - J. R. R. Tolkien.mobi" << "The Hobbit: Graphic Novel" << "J. R. R. Tolkien" << "" << false << false << false << "" << "";
    QTest::newRow("summary") << "A Game of Thrones - George RR Martin.mobi" << "A Game of Thrones: A Song of Ice and Fire Summary" << "George R. R. Martin" << "" << false << false << false << "" << "";
    for (const auto &mode : {QStringLiteral("empty"),QStringLiteral("agree"),QStringLiteral("offline"),QStringLiteral("authors")}) {
        QTest::newRow(qPrintable("clash-" + mode)) << "A Clash of Kings - George RR Martin.mobi" << "A Clash of Kings" << "George R. R. Martin" << (mode == "agree" ? "A Clash of Kings" : "") << (mode == "offline") << true << (mode == "authors") << (mode == "authors" ? "" : "A Clash of Kings") << (mode == "authors" ? "" : mode == "agree" ? "user:openlibrary:/works/OL1W" : "user:apple:apple:9");
    }
}

void VaultBookIdentityTest::decidedRules()
{
    QFETCH(QString, filename); QFETCH(QString, sourceTitle); QFETCH(QString, sourceAuthor);
    QFETCH(QString, olTitle); QFETCH(bool, olOffline); QFETCH(bool, duplicate);
    QFETCH(bool, differentAuthor); QFETCH(QString, expectedTitle); QFETCH(QString, expectedId);
    QTemporaryDir dir; BookFakeTransport transport; transport.olOffline = olOffline;
    QJsonArray listings{QJsonObject{{"trackId",9},{"trackName",sourceTitle},{"artistName",sourceAuthor}}};
    if (duplicate) listings.append(QJsonObject{{"trackId",100},{"trackName",sourceTitle},{"artistName",differentAuthor ? QStringLiteral("Other Writer") : sourceAuthor}});
    transport.apple = QJsonDocument(QJsonObject{{"results",listings}}).toJson();
    QJsonArray docs;
    if (!olTitle.isEmpty()) docs.append(QJsonObject{{"key","/works/OL1W"},{"title",olTitle},{"author_name",QJsonArray{sourceAuthor}}});
    transport.ol = QJsonDocument(QJsonObject{{"docs",docs}}).toJson();
    const auto path = dir.filePath("books.sqlite");
    { BiblioCatalogStore store; QVERIFY(store.open(path)); QVERIFY(store.publish(snapshot({work("unrelated","Unrelated")}))); }
    BiblioCatalog catalog(path,&transport); VaultIndex index(dir.filePath("index.sqlite"));
    const auto row = book("rules",filename); QVERIFY(index.publish({row}));
    VaultIdentifier identifier(&index,nullptr,nullptr,nullptr);
    identifier.setBookCatalogLookup(&catalog,
        [&catalog](const QString &t,const QString &a) { return catalog.exactWorkMatches(t,a); },
        [&catalog](const QString &t,const QString &a,const QString &f) { return catalog.onlineWorkMatches(t,a,f); },
        [&catalog](const BiblioIdentityCandidate &c) { return catalog.retainVaultWork(c); });
    QSignalSpy done(&catalog,&BiblioCatalog::vaultLookupFinished);
    identifier.matchGroup(row.groupKey);
    QTRY_VERIFY(!done.isEmpty()); QTest::qWait(30);
    const auto result = identifier.matchGroup(row.groupKey);
    QCOMPARE(result.adopted,!expectedTitle.isEmpty());
    if (expectedTitle.isEmpty()) return;
    QCOMPARE(result.title,expectedTitle); QCOMPARE(result.bookWork.canonicalId,expectedId);
    QCOMPARE(result.bookWork.author,sourceAuthor);
    if (filename.contains("Sanders")) for (const auto &url : transport.urls) QVERIFY(!url.toString().contains("Sanders"));
    QVERIFY(identifier.applyGroup(row.groupKey,result));
    BiblioCatalog restarted(path,&transport);
    const auto local = restarted.exactWorkMatches(expectedTitle,sourceAuthor);
    QCOMPARE(local.candidates.size(),1); QCOMPARE(local.candidates.first().canonicalId,expectedId);
    bool raw = false;
    for (const auto &s : local.candidates.first().sources) if (s.field == "rawTitle" && s.sourceId == sourceTitle) raw = true;
    QVERIFY(raw);
}

void VaultBookIdentityTest::generalizedDecoration_data()
{
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("author");
    QTest::addColumn<QString>("filename");
    QTest::addColumn<QString>("sourceTitle");
    QTest::addColumn<bool>("accepted");
    QTest::newRow("mistborn-book-number") << "The Final Empire" << "Brandon Sanderson" << "The Final Empire - Brandon Sanderson.epub" << "The Final Empire (Mistborn, Book 1)" << true;
    QTest::newRow("dune-chronicles") << "Dune" << "Frank Herbert" << "Dune - Frank Herbert.epub" << "Dune (Dune Chronicles, Book 1)" << true;
    QTest::newRow("expanse-hash") << "Leviathan Wakes" << "James S. A. Corey" << "Leviathan Wakes - James S. A. Corey.epub" << "Leviathan Wakes (The Expanse #1)" << true;
    for (const auto &label : {QStringLiteral("Arbitrary Name, Vol. 2"), QStringLiteral("Arbitrary Name Volume 2"), QStringLiteral("Arbitrary Name, 2"), QStringLiteral("Arbitrary Chronicles"), QStringLiteral("Arbitrary Cycle"), QStringLiteral("Arbitrary Sequence"), QStringLiteral("Arbitrary Quartet"), QStringLiteral("Arbitrary Duology")}) {
        QTest::newRow(qPrintable(label)) << "The Work" << "Jane Writer" << "The Work - Jane Writer.epub" << ("The Work (" + label + ")") << true;
    }
    QTest::newRow("filename-context-only") << "The Work" << "Jane Writer" << "Arbitrary Name - The Work - Jane Writer.epub" << "Arbitrary Name: The Work" << true;
    QTest::newRow("numbered-derivative") << "Dune" << "Frank Herbert" << "Dune - Frank Herbert.epub" << "Dune: The Graphic Novel, Book 1" << false;
    QTest::newRow("undecorated-subtitle") << "Leviathan Wakes" << "James S. A. Corey" << "Leviathan Wakes - James S. A. Corey.epub" << "Leviathan Wakes: A Novel About Something Else" << false;
    QTest::newRow("series-fraction") << "The Work" << "Jane Writer" << "The Work - Jane Writer.epub" << "The Work (Arbitrary Chronicles 1/3)" << false;
    QTest::newRow("bare-number-not-series") << "The Work" << "Jane Writer" << "The Work - Jane Writer.epub" << "The Work (Arbitrary Name 2)" << false;
    QTest::newRow("series-word-not-substring") << "The Work" << "Jane Writer" << "The Work - Jane Writer.epub" << "The Work (Bicycles)" << false;
}

void VaultBookIdentityTest::generalizedDecoration()
{
    QFETCH(QString, title); QFETCH(QString, author); QFETCH(QString, filename);
    QFETCH(QString, sourceTitle); QFETCH(bool, accepted);
    QTemporaryDir dir; BookFakeTransport transport;
    transport.apple = QJsonDocument(QJsonObject{{"results", QJsonArray{
        QJsonObject{{"trackId",9},{"trackName",sourceTitle},{"artistName",author}}}}}).toJson();
    const auto path = dir.filePath("books.sqlite");
    BiblioCatalog catalog(path, &transport);
    QSignalSpy done(&catalog, &BiblioCatalog::vaultLookupFinished);
    // Exercise catalogue matching with the filename's title/context. EPUB's
    // separate embedded-metadata adoption contract remains unchanged.
    const auto filenameTitle = QFileInfo(filename).completeBaseName().section(" - ",0,-2);
    catalog.onlineWorkMatches(title,author,filenameTitle);
    QTRY_COMPARE(done.size(),1);
    const auto result = catalog.onlineWorkMatches(title,author,filenameTitle);
    QCOMPARE(result.candidates.size(),accepted ? 1 : 0);
    QCOMPARE(transport.calls,2);
    if (!accepted) return;
    const auto candidate = result.candidates.first();
    QCOMPARE(candidate.title,title); QCOMPARE(candidate.author,author);
    QVERIFY(catalog.retainVaultWork(candidate));
    BiblioCatalog restarted(path,&transport);
    const auto local = restarted.exactWorkMatches(title,author);
    QCOMPARE(local.candidates.size(),1);
    QCOMPARE(local.candidates.first().title,title);
    QCOMPARE(local.candidates.first().canonicalId,candidate.canonicalId);
    bool raw = false;
    for (const auto &s : local.candidates.first().sources)
        if (s.field == "rawTitle" && s.sourceId == sourceTitle) raw = true;
    QVERIFY(raw);
}

void VaultBookIdentityTest::onlineLookup_data()
{
    QTest::addColumn<QByteArray>("apple");
    QTest::addColumn<QByteArray>("ol");
    QTest::addColumn<int>("count");
    const QByteArray apple = BookFakeTransport().apple;
    const QByteArray emptyApple = R"({"results":[]})";
    const QByteArray emptyOl = R"({"docs":[]})";
    const QByteArray agree = R"({"docs":[{"key":"/works/OL1W","title":"The Example","author_name":["Jane A. Writer"]}]})";
    QTest::newRow("unique-apple") << apple << emptyOl << 1;
    QTest::newRow("sources-agree") << apple << agree << 1;
    const QByteArray several = R"({"docs":[{"key":"/works/OL1W","title":"The Example","author_name":["Jane A. Writer"]},{"key":"/works/OL2W","title":"The Example","author_name":["Jane A. Writer"]}]})";
    QTest::newRow("apple-anchors-several-ol-keys") << apple << several << 1;
    QTest::newRow("several-apple-editions-collapse") << QByteArray(R"({"results":[{"trackId":123,"trackName":"The Example","artistName":"Jane A. Writer"},{"trackId":124,"trackName":"The Example","artistName":"Jane A. Writer"}]})") << agree << 1;
    QTest::newRow("sources-disagree") << apple << QByteArray(R"({"docs":[{"key":"/works/OL2W","title":"The Example","author_name":["Other Author"]}]})") << 2;
    QTest::newRow("different-title") << QByteArray(R"({"results":[{"trackId":9,"trackName":"Another Book","artistName":"Jane A. Writer"}]})") << emptyOl << 0;
    QTest::newRow("complete-work-not-part-one-of-three") << emptyApple << QByteArray(R"({"docs":[{"key":"/works/OL1W","title":"The Example","author_name":["Jane A. Writer"]},{"key":"/works/OL2W","title":"The Example [1/3]","author_name":["Jane A. Writer"]}]})") << 1;
    QTest::newRow("multiple-ol-works") << emptyApple << QByteArray(R"({"docs":[{"key":"/works/OL1W","title":"The Example","author_name":["Jane A. Writer"]},{"key":"/works/OL2W","title":"The Example","author_name":["Jane A. Writer"]}]})") << 2;
}

void VaultBookIdentityTest::onlineLookup()
{
    QFETCH(QByteArray, apple); QFETCH(QByteArray, ol); QFETCH(int, count);
    QTemporaryDir dir;
    BookFakeTransport transport; transport.apple = apple; transport.ol = ol;
    const auto path = dir.filePath(QStringLiteral("books.sqlite"));
    BiblioCatalog catalog(path, &transport);
    QSignalSpy done(&catalog, &BiblioCatalog::vaultLookupFinished);
    auto result = catalog.onlineWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane A. Writer"));
    QCOMPARE(result.status, BiblioIdentityLookup::Status::Unavailable);
    QTRY_COMPARE(done.size(), 1);
    result = catalog.onlineWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane A. Writer"));
    QCOMPARE(result.status, BiblioIdentityLookup::Status::Available);
    QCOMPARE(result.candidates.size(), count);
    QCOMPARE(transport.calls, 2);
    if (count != 1) {
        catalog.onlineWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane A. Writer"));
        QCOMPARE(transport.calls, 2); // session-negative/ambiguity cache
        return;
    }
    QVERIFY(catalog.retainVaultWork(result.candidates.first()));
    BiblioCatalogStore store; QVERIFY(store.open(path));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("unrelated"), QStringLiteral("Unrelated"))})));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("new"), QStringLiteral("New Snapshot"))})));
    BiblioCatalog restarted(path, &transport);
    const auto local = restarted.exactWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane A. Writer"));
    QCOMPARE(local.candidates.size(), 1);
    QCOMPARE(local.candidates.first().canonicalId, result.candidates.first().canonicalId);
    QCOMPARE(local.candidates.first().coverUrl, result.candidates.first().coverUrl);
    const auto expectedSource = result.candidates.first().canonicalId.startsWith("user:openlibrary:")
        ? QStringLiteral("/works/OL1W") : QStringLiteral("apple:123");
    QCOMPARE(local.candidates.first().sources.first().sourceId, expectedSource);
    QCOMPARE(transport.calls, 2);
}

void VaultBookIdentityTest::personalPdfsNeverGoOnline()
{
    QTemporaryDir dir; BookFakeTransport transport;
    BiblioCatalog catalog(dir.filePath(QStringLiteral("books.sqlite")), &transport);
    VaultIndex index(dir.filePath(QStringLiteral("vault.sqlite")));
    VaultIdentifier identifier(&index,nullptr,nullptr,nullptr);
    int lookups = 0;
    identifier.setBookCatalogLookup(&catalog,
        [&catalog,&lookups](const QString &t,const QString &a) { ++lookups; return catalog.exactWorkMatches(t,a); },
        [&catalog](const QString &t,const QString &a,const QString &f) { return catalog.onlineWorkMatches(t,a,f); });
    const QStringList names = {QStringLiteral("158297294x.pdf"),
        QStringLiteral("31459_98aec97b2c764e6c831f6963f8441f27.pdf"), QStringLiteral("Insurance frauds.pdf"),
        QStringLiteral("New Doc 12.pdf"), QStringLiteral("New Doc 12Page 1.pdf"),
        QStringLiteral("SpiceJet_E-ticket_PNR B8BUGG - 16 Sep 2015 Mumbai-Hyderabad for MR. GVHV (1).pdf"),
        QStringLiteral("Untitled 1.pdf"),QStringLiteral("Warranty.pdf"),QStringLiteral("Welcome to HDFC Bank NetBanking.pdf")};
    for (const auto &name : names) {
        const auto row = book(name,name); QVERIFY(index.publish({row}));
        const auto result = identifier.matchGroup(row.groupKey);
        QVERIFY(!result.adopted);
        QCOMPARE(result.bookLookupReason,QStringLiteral("no-complete-title-author-evidence"));
    }
    QTest::qWait(10); QCOMPARE(lookups,0); QCOMPARE(transport.calls,0);
}

void VaultBookIdentityTest::onlineVaultAdoptionAndRescan()
{
    QTemporaryDir dir; BookFakeTransport transport;
    const auto catPath = dir.filePath(QStringLiteral("books.sqlite"));
    { BiblioCatalogStore store; QVERIFY(store.open(catPath));
      QVERIFY(store.publish(snapshot({work(QStringLiteral("other"), QStringLiteral("Other"))}))); }
    BiblioCatalog catalog(catPath, &transport);
    VaultIndex index(dir.filePath(QStringLiteral("vault.sqlite")));
    const auto row = book(QStringLiteral("vault:online"), QStringLiteral("The Example - Jane A. Writer.mobi"));
    QVERIFY(index.publish({row}));
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    identifier.setBookCatalogLookup(&catalog,
        [&catalog](const QString &t, const QString &a) { return catalog.exactWorkMatches(t,a); },
        [&catalog](const QString &t, const QString &a,const QString &f) { return catalog.onlineWorkMatches(t,a,f); },
        [&catalog](const BiblioIdentityCandidate &c) { return catalog.retainVaultWork(c); });
    connect(&catalog, &BiblioCatalog::vaultLookupFinished, &identifier, &VaultIdentifier::retryBookCatalog);
    const auto pending = identifier.matchGroup(row.groupKey);
    QCOMPARE(pending.bookLookupReason, QStringLiteral("catalogue-unavailable"));
    QTRY_VERIFY(!index.rowsForGroup(row.groupKey).first().identityId.isEmpty());
    const auto adopted = index.rowsForGroup(row.groupKey).first();
    QCOMPARE(adopted.identityTitle, QStringLiteral("The Example"));
    QCOMPARE(adopted.identityCoverUrl, QStringLiteral("https://example.invalid/cover"));
    QCOMPARE(transport.calls, 2);
    // Fresh index simulates a second scan, with no embedded or adopted facts.
    VaultIndex second(dir.filePath(QStringLiteral("second.sqlite"))); QVERIFY(second.publish({row}));
    VaultIdentifier rescan(&second, nullptr, nullptr, nullptr);
    rescan.setBookCatalogLookup(&catalog, [&catalog](const QString &t, const QString &a) {
        return catalog.exactWorkMatches(t,a);
    });
    QCOMPARE(rescan.autoIdentifyExisting(), 1);
    QCOMPARE(transport.calls, 2);
}

void VaultBookIdentityTest::onlineOfflineRetries()
{
    QTemporaryDir dir; BookFakeTransport transport; transport.offline = true;
    BiblioCatalog catalog(dir.filePath(QStringLiteral("books.sqlite")), &transport);
    QSignalSpy done(&catalog, &BiblioCatalog::vaultLookupFinished);
    catalog.onlineWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane A. Writer"));
    QTRY_VERIFY(!done.isEmpty());
    auto result = catalog.onlineWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane A. Writer"));
    QCOMPARE(result.status, BiblioIdentityLookup::Status::Unavailable);
    transport.offline = false;
    QTest::qWait(1100);
    catalog.onlineWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane A. Writer"));
    QTRY_VERIFY(done.size() >= 2);
    result = catalog.onlineWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane A. Writer"));
    QCOMPARE(result.status, BiblioIdentityLookup::Status::Available);
    QCOMPARE(result.candidates.size(), 1);
}

void VaultBookIdentityTest::exactPairCountsWorksAndCarriesProvenance()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    auto first = work(QStringLiteral("ol1w"));
    first.editions = {{QStringLiteral("ol1w:ebook:en"), QStringLiteral("en"), true,
        300, {}, {}, QStringLiteral("ebook")},
        {QStringLiteral("ol1w:print:en"), QStringLiteral("en"), true,
        400, {}, {}, QStringLiteral("print")}};
    auto s = snapshot({first, work(QStringLiteral("ol2w"), first.title,
        QStringLiteral("Other Complete Author"))});
    s.sources.append({first.canonicalId, {QStringLiteral("coverUrl"),
        QStringLiteral("apple"), QStringLiteral("apple:12"), s.capturedAt}});
    QVERIFY(store.publish(s));
    const auto found = store.exactWorkMatches(QStringLiteral("THE EXAMPLE"),
        QStringLiteral("Jane A Writer"));
    QCOMPARE(found.status, BiblioIdentityLookup::Status::Available);
    QCOMPARE(found.candidates.size(), 1);
    QCOMPARE(found.candidates.first().canonicalId, first.canonicalId);
    QCOMPARE(found.candidates.first().sources.size(), 2);
    QCOMPARE(store.exactWorkMatches(first.title, QStringLiteral("Writer")).candidates.size(), 0);
    QCOMPARE(store.exactWorkMatches(first.title, QStringLiteral("Different Author")).candidates.size(), 0);
    const auto quoted = store.exactWorkMatches(QStringLiteral("' OR 1=1 --"), first.author);
    QCOMPARE(quoted.status, BiblioIdentityLookup::Status::Available);
    QVERIFY(quoted.candidates.isEmpty());
    QVERIFY(store.publish(snapshot({work(QStringLiteral("partial"), first.title, QStringLiteral("Sanders"))})));
    QVERIFY(store.exactWorkMatches(first.title, QStringLiteral("Sanders")).candidates.isEmpty());
    QVERIFY(store.publish(snapshot({work(QStringLiteral("unicode"), QString::fromUtf8("Caf\xc3\xa9"))})));
    QVERIFY(store.exactWorkMatches(QStringLiteral("Caf"), first.author).candidates.isEmpty());
}

void VaultBookIdentityTest::distinctWorksStayAmbiguous()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("ol1w")), work(QStringLiteral("ol2w"))})));
    QCOMPARE(store.exactWorkMatches(QStringLiteral("The Example"),
        QStringLiteral("Jane A. Writer")).candidates.size(), 2);
}

void VaultBookIdentityTest::groupedInitialsCompareWithoutDroppingNames()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("initials"),
        QStringLiteral("A Dance With Dragons"), QStringLiteral("George R. R. Martin"))})));
    QCOMPARE(store.exactWorkMatches(QStringLiteral("A Dance With Dragons"),
        QStringLiteral("George RR Martin")).candidates.size(), 1);
    QCOMPARE(store.exactWorkMatches(QStringLiteral("A Dance With Dragons"),
        QStringLiteral("George R.R. Martin")).candidates.size(), 1);
    QVERIFY(store.exactWorkMatches(QStringLiteral("A Dance With Dragons"),
        QStringLiteral("George R. S. Martin")).candidates.isEmpty());
    QVERIFY(store.exactWorkMatches(QStringLiteral("A Dance With Dragons"),
        QStringLiteral("George RR Marton")).candidates.isEmpty());
}

void VaultBookIdentityTest::qualifiedVariantsAbstain_data()
{
    QTest::addColumn<QString>("qualified");
    for (const auto &suffix : {" (Abridged)", " (Unabridged)", " (Illustrated)",
             " Illustrated by the Author", " (Anniversary Edition)", " Anthology",
             " Collection", " Omnibus", " [Edition 2]", " (Book Two)", " (Other subtitle)"})
        QTest::newRow(suffix) << QStringLiteral("The Example") + QString::fromLatin1(suffix);
}

void VaultBookIdentityTest::qualifiedVariantsAbstain()
{
    QFETCH(QString, qualified);
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("bare")),
        work(QStringLiteral("qualified"), qualified)})));
    const auto bare = store.exactWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane A Writer"));
    QCOMPARE(bare.candidates.size(), 1);
    QCOMPARE(bare.candidates.first().canonicalId, QStringLiteral("bare"));
    const auto variant = store.exactWorkMatches(qualified, QStringLiteral("Jane A Writer"));
    QCOMPARE(variant.status, BiblioIdentityLookup::Status::Available);
    QVERIFY(variant.candidates.isEmpty());
}

void VaultBookIdentityTest::unavailableAndReadErrorDifferFromZero()
{
    BiblioCatalogStore sealed;
    QCOMPARE(sealed.exactWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane Writer")).status,
        BiblioIdentityLookup::Status::Unavailable);
    QTemporaryDir dir;
    const QString corruptPath = dir.filePath(QStringLiteral("corrupt.sqlite"));
    {
        QFile corrupt(corruptPath);
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("not a SQLite database");
    }
    BiblioCatalogStore corruptStore;
    QVERIFY(!corruptStore.open(corruptPath));
    QCOMPARE(corruptStore.exactWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane Writer")).status,
        BiblioIdentityLookup::Status::ReadError);
    const QString path = dir.filePath(QStringLiteral("books.sqlite"));
    BiblioCatalogStore store;
    QVERIFY(store.open(path));
    QCOMPARE(store.exactWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane Writer")).status,
        BiblioIdentityLookup::Status::Unavailable);
    QVERIFY(store.publish(snapshot({work(QStringLiteral("one"))})));
    const auto zero = store.exactWorkMatches(QStringLiteral("Absent"), QStringLiteral("Jane Writer"));
    QCOMPARE(zero.status, BiblioIdentityLookup::Status::Available);
    QVERIFY(zero.candidates.isEmpty());
    QVERIFY(execute(path, QStringLiteral("drop table work_sources")));
    const auto broken = store.exactWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane Writer"));
    QCOMPARE(broken.status, BiblioIdentityLookup::Status::ReadError);
    QVERIFY(!broken.warning.isEmpty());
    QVERIFY(broken.candidates.isEmpty());
}

void VaultBookIdentityTest::onlyActiveSnapshotIsRead()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("old"))})));
    const auto old = store.exactWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane A Writer"));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("new"), QStringLiteral("Another Title"))})));
    const auto absent = store.exactWorkMatches(QStringLiteral("The Example"), QStringLiteral("Jane A Writer"));
    QCOMPARE(absent.status, BiblioIdentityLookup::Status::Available);
    QVERIFY(absent.candidates.isEmpty());
    QVERIFY(absent.snapshotId > old.snapshotId);
    QCOMPARE(store.exactWorkMatches(QStringLiteral("Another Title"), QStringLiteral("Jane A Writer"))
        .candidates.first().canonicalId, QStringLiteral("new"));
}

void VaultBookIdentityTest::filenameEvidence_data()
{
    QTest::addColumn<QString>("filename");
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("author");
    QTest::addColumn<bool>("adopts");
    QTest::newRow("clash-mobi") << QStringLiteral("A Clash of Kings - George R. R. Martin.mobi") << QStringLiteral("A Clash of Kings") << QStringLiteral("George RR Martin") << true;
    QTest::newRow("clash-pdf") << QStringLiteral("A Clash of Kings - George R. R. Martin.pdf") << QStringLiteral("A Clash of Kings") << QStringLiteral("George RR Martin") << true;
    QTest::newRow("dance-mobi") << QStringLiteral("A Dance With Dragons - George RR Martin.mobi") << QStringLiteral("A Dance With Dragons") << QStringLiteral("George R. R. Martin") << true;
    QTest::newRow("dance-pdf") << QStringLiteral("A Dance With Dragons - George RR Martin.pdf") << QStringLiteral("A Dance With Dragons") << QStringLiteral("George R. R. Martin") << true;
    QTest::newRow("thrawn-heir") << QStringLiteral("Star Wars_ Heir to the Empire - Timothy Zahn.mobi") << QStringLiteral("Heir to the Empire") << QStringLiteral("Timothy Zahn") << true;
    QTest::newRow("thrawn-dark") << QStringLiteral("Star Wars_ Dark Force Rising - Timothy Zahn.mobi") << QStringLiteral("Dark Force Rising") << QStringLiteral("Timothy Zahn") << true;
    QTest::newRow("thrawn-last") << QStringLiteral("Star Wars_ The Last Command - Timothy Zahn.mobi") << QStringLiteral("The Last Command") << QStringLiteral("Timothy Zahn") << true;
    QTest::newRow("radiance-short-author") << QStringLiteral("Words_of_Radiance_Stormlight_Archive_The_-_Sanders.mobi") << QStringLiteral("Words of Radiance") << QStringLiteral("Brandon Sanderson") << true;
    QTest::newRow("wrong-martin") << QStringLiteral("A Dance With Dragons - George R. S. Martin.mobi") << QStringLiteral("A Dance With Dragons") << QStringLiteral("George R. R. Martin") << false;
    QTest::newRow("wrong-zahn") << QStringLiteral("Star Wars_ Heir to the Empire - Timothy Wrong.mobi") << QStringLiteral("Heir to the Empire") << QStringLiteral("Timothy Zahn") << false;
    QTest::newRow("wrong-short-surname") << QStringLiteral("Words_of_Radiance_Stormlight_Archive_The_-_Harrison.mobi") << QStringLiteral("Words of Radiance") << QStringLiteral("Brandon Sanderson") << false;
    QTest::newRow("too-short-surname") << QStringLiteral("Words_of_Radiance_Stormlight_Archive_The_-_Sand.mobi") << QStringLiteral("Words of Radiance") << QStringLiteral("Brandon Sanderson") << false;
    QTest::newRow("personal-bank") << QStringLiteral("Welcome to HDFC Bank NetBanking.pdf") << QStringLiteral("Welcome to HDFC Bank NetBanking") << QStringLiteral("Timothy Zahn") << false;
    QTest::newRow("personal-insurance") << QStringLiteral("Insurance frauds.pdf") << QStringLiteral("Insurance frauds") << QStringLiteral("Timothy Zahn") << false;
    QTest::newRow("personal-ticket") << QStringLiteral("SpiceJet_E-ticket_PNR B8BUGG - 16 Sep 2015 Mumbai-Hyderabad for MR. GVHV (1).pdf") << QStringLiteral("The Example") << QStringLiteral("Timothy Zahn") << false;
    QTest::newRow("personal-warranty") << QStringLiteral("Warranty.pdf") << QStringLiteral("Warranty") << QStringLiteral("Timothy Zahn") << false;
    QTest::newRow("personal-new-doc") << QStringLiteral("New Doc 12.pdf") << QStringLiteral("New Doc 12") << QStringLiteral("Timothy Zahn") << false;
    QTest::newRow("personal-new-doc-page") << QStringLiteral("New Doc 12Page 1.pdf") << QStringLiteral("New Doc 12Page 1") << QStringLiteral("Timothy Zahn") << false;
    QTest::newRow("personal-untitled") << QStringLiteral("Untitled 1.pdf") << QStringLiteral("Untitled 1") << QStringLiteral("Timothy Zahn") << false;
    QTest::newRow("personal-hash") << QStringLiteral("31459_98aec97b2c764e6c831f6963f8441f27.pdf") << QStringLiteral("The Example") << QStringLiteral("Timothy Zahn") << false;
    QTest::newRow("personal-numeric") << QStringLiteral("158297294x.pdf") << QStringLiteral("The Example") << QStringLiteral("Timothy Zahn") << false;
    QTest::newRow("punctuation") << QStringLiteral("The Example - Jane A. Writer.mobi") << QStringLiteral("The Example") << QStringLiteral("Jane A Writer") << true;
    QTest::newRow("pdf") << QStringLiteral("The Example - Jane A. Writer.pdf") << QStringLiteral("The Example") << QStringLiteral("Jane A Writer") << true;
    QTest::newRow("article") << QStringLiteral("Example, The - Jane A. Writer.mobi") << QStringLiteral("The Example") << QStringLiteral("Jane A Writer") << true;
    QTest::newRow("series-one") << QStringLiteral("A Series - 01 - First Volume - Jane A. Writer.mobi") << QStringLiteral("First Volume") << QStringLiteral("Jane A Writer") << true;
    QTest::newRow("series-two") << QStringLiteral("A Series - 02 - Second Volume - Jane A. Writer.mobi") << QStringLiteral("Second Volume") << QStringLiteral("Jane A Writer") << true;
    QTest::newRow("wrong-volume") << QStringLiteral("A Series - 02 - Second Volume - Jane A. Writer.mobi") << QStringLiteral("First Volume") << QStringLiteral("Jane A Writer") << false;
    QTest::newRow("qualified-series-prefix") << QStringLiteral("A Series Anthology - 01 - First Volume - Jane A. Writer.mobi") << QStringLiteral("First Volume") << QStringLiteral("Jane A Writer") << false;
    QTest::newRow("repeated-series-title") << QStringLiteral("A Series - 01 - A Series - Jane A. Writer.mobi") << QStringLiteral("A Series") << QStringLiteral("Jane A Writer") << false;
    QTest::newRow("number-only") << QStringLiteral("A Series - 01 - Jane A. Writer.mobi") << QStringLiteral("A Series") << QStringLiteral("Jane A Writer") << false;
    QTest::newRow("hash") << QStringLiteral("12345_0123456789abcdef0123456789abcdef.pdf") << QStringLiteral("The Example") << QStringLiteral("Jane A Writer") << false;
    QTest::newRow("personal") << QStringLiteral("Scanned form.pdf") << QStringLiteral("Scanned form") << QStringLiteral("Jane A Writer") << false;
    QTest::newRow("generic") << QStringLiteral("Untitled document 12.pdf") << QStringLiteral("Untitled document 12") << QStringLiteral("Jane A Writer") << false;
    QTest::newRow("truncated-author") << QStringLiteral("First_Volume_Series_Name_-_Writ.mobi") << QStringLiteral("First Volume") << QStringLiteral("Jane A Writer") << false;
    QTest::newRow("wrong-author") << QStringLiteral("The Example - Another Writer.mobi") << QStringLiteral("The Example") << QStringLiteral("Jane A Writer") << false;
    QTest::newRow("abridged") << QStringLiteral("The Example (Abridged) - Jane A. Writer.pdf") << QStringLiteral("The Example") << QStringLiteral("Jane A Writer") << false;
    QTest::newRow("illustrated") << QStringLiteral("The Example Illustrated - Jane A. Writer.pdf") << QStringLiteral("The Example") << QStringLiteral("Jane A Writer") << false;
    QTest::newRow("anthology") << QStringLiteral("The Example Anthology - Jane A. Writer.pdf") << QStringLiteral("The Example") << QStringLiteral("Jane A Writer") << false;
}

void VaultBookIdentityTest::filenameEvidence()
{
    QFETCH(QString, filename); QFETCH(QString, title); QFETCH(QString, author); QFETCH(bool, adopts);
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("ol1w"), title, author)})));
    VaultIndex index(dir.filePath(QStringLiteral("vault.sqlite")));
    auto row = book(QStringLiteral("vault:file"), filename);
    QVERIFY(index.publish({row}));
    QObject owner;
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    wire(identifier, owner, store);
    const auto match = identifier.matchGroup(row.groupKey);
    QCOMPARE(match.adopted, adopts);
    if (adopts) {
        QCOMPARE(match.source, QStringLiteral("BIBLIO"));
        QCOMPARE(match.sourceId, QStringLiteral("biblio:ol1w"));
        QCOMPARE(match.bookProvenance.size(), 1);
        QVERIFY(identifier.applyGroup(row.groupKey, match));
        QCOMPARE(index.rowsForGroup(row.groupKey).first().id, row.id);
    }
}

void VaultBookIdentityTest::titleOnlySurnameMustRemainUnambiguous()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("one"), QStringLiteral("Words of Radiance"),
        QStringLiteral("Brandon Sanderson"))})));
    QCOMPARE(store.exactWorkMatches(QStringLiteral("Words of Radiance"), QString()).candidates.size(), 1);
    VaultIndex index(dir.filePath(QStringLiteral("vault.sqlite")));
    auto row = book(QStringLiteral("vault:book"),
        QStringLiteral("Words_of_Radiance_Stormlight_Archive_The_-_Sanders.mobi"));
    QVERIFY(index.publish({row}));
    QObject owner;
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    wire(identifier, owner, store);
    QCOMPARE(identifier.matchGroup(row.groupKey).sourceId, QStringLiteral("biblio:one"));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("one"), QStringLiteral("Words of Radiance"),
        QStringLiteral("Brandon Sanderson")),
        work(QStringLiteral("two"), QStringLiteral("Words of Radiance"),
        QStringLiteral("Another Sanders"))})));
    QCOMPARE(store.exactWorkMatches(QStringLiteral("Words of Radiance"), QString()).candidates.size(), 2);
    const auto ambiguous = identifier.matchGroup(row.groupKey);
    QVERIFY(!ambiguous.adopted);
    QCOMPARE(ambiguous.candidateCount, 2);
}

void VaultBookIdentityTest::underscorePrefixMustNotChooseBetweenWorks()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    QVERIFY(store.publish(snapshot({
        work(QStringLiteral("full"), QStringLiteral("Star Wars: Heir to the Empire"), QStringLiteral("Timothy Zahn")),
        work(QStringLiteral("short"), QStringLiteral("Heir to the Empire"), QStringLiteral("Timothy Zahn"))})));
    VaultIndex index(dir.filePath(QStringLiteral("vault.sqlite")));
    auto row = book(QStringLiteral("vault:book"),
        QStringLiteral("Star Wars_ Heir to the Empire - Timothy Zahn.mobi"));
    QVERIFY(index.publish({row}));
    QObject owner;
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    wire(identifier, owner, store);
    const auto ambiguous = identifier.matchGroup(row.groupKey);
    QVERIFY(!ambiguous.adopted);
    QCOMPARE(ambiguous.candidateCount, 2);
}

void VaultBookIdentityTest::missingCatalogueGetsLookedUp()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("other"), QStringLiteral("Another Work"),
        QStringLiteral("Timothy Zahn"))})));
    VaultIndex index(dir.filePath(QStringLiteral("vault.sqlite")));
    auto starWars = book(QStringLiteral("vault:star"),
        QStringLiteral("Star Wars_ Dark Force Rising - Timothy Zahn.mobi"));
    auto stormlight = book(QStringLiteral("vault:storm"),
        QStringLiteral("Words_of_Radiance_Stormlight_Archive_The_-_Sanders.mobi"));
    QVERIFY(index.publish({starWars, stormlight}));
    QObject owner;
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    wire(identifier, owner, store);
    for (const auto &row : {starWars, stormlight}) {
        const auto result = identifier.matchGroup(row.groupKey);
        QVERIFY(!result.adopted);
        QCOMPARE(result.bookLookupStatus, BiblioIdentityLookup::Status::Available);
        QCOMPARE(result.bookLookupReason, QStringLiteral("no-exact-work"));
    }
}

void VaultBookIdentityTest::copiesAndExistingChoicesRemainPhysical()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("ol1w"))})));
    VaultIndex index(dir.filePath(QStringLiteral("vault.sqlite")));
    auto mobi = book(QStringLiteral("vault:mobi"), QStringLiteral("The Example - Jane A. Writer.mobi"));
    mobi.progressed = true;
    auto pdf = book(QStringLiteral("vault:pdf"), QStringLiteral("The Example - Jane A. Writer.pdf"));
    auto manual = book(QStringLiteral("vault:manual"), mobi.realName);
    manual.identityId = QStringLiteral("manual:chosen"); manual.identitySource = QStringLiteral("MANUAL");
    auto suppressed = book(QStringLiteral("vault:suppressed"), mobi.realName); suppressed.identitySuppressed = true;
    auto corrupt = book(QStringLiteral("vault:corrupt"), mobi.realName); corrupt.errorState = QStringLiteral("corrupt");
    auto away = book(QStringLiteral("vault:away"), mobi.realName); away.away = true;
    auto epub = book(QStringLiteral("vault:epub"), QStringLiteral("The Example - Jane A. Writer.epub"));
    epub.embeddedTitle = QStringLiteral("The Example"); epub.metadataSource = QStringLiteral("EPUB");
    QVERIFY(index.publish({mobi, pdf, manual, suppressed, corrupt, away, epub}));
    QObject owner;
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    wire(identifier, owner, store);
    QCOMPARE(identifier.autoIdentifyExisting(), 3);
    auto now = index.rowsForGroup(mobi.groupKey).first();
    QCOMPARE(now.id, mobi.id); QCOMPARE(now.path, mobi.path); QVERIFY(now.progressed);
    QCOMPARE(now.identityId, QStringLiteral("biblio:ol1w"));
    const auto pdfNow = index.rowsForGroup(pdf.groupKey).first();
    QCOMPARE(pdfNow.id, pdf.id); QVERIFY(!pdfNow.progressed); QCOMPARE(pdfNow.groupKey, pdf.groupKey);
    QCOMPARE(index.rowsForGroup(epub.groupKey).first().identityId, QStringLiteral("epub:") + epub.id);
    QCOMPARE(index.rowsForGroup(manual.groupKey).first().identityId, manual.identityId);
    for (const auto &original : {suppressed, corrupt, away})
        QVERIFY(index.rowsForGroup(original.groupKey).first().identityId.isEmpty());
}

void VaultBookIdentityTest::conflictingEmbeddedFactsAbstain()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("one")),
        work(QStringLiteral("two"), QStringLiteral("A Different Work"))})));
    VaultIndex index(dir.filePath(QStringLiteral("vault.sqlite")));
    auto row = book(QStringLiteral("vault:file"), QStringLiteral("The Example - Jane A. Writer.mobi"));
    row.embeddedTitle = QStringLiteral("A Different Work"); row.author = QStringLiteral("Jane A Writer");
    row.metadataSource = QStringLiteral("MOBI");
    QVERIFY(index.publish({row}));
    QObject owner;
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    wire(identifier, owner, store);
    QVERIFY(!identifier.matchGroup(row.groupKey).adopted);
    row.embeddedTitle = QStringLiteral("The Example");
    QVERIFY(index.upsert(row));
    QVERIFY(identifier.matchGroup(row.groupKey).adopted);
    QVERIFY(store.publish(snapshot({work(QStringLiteral("one")), work(QStringLiteral("two"))})));
    const auto ambiguous = identifier.matchGroup(row.groupKey);
    QVERIFY(!ambiguous.adopted); QCOMPARE(ambiguous.candidateCount, 2);
    QVERIFY(identifier.recordAmbiguous(row.groupKey, ambiguous.candidateCount));
    QCOMPARE(index.rowsForGroup(row.groupKey).first().identityState, QStringLiteral("ambiguous"));
}

void VaultBookIdentityTest::revisionRetryGuards_data()
{
    QTest::addColumn<QString>("change");
    for (const auto &change : {"none", "zero", "progress", "deleted", "changed", "suppressed", "manual", "away", "corrupt", "owner-destroyed"})
        QTest::newRow(change) << QString::fromLatin1(change);
}

void VaultBookIdentityTest::revisionRetryGuards()
{
    QFETCH(QString, change);
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    if (change == QLatin1String("zero"))
        QVERIFY(store.publish(snapshot({work(QStringLiteral("other"), QStringLiteral("Another Title"))})));
    VaultIndex index(dir.filePath(QStringLiteral("vault.sqlite")));
    auto row = book(QStringLiteral("vault:file"), QStringLiteral("The Example - Jane A. Writer.mobi"));
    QVERIFY(index.publish({row}));
    auto owner = std::make_unique<QObject>();
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    wire(identifier, *owner, store);
    const auto pending = identifier.matchGroup(row.groupKey);
    QVERIFY(!pending.adopted);
    QCOMPARE(pending.bookLookupStatus, change == QLatin1String("zero")
        ? BiblioIdentityLookup::Status::Available : BiblioIdentityLookup::Status::Unavailable);
    if (change == QLatin1String("deleted")) { QVERIFY(index.publish({})); }
    else {
        if (change == QLatin1String("changed")) row.mtimeMs++;
        if (change == QLatin1String("progress")) row.progressed = true;
        if (change == QLatin1String("suppressed")) row.identitySuppressed = true;
        if (change == QLatin1String("manual")) row.identityId = QStringLiteral("manual:chosen");
        if (change == QLatin1String("away")) row.away = true;
        if (change == QLatin1String("corrupt")) row.errorState = QStringLiteral("corrupt");
        QVERIFY(index.upsert(row));
    }
    QVERIFY(store.publish(snapshot({work(QStringLiteral("one"))})));
    if (change == QLatin1String("owner-destroyed")) owner.reset();
    identifier.retryBookCatalog();
    QTest::qWait(40);
    const auto rows = index.rowsForGroup(row.groupKey);
    if (change == QLatin1String("deleted")) QVERIFY(rows.isEmpty());
    else if (change == QLatin1String("none") || change == QLatin1String("zero") || change == QLatin1String("progress")) {
        QCOMPARE(rows.first().identityId, QStringLiteral("biblio:one"));
        QCOMPARE(rows.first().progressed, row.progressed);
    } else QCOMPARE(rows.first().identityId, row.identityId);
}

void VaultBookIdentityTest::staleMatchAndOwnerReplacementAreRejected()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("books.sqlite"))));
    QVERIFY(store.publish(snapshot({work(QStringLiteral("one"))})));
    VaultIndex index(dir.filePath(QStringLiteral("vault.sqlite")));
    auto row = book(QStringLiteral("vault:file"), QStringLiteral("The Example - Jane A. Writer.mobi"));
    QVERIFY(index.publish({row}));
    QObject firstOwner, secondOwner;
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    wire(identifier, firstOwner, store);
    const auto stale = identifier.matchGroup(row.groupKey); QVERIFY(stale.adopted);
    row.mtimeMs++; QVERIFY(index.upsert(row));
    QVERIFY(!identifier.applyGroup(row.groupKey, stale));
    const auto priorRoute = identifier.matchGroup(row.groupKey); QVERIFY(priorRoute.adopted);
    wire(identifier, secondOwner, store);
    QVERIFY(!identifier.applyGroup(row.groupKey, priorRoute));
    const auto priorRevision = identifier.matchGroup(row.groupKey); QVERIFY(priorRevision.adopted);
    identifier.retryBookCatalog();
    QVERIFY(!identifier.applyGroup(row.groupKey, priorRevision));
    QVERIFY(index.rowsForGroup(row.groupKey).first().identityId.isEmpty());
}

QTEST_GUILESS_MAIN(VaultBookIdentityTest)
#include "tst_vault_book_identity.moc"
