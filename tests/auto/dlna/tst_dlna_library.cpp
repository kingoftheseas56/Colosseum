// tst_dlna_library - Arc 71. Proves DlnaLibrary projects the Vault + audiobook owners into the
// immutable DlnaSnapshot the DLNA server reads: container tree, opaque stable ids, fail-closed
// eligibility (hidden root, hidden item, away, unadmitted video, missing file, size mismatch),
// physical dedupe, systemUpdateId behaviour, debounce + signals, stop() revocation, and
// thread-safe snapshot(). Real tiny temp files; a real VaultIndex (SQLite in a temp dir) and
// VaultConfig; audiobooks come from a QObject fake with the downloader's Q_INVOKABLE shape.
// GUILESS; no network.

#include "dlna/DlnaLibrary.h"
#include "engine/VaultConfig.h"
#include "engine/VaultIndex.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QThread>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QtTest>

#include <atomic>
#include <thread>

// Builds run on a worker thread: kick one and wait for its result to be handled.
#define REBUILD(lib)     do {         (lib).rebuildNow();         QTRY_VERIFY_WITH_TIMEOUT(!(lib).isBuilding(), 10000);     } while (false)

namespace {

// The shape of AudiobookDownloader that DlnaLibrary relies on.
class FakeAudiobooks : public QObject
{
    Q_OBJECT
public:
    QVariantList books;
    QHash<QString, QStringList> files;

