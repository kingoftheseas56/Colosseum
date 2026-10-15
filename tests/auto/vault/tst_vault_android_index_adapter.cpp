// Android Vault backend contract. Provider APIs return structured entries; this
// adapter maps them into the existing VaultIndex without walking desktop paths.
#include "engine/VaultAndroidIndexAdapter.h"
#include "engine/VaultAndroidStorageBridge.h"
#include "engine/VaultIdentity.h"
#include "engine/VaultIndex.h"

#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

class VaultAndroidIndexAdapterTest final : public QObject
{
    Q_OBJECT

private:
    static VaultAndroidIndexAdapter::MediaEntry videoEntry()
    {
        VaultAndroidIndexAdapter::MediaEntry entry;
        entry.uri = QStringLiteral("content://media/external/video/media/42");
        entry.displayName = QStringLiteral("Dune.2021.mkv");
        entry.relativePath = QStringLiteral("Dune (2021)");
        entry.mimeType = QStringLiteral("video/x-matroska");
        entry.sizeBytes = 1234;
        entry.modifiedMs = 5000;
        entry.durationMs = 123000;
        return entry;
    }

private slots:    void mapsProviderFactsIntoSharedIndex();
    void unavailableSourceMarksRowsAwayWithoutDeleting();
    void accessibleEmptySourceRemovesRows();
    void returningSourceClearsAwayAndPreservesFacts();
    void contentUriRenamePreservesIdentityAndProgress();
    void decodesAndroidStorageSnapshots();
    void providerDuplicateAndUnknownEntriesAreIgnored();
};

void VaultAndroidIndexAdapterTest::mapsProviderFactsIntoSharedIndex()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIndex index(tmp.filePath(QStringLiteral("index.sqlite")));
    VaultIdentity identity(tmp.path());
    QVERIFY(index.isOpen());
    VaultAndroidIndexAdapter adapter(&index, &identity);

    VaultAndroidIndexAdapter::MediaEntry comic;
    comic.uri = QStringLiteral("content://docs/document/comic%3A7");
    comic.displayName = QStringLiteral("Berserk v2.cbz");
    comic.relativePath = QStringLiteral("Berserk/Volume 02");
    comic.mimeType = QStringLiteral("application/vnd.comicbook+zip");
    comic.sizeBytes = 222;
    comic.modifiedMs = 7000;

    VaultAndroidIndexAdapter::SourceSnapshot snapshot;
    snapshot.rootUri = QStringLiteral(
        "content://com.android.externalstorage.documents/tree/primary%3AMedia");
    snapshot.entries = {videoEntry(), comic};
    const auto applied = adapter.applySnapshot(snapshot);
    QVERIFY(applied.ok);
    QCOMPARE(applied.indexedCount, 2);
    QCOMPARE(index.itemCount(), 2);

    const auto rows = index.rowsForRoot(snapshot.rootUri);
    QCOMPARE(rows.size(), 2);
    const auto video = std::find_if(rows.cbegin(), rows.cend(), [](const auto& row) {
        return row.kind == QLatin1String("video");
    });
    QVERIFY(video != rows.cend());
    QCOMPARE(video->path, videoEntry().uri);
    QCOMPARE(video->rootPath, snapshot.rootUri);
    QCOMPARE(video->subtreePath, snapshot.rootUri + QStringLiteral("/Dune (2021)"));
    QCOMPARE(video->groupTitle, QStringLiteral("Dune"));
    QCOMPARE(video->format, QStringLiteral("mkv"));
    QCOMPARE(video->durationSec, 123.0);
    QVERIFY(video->id.startsWith(QStringLiteral("vault:")));

    const auto comicRow = std::find_if(rows.cbegin(), rows.cend(), [](const auto& row) {
        return row.kind == QLatin1String("comic");
    });
    QVERIFY(comicRow != rows.cend());
    QCOMPARE(comicRow->subfolder, QStringLiteral("Volume 02"));
    QCOMPARE(comicRow->path, comic.uri);
}