    Q_INVOKABLE QVariantList downloadedAudiobooks() const { return books; }
    Q_INVOKABLE QStringList localFiles(const QString& pairKey) const { return files.value(pairKey); }
signals:
    void finished(const QString& pairKey, const QString& dir);
    void removed(const QString& pairKey);
};

QString writeFile(const QString& path, int bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(QByteArray(bytes, 'x'));
    f.close();
    return QFileInfo(path).absoluteFilePath();
}

QString itemId(const QString& vaultId)
{
    return QLatin1Char('i') + QString::fromLatin1(
        QCryptographicHash::hash(vaultId.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

} // namespace

class tst_dlna_library : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QString m_lib, m_hiddenRoot, m_dl, m_ab;
    VaultIndex* m_index = nullptr;
    VaultConfig* m_config = nullptr;
    FakeAudiobooks* m_audio = nullptr;
    QList<VaultIndex::FileRow> m_rows;

    // Fixture ids the assertions refer to.
    const QString idFilm = QStringLiteral("vault:film");
    const QString idEp1 = QStringLiteral("vault:ep1");
    const QString idEp2 = QStringLiteral("vault:ep2");
    const QString idBook = QStringLiteral("vault:book");
    const QString idComic1 = QStringLiteral("vault:comic1");
    const QString idComic2 = QStringLiteral("vault:comic2");
    const QString idDup = QStringLiteral("vault:dup");   // same file as idFilm, via the downloads root
    const QString idDl = QStringLiteral("vault:dl");     // download only reachable through the synthetic root
    const QString idHiddenItem = QStringLiteral("vault:hiddenitem");

    VaultIndex::FileRow row(const QString& id, const QString& kind, const QString& root,
                            const QString& groupDir, const QString& groupTitle,
                            const QString& path, const QString& subfolder = QString())
    {
        VaultIndex::FileRow r;
        r.id = id;
        r.kind = kind;
        r.rootPath = root;
        r.subtreePath = groupDir;
        r.groupKey = groupDir;
        r.groupTitle = groupTitle;
        r.path = path;
        r.realName = QFileInfo(path).fileName();
        r.displayTitle = QFileInfo(path).completeBaseName();
        r.subfolder = subfolder;
        r.size = QFileInfo(path).size();
        r.mtimeMs = QFileInfo(path).lastModified().toMSecsSinceEpoch();
        if (kind == QLatin1String("video"))
            r.admissionVerdict = QStringLiteral("Admitted");
        return r;
    }

    static const DlnaItem* findItemByTitle(const DlnaSnapshot& s, const QString& title)
    {
        for (auto it = s.items.constBegin(); it != s.items.constEnd(); ++it)
            if (it->title == title)
                return &it.value();
        return nullptr;
    }
    static QStringList childTitles(const DlnaSnapshot& s, const QString& containerId, bool containers)
    {
        QStringList out;
        const DlnaContainer c = s.containers.value(containerId);
        for (const QString& id : containers ? c.childContainerIds : c.childItemIds)
            out << (containers ? s.containers.value(id).title : s.items.value(id).title);
        return out;
    }
    QString childContainerByTitle(const DlnaSnapshot& s, const QString& parent, const QString& title) const
    {
        for (const QString& id : s.containers.value(parent).childContainerIds)
            if (s.containers.value(id).title == title)
                return id;
        return {};
    }

private slots:
    void initTestCase();
    void cleanupTestCase();

    void mime_is_truthful_by_extension();
    void initial_snapshot_is_empty_root();
    void container_tree_and_metadata();
    void exclusions_fail_closed();
    void duplicate_physical_file_listed_once();
    void ids_are_opaque_and_stable_across_rebuilds();
    void system_update_id_only_moves_on_change();
    void debounced_owner_signals_rebuild_and_stop_revokes();
    void snapshot_is_safe_from_other_threads();
    void system_update_id_continues_from_injected_value();
    void build_runs_off_the_owner_thread_and_publishes_async();
    void cover_hook_results_are_limited_to_servable_images();
    void owner_change_during_build_triggers_one_more_build();
    void stop_discards_a_build_in_flight();
};

void tst_dlna_library::initTestCase()
{
    QVERIFY(m_tmp.isValid());
    const QString base = m_tmp.path();
    m_lib = base + QStringLiteral("/lib");
    m_hiddenRoot = base + QStringLiteral("/hiddenroot");
    m_dl = base + QStringLiteral("/downloads");
    m_ab = base + QStringLiteral("/audiobooks");

    QVERIFY(m_lib != QString());
    const QString film = writeFile(m_lib + "/Inception (2010)/Inception.2010.1080p.mkv", 100);
    const QString ep1 = writeFile(m_lib + "/Some Show/Season 01/Some.Show.S01E01.mkv", 11);
    const QString ep2 = writeFile(m_lib + "/Some Show/Season 01/Some.Show.S01E02.mkv", 12);
    const QString book = writeFile(m_lib + "/Books/Dune.epub", 20);
    const QString comic1 = writeFile(m_lib + "/Comics/Saga/Saga 001.cbz", 31);
    const QString comic2 = writeFile(m_lib + "/Comics/Saga/Saga 002.cbz", 32);
    const QString unadmitted = writeFile(m_lib + "/Unadmitted/Weird.mkv", 41);
    const QString awayFile = writeFile(m_lib + "/Away Film/Away.mkv", 42);
    const QString hiddenItem = writeFile(m_lib + "/Hidden Item/Secret.mkv", 43);
    const QString hiddenRootFile = writeFile(m_hiddenRoot + "/Hidden Film/H.mkv", 44);
    const QString dlFilm = writeFile(m_dl + "/Downloaded Film/Downloaded.mp4", 45);
    const QString sizeLiar = writeFile(m_lib + "/Size Liar/Liar.mkv", 46);
    const QString tri1 = writeFile(m_lib + "/Trilogy/Trilogy 1.mkv", 61);
    const QString tri2 = writeFile(m_lib + "/Trilogy/Trilogy 2.mkv", 62);
    const QString tri3 = writeFile(m_lib + "/Trilogy/Trilogy 3.mkv", 63);
    const QString sampleFile = writeFile(m_lib + "/Inception (2010)/Inception.2010-sample.mkv", 64);
    const QString featurette = writeFile(m_lib + "/Inception (2010)/Making Of/Featurettes/Making Of.mkv", 65);
    const QString trailerDir = writeFile(m_lib + "/Inception (2010)/Trailers/Teaser.mkv", 66);
    const QString trailerName = writeFile(m_lib + "/Trilogy/Trilogy 1 - Trailer.mkv", 67);
    const QString parkBoys = writeFile(m_lib + "/Trailer Park Boys/Trailer Park Boys.mkv", 68); // a real title
    const QString fan1 = writeFile(m_lib + "/Fansub Show/Fansub Show - 003.mkv", 71);
    const QString fan2 = writeFile(m_lib + "/Fansub Show/Fansub Show - 004.mkv", 72);
    const QString lone300 = writeFile(m_lib + "/Lone Movie/Lone Movie - 300.mkv", 73);
    QVERIFY(!tri1.isEmpty() && !tri2.isEmpty() && !tri3.isEmpty() && !sampleFile.isEmpty()
            && !featurette.isEmpty() && !trailerDir.isEmpty() && !trailerName.isEmpty()
            && !parkBoys.isEmpty() && !fan1.isEmpty() && !fan2.isEmpty() && !lone300.isEmpty());
    QVERIFY(!film.isEmpty() && !ep1.isEmpty() && !ep2.isEmpty() && !book.isEmpty()
            && !comic1.isEmpty() && !comic2.isEmpty() && !unadmitted.isEmpty() && !awayFile.isEmpty()
            && !hiddenItem.isEmpty() && !hiddenRootFile.isEmpty() && !dlFilm.isEmpty() && !sizeLiar.isEmpty());

    m_config = new VaultConfig(m_tmp.path() + QStringLiteral("/vault"), this);
    m_config->addRoot(m_lib);
    m_config->confirmRoot(m_lib);
    m_config->addRoot(m_hiddenRoot);
    m_config->confirmRoot(m_hiddenRoot);
    m_config->setRootHidden(m_hiddenRoot, true);
    m_config->addSyntheticRoot(m_dl);

    m_index = new VaultIndex(m_tmp.path() + QStringLiteral("/index.sqlite"), this);
    QVERIFY(m_index->isOpen());

    const QString group = QStringLiteral("%1/Inception (2010)").arg(m_lib);
    VaultIndex::FileRow filmRow = row(idFilm, "video", m_lib, group, "Inception (2010)", film);
    filmRow.identityTitle = QStringLiteral("Inception");
    filmRow.identityYear = 2010;
    filmRow.identityGenres = QStringLiteral("Action · Sci-Fi");
    filmRow.identitySynopsis = QStringLiteral("A thief who steals secrets through dreams.");
    filmRow.durationSec = 8880;
    m_rows << filmRow;

    const QString showDir = m_lib + "/Some Show";
    m_rows << row(idEp1, "video", m_lib, showDir, "Some Show", ep1, "Season 01");
    m_rows << row(idEp2, "video", m_lib, showDir, "Some Show", ep2, "Season 01");

    VaultIndex::FileRow bookRow = row(idBook, "book", m_lib, m_lib + "/Books", "Books", book);
    bookRow.author = QStringLiteral("Frank Herbert");
    bookRow.displayTitle = QStringLiteral("Dune");
    m_rows << bookRow;

    m_rows << row(idComic1, "comic", m_lib, m_lib + "/Comics/Saga", "Saga", comic1);
    m_rows << row(idComic2, "comic", m_lib, m_lib + "/Comics/Saga", "Saga", comic2);

    VaultIndex::FileRow unadmittedRow = row("vault:unadmitted", "video", m_lib, m_lib + "/Unadmitted", "Unadmitted", unadmitted);
    unadmittedRow.admissionVerdict = QStringLiteral("RejectedNoVideo");
    m_rows << unadmittedRow;

    VaultIndex::FileRow unprobedRow = row("vault:unprobed", "video", m_lib, m_lib + "/Unprobed", "Unprobed",
                                          writeFile(m_lib + "/Unprobed/Unprobed.mkv", 47));
    unprobedRow.admissionVerdict.clear(); // never probed: fail closed
    m_rows << unprobedRow;

    VaultIndex::FileRow awayRow = row("vault:away", "video", m_lib, m_lib + "/Away Film", "Away Film", awayFile);
    awayRow.away = true;
    m_rows << awayRow;

    m_rows << row(idHiddenItem, "video", m_lib, m_lib + "/Hidden Item", "Hidden Item", hiddenItem);
    m_config->setHidden(idHiddenItem, true);

    m_rows << row("vault:hiddenroot", "video", m_hiddenRoot, m_hiddenRoot + "/Hidden Film", "Hidden Film", hiddenRootFile);

    VaultIndex::FileRow missingRow = row("vault:missing", "video", m_lib, m_lib + "/Gone", "Gone",
                                         m_lib + "/Gone/Gone.mkv");
    missingRow.size = 99; // the file does not exist
    m_rows << missingRow;

    VaultIndex::FileRow liarRow = row("vault:liar", "video", m_lib, m_lib + "/Size Liar", "Size Liar", sizeLiar);
    liarRow.size += 1; // file changed since the scan
    m_rows << liarRow;

    VaultIndex::FileRow errRow = row("vault:err", "video", m_lib, m_lib + "/Err", "Err",
                                     writeFile(m_lib + "/Err/Err.mkv", 48));
    errRow.errorState = QStringLiteral("extract");
    m_rows << errRow;

    // A trilogy in ONE folder (3 films), a sample / featurette / trailer that must be dropped, a
    // real title that merely starts with "Trailer", a fansub run, and a lone "- 300" film.
    const QString trilogy = m_lib + "/Trilogy";
    m_rows << row("vault:tri1", "video", m_lib, trilogy, "Trilogy", tri1);
    m_rows << row("vault:tri2", "video", m_lib, trilogy, "Trilogy", tri2);
    m_rows << row("vault:tri3", "video", m_lib, trilogy, "Trilogy", tri3);
    m_rows << row("vault:trailername", "video", m_lib, trilogy, "Trilogy", trailerName);
    m_rows << row("vault:sample", "video", m_lib, group, "Inception (2010)", sampleFile);
    m_rows << row("vault:featurette", "video", m_lib, group, "Inception (2010)", featurette, "Making Of/Featurettes");
    m_rows << row("vault:trailerdir", "video", m_lib, group, "Inception (2010)", trailerDir, "Trailers");
    m_rows << row("vault:parkboys", "video", m_lib, m_lib + "/Trailer Park Boys", "Trailer Park Boys", parkBoys);
    m_rows << row("vault:fan1", "video", m_lib, m_lib + "/Fansub Show", "Fansub Show", fan1);
    m_rows << row("vault:fan2", "video", m_lib, m_lib + "/Fansub Show", "Fansub Show", fan2);
    m_rows << row("vault:lone300", "video", m_lib, m_lib + "/Lone Movie", "Lone Movie", lone300);

    // The synthetic downloads root: one download only it holds, plus a SECOND row for the very
    // same physical file the user root already shares (different row id, different root).
    m_rows << row(idDl, "video", m_dl, m_dl + "/Downloaded Film", "Downloaded Film", dlFilm);
    m_rows << row(idDup, "video", m_dl, m_dl + "/Inception (2010)", "Inception (2010)", film);

    QVERIFY(m_index->publish(m_rows));

    // Audiobooks: one good two-file book (provider order is already natural), one 'missing' book,
    // one single-file book.
    m_audio = new FakeAudiobooks;
    m_audio->setParent(this);
    const QString a1 = writeFile(m_ab + "/book1/01 - Chapter One.mp3", 51);
    const QString a2 = writeFile(m_ab + "/book1/02 - Chapter Two.mp3", 52);
    const QString a3 = writeFile(m_ab + "/book3/Whole Book.m4b", 53);
    m_audio->books = QVariantList{
        QVariantMap{{"id", "pair:one"}, {"title", "The Long Listen"}, {"author", "A. Narrator"}, {"missing", false}},
        QVariantMap{{"id", "pair:gone"}, {"title", "Deleted Book"}, {"author", "Nobody"}, {"missing", true}},
        QVariantMap{{"id", "pair:three"}, {"title", "Single File Book"}, {"author", "B. Author"}, {"missing", false}},
    };
    m_audio->files.insert("pair:one", {a1, a2});
    m_audio->files.insert("pair:gone", {m_ab + "/book2/lost.mp3"});
    m_audio->files.insert("pair:three", {a3});
}

void tst_dlna_library::cleanupTestCase()
{
    // Close the SQLite file before the QTemporaryDir member is destroyed (Windows cannot
    // remove an open file).
    delete m_index;
    m_index = nullptr;
    delete m_config;
    m_config = nullptr;
    delete m_audio;
    m_audio = nullptr;
}

void tst_dlna_library::mime_is_truthful_by_extension()
{
    const QList<QPair<QString, QString>> cases = {
        {"a.mkv", "video/x-matroska"}, {"a.MP4", "video/mp4"}, {"a.m4v", "video/mp4"},
        {"a.avi", "video/x-msvideo"}, {"a.webm", "video/webm"}, {"a.ts", "video/mp2t"},
        {"a.mov", "video/quicktime"}, {"a.mp3", "audio/mpeg"}, {"a.m4a", "audio/mp4"},
        {"a.m4b", "audio/mp4"}, {"a.flac", "audio/flac"}, {"a.ogg", "audio/ogg"},
        {"a.opus", "audio/ogg"}, {"a.epub", "application/epub+zip"}, {"a.pdf", "application/pdf"},
        {"a.cbz", "application/vnd.comicbook+zip"}, {"a.cbr", "application/vnd.comicbook-rar"},
        {"a.mobi", "application/x-mobipocket-ebook"}, {"a.jpg", "image/jpeg"}, {"a.png", "image/png"},
        {"a.xyz", "application/octet-stream"}, {"noext", "application/octet-stream"},
    };
    for (const auto& c : cases)
        QCOMPARE(DlnaLibrary::mimeForPath(c.first), c.second);
}

void tst_dlna_library::initial_snapshot_is_empty_root()
{
    DlnaLibrary lib(m_index, m_config, m_audio);
    const auto s = lib.snapshot();
    QVERIFY(s);
    QCOMPARE(s->systemUpdateId, 0u);
    QCOMPARE(s->itemCount, 0);
    QVERIFY(s->containers.contains(QStringLiteral("0")));
    QVERIFY(s->containers.value("0").childContainerIds.isEmpty());
    QVERIFY(s->typeOrder.isEmpty());
}

void tst_dlna_library::container_tree_and_metadata()
{
    DlnaLibrary lib(m_index, m_config, m_audio);
    REBUILD(lib);
    const DlnaSnapshot& s = *lib.snapshot();

    QCOMPARE(s.typeOrder, QStringList({"Movies", "Shows", "Books", "Comics", "Audiobooks"}));
    QCOMPARE(childTitles(s, "0", true), s.typeOrder);

    // Movies: flat, sorted by title - the identified film wears its identity title.
    const QString movies = childContainerByTitle(s, "0", "Movies");
    QVERIFY(!movies.isEmpty());
    QVERIFY(s.containers.value(movies).childContainerIds.isEmpty());
    QCOMPARE(childTitles(s, movies, false),
             QStringList({"Downloaded", "Inception", "Lone Movie - 300", "Trailer Park Boys",
                          "Trilogy 1", "Trilogy 2", "Trilogy 3"}));
    // A trilogy in one folder is three Movies (not clips, not a show) and each wears the movie
    // class; the lone "- 300" file is a film, not episode 300.
    for (const QString& t : {"Trilogy 1", "Trilogy 2", "Trilogy 3", "Lone Movie - 300"}) {
        QVERIFY2(findItemByTitle(s, t), qPrintable(t));
        QCOMPARE(findItemByTitle(s, t)->parentId, movies);
        QCOMPARE(findItemByTitle(s, t)->upnpClass, QStringLiteral("object.item.videoItem.movie"));
    }
    // There is no catch-all container for unsorted video any more.
    for (const DlnaContainer& c : s.containers)
        QVERIFY(c.title != QLatin1String("Other videos"));

    const DlnaItem* film = findItemByTitle(s, "Inception");
    QVERIFY(film);
    QCOMPARE(film->upnpClass, QStringLiteral("object.item.videoItem.movie"));
    QCOMPARE(film->mime, QStringLiteral("video/x-matroska"));
    QCOMPARE(film->year, 2010);
    QCOMPARE(film->genres, QStringLiteral("Action · Sci-Fi"));
    QCOMPARE(film->synopsis, QStringLiteral("A thief who steals secrets through dreams."));
    QCOMPARE(film->durationSec, 8880.0);
    QCOMPARE(film->size, qint64(100));
    QVERIFY(film->mtimeMs > 0);
    QCOMPARE(film->id, itemId(idFilm));
    QCOMPARE(film->parentId, movies);
    QVERIFY(film->coverPath.isEmpty()); // no cover resolver wired
    QCOMPARE(findItemByTitle(s, "Downloaded")->mime, QStringLiteral("video/mp4"));

    // Shows > Some Show > Season 1 > two episodes with numbers.
    const QString shows = childContainerByTitle(s, "0", "Shows");
    QCOMPARE(childTitles(s, shows, true), QStringList({"Fansub Show", "Some Show"}));
    {   // a fansub run of absolute numbers is a show: flat -> Season 1, episode numbers kept
        const QString fan = childContainerByTitle(s, shows, "Fansub Show");
        QCOMPARE(childTitles(s, fan, true), QStringList({"Season 1"}));
        const QStringList ids = s.containers.value(childContainerByTitle(s, fan, "Season 1")).childItemIds;
        QCOMPARE(ids.size(), 2);
        QCOMPARE(s.items.value(ids.at(0)).episode, 3);
        QCOMPARE(s.items.value(ids.at(1)).episode, 4);
        QCOMPARE(s.items.value(ids.at(0)).season, 1);
    }
    const QString show = childContainerByTitle(s, shows, "Some Show");
    QCOMPARE(childTitles(s, show, true), QStringList({"Season 1"}));
    const QString season = childContainerByTitle(s, show, "Season 1");
    const QStringList epIds = s.containers.value(season).childItemIds;
    QCOMPARE(epIds.size(), 2);
    const DlnaItem e1 = s.items.value(epIds.at(0));
    const DlnaItem e2 = s.items.value(epIds.at(1));
    QCOMPARE(e1.id, itemId(idEp1));
    QCOMPARE(e2.id, itemId(idEp2));
    QCOMPARE(e1.season, 1);
    QCOMPARE(e1.episode, 1);
    QCOMPARE(e2.episode, 2);
    QCOMPARE(e1.seriesTitle, QStringLiteral("Some Show"));
    QCOMPARE(e1.upnpClass, QStringLiteral("object.item.videoItem"));
    QCOMPARE(e1.parentId, season);

    // Books: flat by title.
    const QString books = childContainerByTitle(s, "0", "Books");
    QCOMPARE(childTitles(s, books, false), QStringList({"Dune"}));
    const DlnaItem* dune = findItemByTitle(s, "Dune");
    QCOMPARE(dune->upnpClass, QStringLiteral("object.item.textItem"));
    QCOMPARE(dune->mime, QStringLiteral("application/epub+zip"));
    QCOMPARE(dune->author, QStringLiteral("Frank Herbert"));

    // Comics: series > issues in natural order.
    const QString comics = childContainerByTitle(s, "0", "Comics");
    QCOMPARE(childTitles(s, comics, true), QStringList({"Saga"}));
    const QString saga = childContainerByTitle(s, comics, "Saga");
    QCOMPARE(childTitles(s, saga, false), QStringList({"Saga 001", "Saga 002"}));
    QCOMPARE(findItemByTitle(s, "Saga 001")->upnpClass, QStringLiteral("object.item"));
    QCOMPARE(findItemByTitle(s, "Saga 001")->mime, QStringLiteral("application/vnd.comicbook+zip"));

    // Audiobooks: one container per present book; multi-file keeps order, single-file wears the
    // book's name; the 'missing' book is absent.
    const QString abs = childContainerByTitle(s, "0", "Audiobooks");
    QCOMPARE(childTitles(s, abs, true), QStringList({"Single File Book", "The Long Listen"}));
    const QString longListen = childContainerByTitle(s, abs, "The Long Listen");
    QCOMPARE(childTitles(s, longListen, false), QStringList({"01 - Chapter One", "02 - Chapter Two"}));
    const DlnaItem ch1 = s.items.value(s.containers.value(longListen).childItemIds.first());
    QCOMPARE(ch1.upnpClass, QStringLiteral("object.item.audioItem.audioBook"));
    QCOMPARE(ch1.mime, QStringLiteral("audio/mpeg"));
    QCOMPARE(ch1.author, QStringLiteral("A. Narrator"));
    const QString single = childContainerByTitle(s, abs, "Single File Book");
    QCOMPARE(childTitles(s, single, false), QStringList({"Single File Book"}));
    QCOMPARE(s.items.value(s.containers.value(single).childItemIds.first()).mime, QStringLiteral("audio/mp4"));

    // 7 films + 4 eps + 1 book + 2 comics + 3 audiobook files.
    QCOMPARE(s.itemCount, 17);
    QCOMPARE(s.items.size(), 17);
}

void tst_dlna_library::exclusions_fail_closed()
{
    DlnaLibrary lib(m_index, m_config, m_audio);
    REBUILD(lib);
    const DlnaSnapshot& s = *lib.snapshot();
    QSet<QString> sharedPaths;
    for (const DlnaItem& it : s.items)
        sharedPaths.insert(QFileInfo(it.filePath).fileName());

    for (const QString& excluded : {"Weird.mkv", "Unprobed.mkv", "Away.mkv", "Secret.mkv", "H.mkv",
                                    "Gone.mkv", "Liar.mkv", "Err.mkv", "lost.mp3", "Inception.2010-sample.mkv",
                                    "Making Of.mkv", "Teaser.mkv", "Trilogy 1 - Trailer.mkv"})
        QVERIFY2(!sharedPaths.contains(excluded), qPrintable(excluded + " must not be shared"));
    for (const QString& included : {"Inception.2010.1080p.mkv", "Downloaded.mp4", "Some.Show.S01E01.mkv",
                                    "Dune.epub", "Saga 001.cbz", "01 - Chapter One.mp3", "Trailer Park Boys.mkv",
                                    "Trilogy 1.mkv", "Trilogy 3.mkv", "Lone Movie - 300.mkv"})
        QVERIFY2(sharedPaths.contains(included), qPrintable(included + " must be shared"));

    // No shelf for the excluded groups exists anywhere in the tree.
    for (const DlnaContainer& c : s.containers)
        for (const QString& bad : {"Unadmitted", "Away Film", "Hidden Item", "Hidden Film", "Gone", "Size Liar", "Err"})
            QVERIFY(c.title != bad);

    // Hiding a root after the fact removes its rows; un-hiding the synthetic downloads root
    // removes the download (config.changed is not connected here: rebuild explicitly).
    m_config->setRootHidden(m_dl, true);
    REBUILD(lib);
    QVERIFY(!findItemByTitle(*lib.snapshot(), "Downloaded"));
    QVERIFY(findItemByTitle(*lib.snapshot(), "Inception")); // still shared through the user root
    m_config->setRootHidden(m_dl, false);
    REBUILD(lib);
    QVERIFY(findItemByTitle(*lib.snapshot(), "Downloaded"));
}

void tst_dlna_library::duplicate_physical_file_listed_once()
{
    DlnaLibrary lib(m_index, m_config, m_audio);
    REBUILD(lib);
    const DlnaSnapshot& s = *lib.snapshot();
    int inceptionCopies = 0;
    for (const DlnaItem& it : s.items)
        if (it.filePath.endsWith(QLatin1String("Inception.2010.1080p.mkv")))
            ++inceptionCopies;
    QCOMPARE(inceptionCopies, 1);
    // The real user-root row wins over the synthetic downloads-root row.
    QVERIFY(s.items.contains(itemId(idFilm)));
    QVERIFY(!s.items.contains(itemId(idDup)));
    QVERIFY(s.items.contains(itemId(idDl)));
}

void tst_dlna_library::ids_are_opaque_and_stable_across_rebuilds()
{
    DlnaLibrary a(m_index, m_config, m_audio);
    REBUILD(a);
    DlnaLibrary b(m_index, m_config, m_audio); // a fresh instance must derive the same ids
    REBUILD(b);
    const auto sa = a.snapshot();
    const auto sb = b.snapshot();
    QVERIFY(sa->sameContent(*sb));
    QCOMPARE(QSet<QString>(sa->items.keyBegin(), sa->items.keyEnd()),
             QSet<QString>(sb->items.keyBegin(), sb->items.keyEnd()));
    QCOMPARE(QSet<QString>(sa->containers.keyBegin(), sa->containers.keyEnd()),
             QSet<QString>(sb->containers.keyBegin(), sb->containers.keyEnd()));

    const QString root = m_tmp.path();
    const auto looksOpaque = [&](const QString& id) {
        return !id.contains('/') && !id.contains('\\') && !id.contains(':') && !id.contains(' ')
            && !id.contains(QFileInfo(root).fileName());
    };
    for (const QString& id : sa->items.keys())
        QVERIFY2(looksOpaque(id) && id.startsWith('i') && id.size() == 17, qPrintable(id));
    for (const QString& id : sa->containers.keys())
        QVERIFY2(id == "0" || looksOpaque(id), qPrintable(id));
    // Parent links resolve and are consistent with the child lists.
    for (const DlnaItem& it : sa->items)
        QVERIFY(sa->containers.value(it.parentId).childItemIds.contains(it.id));
    for (const DlnaContainer& c : sa->containers)
        if (c.id != "0")
            QVERIFY(sa->containers.value(c.parentId).childContainerIds.contains(c.id));
}

void tst_dlna_library::system_update_id_only_moves_on_change()
{
    DlnaLibrary lib(m_index, m_config, m_audio);
    QSignalSpy changed(&lib, &DlnaLibrary::snapshotChanged);
    QCOMPARE(lib.snapshot()->systemUpdateId, 0u);

    REBUILD(lib);
    QCOMPARE(lib.snapshot()->systemUpdateId, 1u);
    QCOMPARE(changed.count(), 1);
    const auto first = lib.snapshot();

    REBUILD(lib); // nothing changed: same snapshot object, no signal, id unchanged
    QCOMPARE(lib.snapshot()->systemUpdateId, 1u);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(lib.snapshot().get(), first.get());

    // Hide one episode: id moves, the item is gone, others keep their ids.
    m_config->setHidden(idEp2, true);
    REBUILD(lib);
    QCOMPARE(lib.snapshot()->systemUpdateId, 2u);
    QCOMPARE(changed.count(), 2);
    QVERIFY(!lib.snapshot()->items.contains(itemId(idEp2)));
    QVERIFY(lib.snapshot()->items.contains(itemId(idEp1)));
    QCOMPARE(lib.snapshot()->itemCount, 16);
    // The old snapshot handed out earlier is untouched (immutable).
    QVERIFY(first->items.contains(itemId(idEp2)));

    // Un-hide: id moves again and the very same item id returns (stable ids).
    m_config->setHidden(idEp2, false);
    REBUILD(lib);
    QCOMPARE(lib.snapshot()->systemUpdateId, 3u);
    QVERIFY(lib.snapshot()->items.contains(itemId(idEp2)));
    QVERIFY(lib.snapshot()->sameContent(*first));
}

void tst_dlna_library::debounced_owner_signals_rebuild_and_stop_revokes()
{
    DlnaLibrary lib(m_index, m_config, m_audio);
    QSignalSpy changed(&lib, &DlnaLibrary::snapshotChanged);
    lib.start();
    lib.start(); // idempotent
    QVERIFY(lib.isStarted());
    QCOMPARE(changed.count(), 0); // debounced: nothing synchronous
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 3000);
    const quint32 id1 = lib.snapshot()->systemUpdateId;
    QCOMPARE(lib.snapshot()->itemCount, 17);

    // A burst of owner signals coalesces into ONE rebuild. Hide the book, then fire several
    // change signals from different owners.
    m_config->setHidden(idBook, true);        // VaultConfig::changed
    emit m_audio->removed(QStringLiteral("x")); // downloader signal
    emit m_audio->finished(QStringLiteral("x"), QString());
    emit m_index->changed();
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 3000);
    QTest::qWait(600);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(lib.snapshot()->systemUpdateId, id1 + 1);
    QVERIFY(!findItemByTitle(*lib.snapshot(), "Dune"));
    QVERIFY(!lib.snapshot()->typeOrder.contains("Books"));
    m_config->setHidden(idBook, false);
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 3, 3000);
    QVERIFY(findItemByTitle(*lib.snapshot(), "Dune"));

    // An audiobook finishing downloads rebuilds the audiobook shelf.
    const QString a4 = writeFile(m_ab + "/book4/Fresh.mp3", 54);
    m_audio->books.append(QVariantMap{{"id", "pair:four"}, {"title", "Fresh Book"}, {"author", "C"}, {"missing", false}});
    m_audio->files.insert("pair:four", {a4});
    emit m_audio->finished(QStringLiteral("pair:four"), m_ab + "/book4");
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 4, 3000);
    QVERIFY(findItemByTitle(*lib.snapshot(), "Fresh Book"));

    // stop(): revoked to an empty root, no signal, and later owner changes do nothing.
    const int emitted = changed.count();
    lib.stop();
    QVERIFY(!lib.isStarted());
    QCOMPARE(lib.snapshot()->itemCount, 0);
    QVERIFY(lib.snapshot()->typeOrder.isEmpty());
    QVERIFY(lib.snapshot()->containers.contains("0"));
    emit m_index->changed();
    emit m_audio->removed(QStringLiteral("x"));
    QTest::qWait(600);
    QCOMPARE(changed.count(), emitted);
    QCOMPARE(lib.snapshot()->itemCount, 0);

    // start() again republishes with a monotonically larger update id.
    const quint32 afterStop = lib.snapshot()->systemUpdateId;
    lib.start();
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), emitted + 1, 3000);
    QVERIFY(lib.snapshot()->systemUpdateId > afterStop);
    QVERIFY(lib.snapshot()->itemCount > 0);
    lib.stop();

    // clean the extra audiobook so test order does not matter
    m_audio->books.removeLast();
    m_audio->files.remove("pair:four");
}