void VaultAndroidIndexAdapterTest::unavailableSourceMarksRowsAwayWithoutDeleting()
{
    QTemporaryDir tmp;
    VaultIndex index(tmp.filePath(QStringLiteral("index.sqlite")));
    VaultIdentity identity(tmp.path());
    VaultAndroidIndexAdapter adapter(&index, &identity);
    const QString root = QStringLiteral("content://docs/tree/primary%3AVideos");

    VaultAndroidIndexAdapter::SourceSnapshot present;
    present.rootUri = root;
    present.entries = {videoEntry()};
    QVERIFY(adapter.applySnapshot(present).ok);
    QCOMPARE(index.itemCount(), 1);

    VaultAndroidIndexAdapter::SourceSnapshot unavailable;
    unavailable.rootUri = root;
    unavailable.available = false;
    const auto result = adapter.applySnapshot(unavailable);
    QVERIFY(result.ok);
    QVERIFY(result.sourceAway);
    QCOMPARE(index.itemCount(), 1);
    const auto rows = index.rowsForRoot(root);
    QCOMPARE(rows.size(), 1);
    QVERIFY(rows.first().away);
}

void VaultAndroidIndexAdapterTest::accessibleEmptySourceRemovesRows()
{
    QTemporaryDir tmp;
    VaultIndex index(tmp.filePath(QStringLiteral("index.sqlite")));
    VaultIdentity identity(tmp.path());
    VaultAndroidIndexAdapter adapter(&index, &identity);
    const QString root = QStringLiteral("content://docs/tree/primary%3ABooks");

    VaultAndroidIndexAdapter::SourceSnapshot populated;
    populated.rootUri = root;
    populated.entries = {videoEntry()};
    QVERIFY(adapter.applySnapshot(populated).ok);

    VaultAndroidIndexAdapter::SourceSnapshot empty;
    empty.rootUri = root;
    const auto result = adapter.applySnapshot(empty);
    QVERIFY(result.ok);
    QCOMPARE(result.removedCount, 1);
    QCOMPARE(result.indexedCount, 0);
    QCOMPARE(index.itemCount(), 0);
}

void VaultAndroidIndexAdapterTest::returningSourceClearsAwayAndPreservesFacts()
{
    QTemporaryDir tmp;
    VaultIndex index(tmp.filePath(QStringLiteral("index.sqlite")));
    VaultIdentity identity(tmp.path());
    VaultAndroidIndexAdapter adapter(&index, &identity);
    const QString root = QStringLiteral("content://docs/tree/primary%3AMedia");

    VaultAndroidIndexAdapter::SourceSnapshot present;
    present.rootUri = root;
    present.entries = {videoEntry()};
    QVERIFY(adapter.applySnapshot(present).ok);
    auto rows = index.rowsForRoot(root);
    QCOMPARE(rows.size(), 1);
    rows[0].progressed = true;
    rows[0].coverRef = QStringLiteral("cached-cover");
    QVERIFY(index.upsert(rows[0]));

    VaultAndroidIndexAdapter::SourceSnapshot unavailable;
    unavailable.rootUri = root;
    unavailable.available = false;
    QVERIFY(adapter.applySnapshot(unavailable).ok);
    QVERIFY(index.rowsForRoot(root).first().away);

    const auto restored = adapter.applySnapshot(present);
    QVERIFY(restored.ok);
    rows = index.rowsForRoot(root);
    QCOMPARE(rows.size(), 1);
    QVERIFY(!rows.first().away);
    QVERIFY(rows.first().progressed);
    QCOMPARE(rows.first().coverRef, QStringLiteral("cached-cover"));
}