void tst_dlna_library::snapshot_is_safe_from_other_threads()
{
    DlnaLibrary lib(m_index, m_config, m_audio);
    REBUILD(lib);
    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::atomic<long> reads{0};
    auto reader = [&]() {
        while (!stop) {
            const auto s = lib.snapshot();
            if (!s || !s->containers.contains(QStringLiteral("0")) || s->itemCount != s->items.size())
                ++bad;
            ++reads;
        }
    };
    std::thread t1(reader), t2(reader);
    // Flip content on the owner thread while the readers hammer snapshot().
    for (int i = 0; i < 20; ++i) {
        m_config->setHidden(idEp1, i % 2 == 0);
        REBUILD(lib);
    }
    m_config->setHidden(idEp1, false);
    stop = true;
    t1.join();
    t2.join();
    QCOMPARE(bad.load(), 0);
    QVERIFY(reads.load() > 0);
}

void tst_dlna_library::system_update_id_continues_from_injected_value()
{
    DlnaLibrary lib(m_index, m_config, m_audio);
    lib.setInitialSystemUpdateId(41); // what the server stored last run
    QCOMPARE(lib.currentSystemUpdateId(), 41u);
    QCOMPARE(lib.snapshot()->systemUpdateId, 41u);
    lib.setInitialSystemUpdateId(7);  // never backwards
    QCOMPARE(lib.currentSystemUpdateId(), 41u);

    QSignalSpy changed(&lib, &DlnaLibrary::snapshotChanged);
    REBUILD(lib);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(lib.currentSystemUpdateId(), 42u);   // stored + 1
    REBUILD(lib);                                  // unchanged content: no bump
    QCOMPARE(lib.currentSystemUpdateId(), 42u);

    m_config->setHidden(idBook, true);
    REBUILD(lib);
    QCOMPARE(lib.currentSystemUpdateId(), 43u);
    lib.setInitialSystemUpdateId(10);              // late, lower value is still ignored
    QCOMPARE(lib.currentSystemUpdateId(), 43u);
    m_config->setHidden(idBook, false);
    REBUILD(lib);
    QCOMPARE(lib.currentSystemUpdateId(), 44u);

    // A restarted app: a new library seeded with the stored value continues, never repeats.
    DlnaLibrary next(m_index, m_config, m_audio);
    next.setInitialSystemUpdateId(lib.currentSystemUpdateId());
    REBUILD(next);
    QCOMPARE(next.currentSystemUpdateId(), 45u);
    // stop() revocation also moves the id forward (an empty shelf is a change).
    next.start();
    next.stop();
    QVERIFY(next.currentSystemUpdateId() > 45u);
}

void tst_dlna_library::build_runs_off_the_owner_thread_and_publishes_async()
{
    DlnaLibrary lib(m_index, m_config, m_audio);
    std::atomic<int> resolverCalls{0};
    std::atomic<bool> resolverOnOwnerThread{false};
    QThread* owner = QThread::currentThread();
    lib.setCoverResolver([&](const VaultIndex::FileRow& r) -> QString {
        ++resolverCalls;
        if (QThread::currentThread() == owner)
            resolverOnOwnerThread = true;
        return r.id == QLatin1String("vault:film") ? QStringLiteral("C:/covers/film.jpg") : QString();
    });
    QSignalSpy changed(&lib, &DlnaLibrary::snapshotChanged);

    lib.rebuildNow();
    // Returned immediately: still building, nothing published yet, event loop not entered.
    QVERIFY(lib.isBuilding());
    QCOMPARE(lib.snapshot()->itemCount, 0);
    QCOMPARE(changed.count(), 0);

    QTRY_VERIFY_WITH_TIMEOUT(!lib.isBuilding(), 10000);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(lib.snapshot()->itemCount, 17);
    QVERIFY(resolverCalls.load() > 0);
    QVERIFY2(!resolverOnOwnerThread.load(), "cover hook must run on the worker thread");
    QCOMPARE(findItemByTitle(*lib.snapshot(), "Inception")->coverPath, QStringLiteral("C:/covers/film.jpg"));
    QVERIFY(findItemByTitle(*lib.snapshot(), "Dune")->coverPath.isEmpty());
}