void VaultAndroidIndexAdapterTest::contentUriRenamePreservesIdentityAndProgress()
{
    QTemporaryDir tmp;
    VaultIndex index(tmp.filePath(QStringLiteral("index.sqlite")));
    VaultIdentity identity(tmp.path());
    VaultAndroidIndexAdapter adapter(&index, &identity);
    const QString root = QStringLiteral("content://docs/tree/primary%3AMedia");

    VaultAndroidIndexAdapter::SourceSnapshot first;
    first.rootUri = root;
    first.entries = {videoEntry()};
    QVERIFY(adapter.applySnapshot(first).ok);
    auto rows = index.rowsForRoot(root);
    QCOMPARE(rows.size(), 1);
    const QString originalId = rows.first().id;
    rows[0].progressed = true;
    QVERIFY(index.upsert(rows[0]));

    auto renamed = videoEntry();
    renamed.uri = QStringLiteral("content://media/external/video/media/777");
    renamed.displayName = QStringLiteral("Dune Final.mkv");
    VaultAndroidIndexAdapter::SourceSnapshot second;
    second.rootUri = root;
    second.entries = {renamed};
    QVERIFY(adapter.applySnapshot(second).ok);

    rows = index.rowsForRoot(root);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().path, renamed.uri);
    QCOMPARE(rows.first().id, originalId);
    QVERIFY(rows.first().progressed);
}
void VaultAndroidIndexAdapterTest::decodesAndroidStorageSnapshots()
{
    const QByteArray payload = R"json({
      "ok": true,
      "sources": [{
        "rootUri": "content://media/external/video",
        "available": true,
        "entries": [{
          "uri": "content://media/external/video/media/42",
          "displayName": "Dune.2021.mkv",
          "relativePath": "Movies/Dune (2021)",
          "mimeType": "video/x-matroska",
          "sizeBytes": 1234,
          "modifiedMs": 5000,
          "durationMs": 123000
        }]
      }]
    })json";

    const auto decoded = VaultAndroidStorageBridge::decodeSnapshotJson(payload);
    QVERIFY(decoded.ok);
    QCOMPARE(decoded.sources.size(), 1);
    QCOMPARE(decoded.sources.first().rootUri,
             QStringLiteral("content://media/external/video"));
    QCOMPARE(decoded.sources.first().entries.size(), 1);
    QCOMPARE(decoded.sources.first().entries.first().relativePath,
             QStringLiteral("Movies/Dune (2021)"));
    QCOMPARE(decoded.sources.first().entries.first().durationMs, 123000);

    const auto bad = VaultAndroidStorageBridge::decodeSnapshotJson(QByteArray("not-json"));
    QVERIFY(!bad.ok);
    QVERIFY(!bad.error.isEmpty());
}
void VaultAndroidIndexAdapterTest::providerDuplicateAndUnknownEntriesAreIgnored()
{
    QTemporaryDir tmp;
    VaultIndex index(tmp.filePath(QStringLiteral("index.sqlite")));
    VaultIdentity identity(tmp.path());
    VaultAndroidIndexAdapter adapter(&index, &identity);

    auto duplicate = videoEntry();
    VaultAndroidIndexAdapter::MediaEntry unknown;
    unknown.uri = QStringLiteral("content://docs/document/readme%3A1");
    unknown.displayName = QStringLiteral("README.txt");
    unknown.mimeType = QStringLiteral("text/plain");

    VaultAndroidIndexAdapter::MediaEntry extensionless;
    extensionless.uri = QStringLiteral("content://media/external/video/media/99");
    extensionless.displayName = QStringLiteral("Camera recording");
    extensionless.mimeType = QStringLiteral("video/mp4");
    extensionless.sizeBytes = 55;
    extensionless.modifiedMs = 88;
    extensionless.durationMs = 1000;

    VaultAndroidIndexAdapter::SourceSnapshot snapshot;
    snapshot.rootUri = QStringLiteral("content://media/external");
    snapshot.entries = {duplicate, duplicate, unknown, extensionless};
    const auto result = adapter.applySnapshot(snapshot);
    QVERIFY(result.ok);
    QCOMPARE(result.indexedCount, 2);
    QCOMPARE(index.itemCount(), 2);
    QCOMPARE(index.itemCountForKind(QStringLiteral("video")), 2);
    QCOMPARE(index.itemCountForKind(QStringLiteral("book")), 0);
    QCOMPARE(index.itemCountForKind(QStringLiteral("comic")), 0);
}

QTEST_GUILESS_MAIN(VaultAndroidIndexAdapterTest)
#include "tst_vault_android_index_adapter.moc"