// /art serves jpg/jpeg/png only: a cover in any other format is never put in the snapshot.
void tst_dlna_library::cover_hook_results_are_limited_to_servable_images()
{
    DlnaLibrary lib(m_index, m_config, m_audio);
    lib.setCoverResolver([](const VaultIndex::FileRow& r) -> QString {
        if (r.id == QLatin1String("vault:film"))
            return QStringLiteral("C:/covers/film.webp");
        return QString();
    });
    lib.rebuildNow();
    QTRY_VERIFY_WITH_TIMEOUT(!lib.isBuilding(), 10000);
    QVERIFY(findItemByTitle(*lib.snapshot(), "Inception") != nullptr);
    QVERIFY(findItemByTitle(*lib.snapshot(), "Inception")->coverPath.isEmpty());
}

void tst_dlna_library::owner_change_during_build_triggers_one_more_build()
{
    DlnaLibrary lib(m_index, m_config, m_audio);
    QSignalSpy changed(&lib, &DlnaLibrary::snapshotChanged);

    lib.rebuildNow();            // build 1 reads the owners now (book visible)
    QVERIFY(lib.isBuilding());
    m_config->setHidden(idBook, true); // owners move while build 1 is in flight
    lib.rebuildNow();            // coalesced into a single queued follow-up
    lib.rebuildNow();
    lib.rebuildNow();
    QVERIFY(lib.isBuilding());

    QTRY_VERIFY_WITH_TIMEOUT(!lib.isBuilding(), 10000);
    // Build 1 published (with the book), the follow-up published the change: exactly two.
    QCOMPARE(changed.count(), 2);
    QVERIFY(!findItemByTitle(*lib.snapshot(), "Dune"));
    QCOMPARE(lib.snapshot()->itemCount, 16);
    m_config->setHidden(idBook, false);
}

void tst_dlna_library::stop_discards_a_build_in_flight()
{
    DlnaLibrary lib(m_index, m_config, m_audio);
    QSignalSpy changed(&lib, &DlnaLibrary::snapshotChanged);
    lib.start();
    lib.rebuildNow();
    QVERIFY(lib.isBuilding());
    lib.stop();                  // revoke while the worker is still running
    QTRY_VERIFY_WITH_TIMEOUT(!lib.isBuilding(), 10000);
    QTest::qWait(400);           // and no stray debounced rebuild either
    QCOMPARE(changed.count(), 0);
    QCOMPARE(lib.snapshot()->itemCount, 0);
    QVERIFY(lib.snapshot()->typeOrder.isEmpty());
}

QTEST_GUILESS_MAIN(tst_dlna_library)
#include "tst_dlna_library.moc"
