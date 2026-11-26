#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <algorithm>
#include <QUuid>
#include "engine/VaultScanner.h"
#include "engine/VaultWatcher.h"
#include "engine/VaultConfig.h"
#include "engine/VaultIdentifier.h"

namespace {
QString media(const QString& dir, const QString& name) {
    if (!QDir().mkpath(dir)) return {};
    QFile file(dir + '/' + name);
    if (!file.open(QIODevice::WriteOnly) || file.write("synthetic-media") != 15) return {};
    return file.fileName();
}
struct Library {
    QTemporaryDir temp;
    QString parent = temp.path() + "/library";
    QString child = parent + "/Movies/Art";
    QString alpha = media(child + "/Alpha (2000)", "Alpha.2000.mkv");
    QString beta = media(child + "/Beta (2001)", "Beta.2001.mp4");
    VaultIndex index{temp.path() + "/index.sqlite"};
    VaultIdentity identity{temp.path() + "/identity"};
    VaultScanner scanner{&index, &identity};
    void publish(const QStringList& roots, const QList<VaultIndex::FileRow>& extras = {}) {
        const auto generation = scanner.nextGeneration();
        QList<VaultScanner::RawResult> scans;
        for (const auto& root : roots) scans.append(VaultScanner::buildScan(root, {}, generation, {}));
        scanner.applyPublish(scans, generation, extras);
    }
    bool seed() {
        auto rows = index.rowsForKind("video");
        int n = 0;
        for (auto& row : rows) {
            row.identityId = QString("imdb:tt%1").arg(++n);
            row.identityTitle = row.groupTitle;
            row.identitySource = "IMDB"; row.identityType = "movie";
            row.identityState = "adopted"; row.identityYear = 2000+n;
            row.identityRating = 8.5; row.identityGenres = "Drama";
            row.identitySynopsis = "Saved synopsis"; row.identityCoverUrl = "file:///saved-art";
            row.identityWorld = "Theatre"; row.durationSec = 7200; row.pages = 123;
            row.progressed = true; row.admissionVerdict = "Admitted";
            row.admissionDetail = "Saved admission"; row.coverRef = "file:///local-art";
        }
        return index.upsertMany(rows);
    }
};
bool sameFacts(const VaultIndex::FileRow& a, const VaultIndex::FileRow& b) {
    return a.id == b.id && a.identityId == b.identityId && a.identityTitle == b.identityTitle
        && a.identitySource == b.identitySource && a.identityType == b.identityType
        && a.identityState == b.identityState && a.identityCandidateCount == b.identityCandidateCount
        && a.identityYear == b.identityYear && a.identityRating == b.identityRating
        && a.identityGenres == b.identityGenres && a.identitySynopsis == b.identitySynopsis
        && a.identityCoverUrl == b.identityCoverUrl && a.identityWorld == b.identityWorld
        && a.identitySuppressed == b.identitySuppressed && a.progressed == b.progressed
        && a.durationSec == b.durationSec && a.pages == b.pages && a.coverRef == b.coverRef
        && a.admissionVerdict == b.admissionVerdict && a.admissionDetail == b.admissionDetail;
}
QMap<QString, VaultIndex::FileRow> byPath(VaultIndex& index) {
    QMap<QString,VaultIndex::FileRow> out;
    for (const auto& kind : index.kinds())
        for (const auto& row : index.rowsForKind(kind)) out.insert(row.path, row);
    return out;
}
}

class VaultIntegrityTest : public QObject {
    Q_OBJECT
private slots:
    void ownershipRespectsDriveRootsAndPathBoundaries() {
        const QString root=QDir::rootPath(), mediaRoot=QDir(root).filePath("media"), nested=mediaRoot+"/inner";
        QCOMPARE(VaultKit::ownerRootForPath({root},QDir(root).filePath("movie.mkv")),root);
        QCOMPARE(VaultKit::ownerRootForPath({mediaRoot,root},QDir(root).filePath("media-other/movie.mkv")),root);
        QCOMPARE(VaultKit::ownerRootForPath({root,nested,mediaRoot},nested+"/movie.mkv"),nested);
    }
    void rootOrderAndWatcher_data() { QTest::addColumn<bool>("childFirst"); QTest::newRow("child-parent") << true; QTest::newRow("parent-child") << false; }
    void rootOrderAndWatcher() {
        QFETCH(bool, childFirst);
        Library f; QVERIFY(f.index.isOpen()); QVERIFY(!f.alpha.isEmpty()); QVERIFY(!f.beta.isEmpty());
        f.publish({f.child}); QVERIFY(f.seed()); const auto saved = byPath(f.index);
        const QStringList roots = childFirst ? QStringList{f.child,f.parent} : QStringList{f.parent,f.child};
        VaultConfig config(f.temp.path()+"/config");
        for (const auto& root : roots) { config.addRoot(root); config.confirmRoot(root); }
        for (int pass=0; pass<2; ++pass) {
            f.publish(roots);
            QCOMPARE(f.index.itemCount(), 2); QCOMPARE(f.index.groupsForKind("video").size(), 2);
            QCOMPARE(f.index.rowsForRoot(f.child).size(), 2); QCOMPARE(f.index.rowsForRoot(f.parent).size(), 0);
            for (const auto& row : f.index.rowsForKind("video")) QVERIFY(sameFacts(row, saved.value(row.path)));
            VaultWatcher watcher(&f.index,&f.identity,&config);
            QCOMPARE(watcher.processRoot(f.parent,{},{}).landedCount, 0);
            QCOMPARE(watcher.processRoot(f.child,{},{}).landedCount, 0);
            for (const auto& row : f.index.rowsForKind("video")) QVERIFY(sameFacts(row, saved.value(row.path)));
        }
    }
    void nestedOfflineRecoveryAndRemoval() {
        Library f; f.publish({f.child,f.parent}); QVERIFY(f.seed()); const auto saved = byPath(f.index);
        const QString offline = f.temp.path()+"/offline-art";
        QVERIFY(QDir().rename(f.child, offline)); f.publish({f.child,f.parent});
        QCOMPARE(f.index.itemCount(),2);
        for (const auto& row : f.index.rowsForKind("video")) { QVERIFY(row.away); QVERIFY(sameFacts(row,saved.value(row.path))); }
        QVERIFY(QDir().rename(offline,f.child)); f.publish({f.parent,f.child});
        for (const auto& row : f.index.rowsForKind("video")) { QVERIFY(!row.away); QVERIFY(sameFacts(row,saved.value(row.path))); }
        f.publish({f.parent}); // explicit child removal hands physical membership to parent
        QCOMPARE(f.index.rowsForRoot(f.parent).size(),2); QCOMPARE(f.index.groupsForKind("video").size(),2);
        for (const auto& row : f.index.rowsForKind("video")) QVERIFY(sameFacts(row,saved.value(row.path)));
        VaultConfig config(f.temp.path()+"/config");config.addRoot(f.parent);config.confirmRoot(f.parent);
        VaultWatcher watcher(&f.index,&f.identity,&config);watcher.processRoot(f.parent,{},{});
        for (const auto& row : f.index.rowsForKind("video")) QVERIFY(sameFacts(row,saved.value(row.path)));
        f.publish({f.child}); // explicit parent removal leaves child untouched
        QCOMPARE(f.index.rowsForRoot(f.child).size(),2);
        for (const auto& row : f.index.rowsForKind("video")) QVERIFY(sameFacts(row,saved.value(row.path)));
    }
    void oldParentOwnedOfflineRowsAreRecovered() {
        Library f; f.publish({f.parent}); QVERIFY(f.seed()); const auto saved=byPath(f.index);
        auto corrupt=f.index.rowsForKind("video");for(auto&row:corrupt){row.groupKey=row.subtreePath=f.parent+"/Movies";row.groupTitle="Movies";}QVERIFY(f.index.upsertMany(corrupt));
        const auto offline=f.temp.path()+"/offline"; QVERIFY(QDir().rename(f.child,offline));
        f.publish({f.parent,f.child}); QCOMPARE(f.index.itemCount(),2); QCOMPARE(f.index.rowsForRoot(f.child).size(),2);QCOMPARE(f.index.groupsForKind("video").size(),2);
        for(const auto& row:f.index.rowsForKind("video")){QVERIFY(row.away);QVERIFY(sameFacts(row,saved.value(row.path)));}
    }
    void mixedKindsAgreeWithWatcherAndOverrides() {
        QTemporaryDir temp; const auto root=temp.path()+"/library", mixed=root+"/Mixed";
        QVERIFY(!media(mixed,"Alpha.2000.mkv").isEmpty()); QVERIFY(!media(mixed,"Beta.2001.mp4").isEmpty());
        QVERIFY(!media(mixed,"Book.pdf").isEmpty()); QVERIFY(!media(mixed,"Comic.cbz").isEmpty());
        VaultIndex index(temp.path()+"/i.sqlite"); VaultIdentity identity(temp.path()+"/identity"); VaultScanner scanner(&index,&identity);
        const auto generation=scanner.nextGeneration();const auto scan=VaultScanner::buildScan(root,{},generation,{});
        QCOMPARE(scan.rows.size(),4);scanner.applyPublish({scan},generation);QCOMPARE(index.itemCount(),4);
        QCOMPARE(index.itemCountForKind("book"),1);QCOMPARE(index.itemCountForKind("comic"),1);
        VaultWatcher watcher(&index,&identity,nullptr);QCOMPARE(watcher.processRoot(root,{},{}).landedCount,0);
        const auto g=scanner.nextGeneration();scanner.applyPublish({VaultScanner::buildScan(root,{},g,{})},g);QCOMPARE(index.itemCount(),4);
        const QString reclassified=root+"/Art books";QVERIFY(!media(reclassified,"One.cbz").isEmpty());QVERIFY(!media(reclassified,"Two.cbz").isEmpty());
        QMap<QString,QString> overrides{{QDir::cleanPath(reclassified).toLower(),"book"},{QDir::cleanPath(mixed).toLower(),"book"}};
        const auto g2=scanner.nextGeneration();const auto relabelled=VaultScanner::buildScan(root,{},g2,{},overrides);
        scanner.applyPublish({relabelled},g2);QCOMPARE(index.itemCount(),6);QCOMPARE(index.itemCountForKind("book"),3);QCOMPARE(index.itemCountForKind("video"),2);QCOMPARE(index.itemCountForKind("comic"),1);
        QVariantMap ov;for(auto it=overrides.constBegin();it!=overrides.constEnd();++it)ov.insert(it.key(),it.value());
        QCOMPARE(watcher.processRoot(root,{},ov).landedCount,0);
    }
    void flatFilmGroupsAndCorruptGroupActionsAreIsolated() {
        QTemporaryDir temp;const auto root=temp.path()+"/Films";
        QVERIFY(!media(root,"Alpha.2000.mkv").isEmpty());QVERIFY(!media(root,"Beta.2001.mp4").isEmpty());
        VaultIndex index(temp.path()+"/i.sqlite");VaultIdentity identity(temp.path()+"/identity");VaultScanner scanner(&index,&identity);
        auto g=scanner.nextGeneration();auto scan=VaultScanner::buildScan(root,{},g,{});scanner.applyPublish({scan},g);
        QCOMPARE(index.groupsForKind("video").size(),2);auto rows=index.rowsForKind("video");int n=0;
        for(auto&row:rows){row.identityId=QString("imdb:tt%1").arg(++n);row.identitySource="IMDB";row.identityTitle=row.groupTitle;row.identityType="movie";row.groupKey=row.subtreePath=root;row.groupTitle="Films";}
        QVERIFY(index.upsertMany(rows));VaultIdentifier identifier(&index,nullptr,nullptr,nullptr);VaultIdentifier::Match chosen;chosen.adopted=true;chosen.source="IMDB";chosen.sourceId="imdb:chosen";chosen.title="Chosen";chosen.type="movie";
        QVERIFY(!identifier.identifyGroupWith(root,chosen));QVERIFY(!identifier.unidentifyGroup(root));QVERIFY(!identifier.reshelveGroup(root,"book"));
        QCOMPARE(index.rowsForIdentity(rows.first().identityId).size(),1);QCOMPARE(index.rowsForIdentity(rows.last().identityId).size(),1);
        g=scanner.nextGeneration();scanner.applyPublish({VaultScanner::buildScan(root,{},g,{})},g);rows=index.rowsForKind("video");QCOMPARE(index.groupsForKind("video").size(),2);
        QVERIFY(identifier.identifyGroupWith(rows.first().groupKey,chosen));QCOMPARE(index.rowsForIdentity(rows.last().identityId).size(),1);
        QVERIFY(identifier.unidentifyGroup(rows.first().groupKey));QCOMPARE(index.rowsForIdentity(rows.last().identityId).size(),1);
    }
    void mixedEpisodeAndStandaloneFilmActionsAreRefused_data() {
        QTest::addColumn<bool>("identifiedEpisode");
        QTest::newRow("both-unidentified") << false;
        QTest::newRow("episode-identified") << true;
    }
    void mixedEpisodeAndStandaloneFilmActionsAreRefused() {
        QFETCH(bool, identifiedEpisode);
        QTemporaryDir temp;const auto root=temp.path()+"/Legacy";
        QVERIFY(!media(root,"Known.Show.S01E01.mkv").isEmpty());
        QVERIFY(!media(root,"Unrelated.Film.2000.mp4").isEmpty());
        VaultIndex index(temp.path()+"/i.sqlite");VaultIdentity identity(temp.path()+"/identity");VaultScanner scanner(&index,&identity);
        auto g=scanner.nextGeneration();scanner.applyPublish({VaultScanner::buildScan(root,{},g,{})},g);
        auto rows=index.rowsForKind("video");QCOMPARE(rows.size(),2);
        for(auto&row:rows){
            row.groupKey=row.subtreePath=root;row.groupTitle="Known Show";row.progressed=true;
            if(identifiedEpisode && VaultKit::isEpisodeVideo(row.realName)){
                row.identityId="imdb:known-show";row.identityTitle="Known Show";row.identitySource="IMDB";row.identityType="series";row.identityState="adopted";
            }
        }
        QVERIFY(index.upsertMany(rows));const auto before=byPath(index);
        VaultIdentifier identifier(&index,nullptr,nullptr,nullptr);VaultIdentifier::Match chosen;
        chosen.adopted=true;chosen.source="IMDB";chosen.sourceId="imdb:chosen-show";chosen.title="Chosen Show";chosen.type="series";
        QVERIFY(!identifier.identifyGroupWith(root,chosen));
        QVERIFY(!identifier.unidentifyGroup(root));
        QVERIFY(!identifier.reshelveGroup(root,"book"));
        for(const auto&row:index.rowsForKind("video"))QVERIFY(sameFacts(row,before.value(row.path)));
    }
    void genericMultipartFilmRemainsActionable() {
        QTemporaryDir temp;const auto root=temp.path()+"/Dune (2021)";
        QVERIFY(!media(root,"movie.mp4").isEmpty());QVERIFY(!media(root,"part2.mp4").isEmpty());
        VaultIndex index(temp.path()+"/i.sqlite");VaultIdentity identity(temp.path()+"/identity");VaultScanner scanner(&index,&identity);
        auto g=scanner.nextGeneration();scanner.applyPublish({VaultScanner::buildScan(root,{},g,{})},g);
        QCOMPARE(index.groupsForKind("video").size(),1);auto rows=index.rowsForKind("video");QCOMPARE(rows.size(),2);
        const auto group=rows.first().groupKey;QCOMPARE(group,root);
        VaultIdentifier identifier(&index,nullptr,nullptr,nullptr);VaultIdentifier::Match chosen;
        chosen.adopted=true;chosen.source="IMDB";chosen.sourceId="imdb:dune";chosen.title="Dune";chosen.type="movie";
        QVERIFY(identifier.identifyGroupWith(group,chosen));QCOMPARE(index.rowsForIdentity(chosen.sourceId).size(),2);
        QVERIFY(identifier.unidentifyGroup(group));
        for(const auto&row:index.rowsForKind("video")){QVERIFY(row.identityId.isEmpty());QVERIFY(row.identitySuppressed);}
        QVERIFY(identifier.reshelveGroup(group,"book"));QCOMPARE(index.itemCountForKind("book"),2);
    }
    void configNormalizesDuplicateRootSpellingsBeforePublication() {
        Library f;VaultConfig config(f.temp.path()+"/config");
        config.addRoot(f.child);config.confirmRoot(f.child);
        config.addRoot(f.child+"/./");
#ifdef Q_OS_WIN
        config.addRoot(f.child.toUpper());
#endif
        QCOMPARE(config.roots().size(),1);QStringList roots;
        for(const auto&value:config.roots())roots.append(value.toMap().value("path").toString());
        f.publish(roots);QCOMPARE(f.index.itemCount(),2);QCOMPARE(f.index.rowsForRoot(roots.first()).size(),2);
        VaultConfig reopened(f.temp.path()+"/config");QCOMPARE(reopened.roots().size(),1);QCOMPARE(reopened.roots().first().toMap().value("path").toString(),roots.first());
    }
    void colocatedMediaHaveSeparateActionableGroups() {
        QTemporaryDir temp;const auto root=temp.path()+"/library", mixed=root+"/Mixed";
        QVERIFY(!media(mixed,"Alpha.2000.mp4").isEmpty());QVERIFY(!media(mixed,"Volume 1.cbz").isEmpty());QVERIFY(!media(mixed,"Book.pdf").isEmpty());
        VaultIndex index(temp.path()+"/i.sqlite");VaultIdentity identity(temp.path()+"/identity");VaultScanner scanner(&index,&identity);
        auto publish=[&]{const auto g=scanner.nextGeneration();scanner.applyPublish({VaultScanner::buildScan(root,{},g,{})},g);};publish();
        QSet<QString> keys;auto original=byPath(index);
        for(auto row:original){keys.insert(row.groupKey);row.identityId=row.kind+":saved";row.identityTitle=row.groupTitle;row.identitySource="MANUAL";row.identityState="adopted";row.progressed=true;QVERIFY(index.upsert(row));}
        QCOMPARE(keys.size(),3);original=byPath(index);
        for(const auto&row:original){const auto group=index.rowsForGroup(row.groupKey);QCOMPARE(group.size(),1);QCOMPARE(group.first().kind,row.kind);QCOMPARE(index.filesForGroup(row.kind,row.groupKey).size(),1);}
        const auto comic=index.rowsForKind("comic").first();QCOMPARE(comic.groupKey,QString("comic::")+mixed);QCOMPARE(comic.subtreePath,mixed);
        QCOMPARE(index.filesForGroup("video",comic.groupKey).size(),0);
        VaultIdentifier identifier(&index,nullptr,nullptr,nullptr);VaultIdentifier::Match chosen;
        chosen.adopted=true;chosen.source="COMICS";chosen.sourceId="comics:chosen";chosen.title="Chosen Comic";
        QVERIFY(identifier.identifyGroupWith(comic.groupKey,chosen));QCOMPARE(index.rowsForIdentity(chosen.sourceId).size(),1);
        for(const auto&row:index.rowsForKind("video"))QVERIFY(sameFacts(row,original.value(row.path)));
        for(const auto&row:index.rowsForKind("book"))QVERIFY(sameFacts(row,original.value(row.path)));
        QVERIFY(identifier.reshelveGroup(comic.groupKey,"book"));QCOMPARE(index.itemCountForKind("book"),2);QCOMPARE(index.itemCountForKind("video"),1);
        for(const auto&row:index.rowsForKind("video"))QVERIFY(sameFacts(row,original.value(row.path)));
        // A legacy cross-medium key is guarded too, before the controlled scan repairs it.
        auto corrupt=byPath(index);for(auto row:corrupt){row.groupKey=mixed;QVERIFY(index.upsert(row));}
        const auto before=byPath(index);QVERIFY(!identifier.identifyGroupWith(mixed,chosen));QVERIFY(!identifier.unidentifyGroup(mixed));QVERIFY(!identifier.reshelveGroup(mixed,"video"));
        for(const auto&row:byPath(index))QVERIFY(sameFacts(row,before.value(row.path)));
        publish();keys.clear();for(const auto&row:byPath(index))keys.insert(row.groupKey);QCOMPARE(keys.size(),3);
        VaultWatcher watcher(&index,&identity,nullptr);QCOMPARE(watcher.processRoot(root,{},{}).landedCount,0);
    }
    void typedComicOverridesRoundTripWithoutRelabellingOtherMedia() {
        QTemporaryDir temp;const auto root=temp.path()+"/library", mixed=root+"/Mixed";
        QVERIFY(!media(mixed,"Alpha.2000.mp4").isEmpty());QVERIFY(!media(mixed,"Volume 1.cbz").isEmpty());QVERIFY(!media(mixed,"Volume 2.cbr").isEmpty());QVERIFY(!media(mixed,"Book.pdf").isEmpty());
        VaultIndex index(temp.path()+"/i.sqlite");VaultIdentity identity(temp.path()+"/identity");VaultScanner scanner(&index,&identity);VaultConfig config(temp.path()+"/config");config.addRoot(root);config.confirmRoot(root);
        auto publish=[&]{QMap<QString,QString> overrides;const auto values=config.kindOverrides();for(auto it=values.constBegin();it!=values.constEnd();++it)overrides.insert(it.key(),it.value().toString());const auto g=scanner.nextGeneration();scanner.applyPublish({VaultScanner::buildScan(root,{},g,{},overrides)},g);};publish();
        auto rows=byPath(index);for(auto row:rows){row.identityId=row.kind+":manual:"+row.id;row.identityTitle=row.displayTitle;row.identitySource="MANUAL";row.identityState="manual";row.progressed=true;QVERIFY(index.upsert(row));}
        const auto saved=byPath(index);const auto comicKey=index.rowsForKind("comic").first().groupKey;QCOMPARE(index.rowsForGroup(comicKey).size(),2);
        config.setKind(comicKey,"book");
        VaultWatcher watcher(&index,&identity,&config);
        for(int pass=0;pass<2;++pass){publish();QCOMPARE(index.itemCountForKind("comic"),0);QCOMPARE(index.itemCountForKind("book"),3);QCOMPARE(index.itemCountForKind("video"),1);QCOMPARE(watcher.processRoot(root,{},config.kindOverrides()).landedCount,0);for(const auto&row:byPath(index))QVERIFY(sameFacts(row,saved.value(row.path)));}
        // The visible book groups are now per-file; explicit return selections must
        // outrank the older comic-folder -> book declaration without changing file IDs.
        for(const auto&row:index.rowsForKind("book"))if(VaultKit::kindForFile(row.path)==VaultKit::MediaKind::Comic)config.setKind(row.groupKey,"comic");
        for(int pass=0;pass<2;++pass){publish();QCOMPARE(index.itemCountForKind("comic"),2);QCOMPARE(index.itemCountForKind("book"),1);QCOMPARE(index.itemCountForKind("video"),1);QCOMPARE(index.rowsForGroup(comicKey).size(),2);QCOMPARE(watcher.processRoot(root,{},config.kindOverrides()).landedCount,0);for(const auto&row:byPath(index))QVERIFY(sameFacts(row,saved.value(row.path)));}
        QCOMPARE(index.filesForGroup("comic",comicKey).size(),2);QCOMPARE(index.filesForGroup("video",comicKey).size(),0);
        QMap<QString,QString> overrides;const auto values=config.kindOverrides();for(auto it=values.constBegin();it!=values.constEnd();++it)overrides.insert(it.key(),it.value().toString());
        const auto broader=VaultScanner::buildScan(temp.path(),{},1,{},overrides);
        for(const auto&row:broader.rows)if(row.kind=="comic"){QCOMPARE(row.groupKey,comicKey);QCOMPARE(row.subtreePath,mixed);QVERIFY(row.subfolder.isEmpty());}
    }
    void episodeGroupsDoNotDependOnImportedAncestor() {
        QTemporaryDir temp;const auto root=temp.path()+"/library";
        const QStringList files{media(root+"/TV/Show Alpha/Season 1","S01E01.mkv"),
            media(root+"/TV/Show Beta/Season 1","S01E01.mkv"),
            media(root+"/TV/Release Pack/Season 1","Other.Show.S01E01.mp4")};
        const auto broad=VaultScanner::buildScan(root,{},1,{});QCOMPARE(broad.rows.size(),3);
        QSet<QString> groups;
        for(const auto& file:files){QVERIFY(!file.isEmpty());
            const auto row=std::find_if(broad.rows.begin(),broad.rows.end(),[&](const auto&r){return r.path==file;});QVERIFY(row!=broad.rows.end());groups.insert(row->groupKey);
            const auto narrow=VaultScanner::buildScan(QFileInfo(file).absolutePath(),{},1,{});QCOMPARE(narrow.rows.size(),1);QCOMPARE(narrow.rows.first().groupKey,row->groupKey);QCOMPARE(narrow.rows.first().groupTitle,row->groupTitle);
        }
        QCOMPARE(groups.size(),3);
    }
    void overlapRenameKeepsCanonicalIdentityAndFacts() {
        Library f;
        // Establish the unique-signature premise deterministically. Equal fake payloads
        // created in one clock tick legitimately trigger the existing copy ceremony.
        { QFile beta(f.beta);QVERIFY(beta.open(QIODevice::Append));QVERIFY(beta.write("distinct-beta")>0); }
        f.publish({f.child,f.parent});QVERIFY(f.seed());const auto before=f.index.rowsForPath(f.alpha).first();
        QVERIFY(before.size != f.index.rowsForPath(f.beta).first().size);
        const auto renamed=QFileInfo(f.alpha).absolutePath()+"/Alpha.2000.Renamed.mkv";QVERIFY(QFile::rename(f.alpha,renamed));
        f.publish({f.parent,f.child});const auto after=f.index.rowsForPath(renamed);QCOMPARE(after.size(),1);
        if(!sameFacts(before,after.first())) {
            const auto other=f.index.rowsForPath(f.beta).first();
            qWarning() << "rename before" << before.id << before.size << before.mtimeMs << before.identityId << before.identityState << before.durationSec;
            qWarning() << "rename after" << after.first().id << after.first().size << after.first().mtimeMs << after.first().identityId << after.first().identityState << after.first().durationSec;
            qWarning() << "other signature" << other.id << other.size << other.mtimeMs;
        }
        QCOMPARE(after.first().id,before.id);QVERIFY(sameFacts(before,after.first()));
        QCOMPARE(f.identity.idForFile(renamed,after.first().size,after.first().mtimeMs),before.id);
    }
    void syntheticOwnerAndHideHandoffPreserveFacts() {
        Library f;f.publish({f.child});QVERIFY(f.seed());const auto saved=f.index.rowsForPath(f.alpha).first();
        auto extra=VaultScanner::buildScan(f.child,{},1,{}).rows.first();extra.rootPath=f.temp.path()+"/downloads";extra.groupKey=extra.subtreePath=extra.path;
        f.publish({f.parent,f.child},{extra});const auto owned=f.index.rowsForPath(extra.path).first();QCOMPARE(owned.rootPath,extra.rootPath);QVERIFY(sameFacts(owned,saved));
        VaultConfig config(f.temp.path()+"/config");config.addRoot(f.child);config.confirmRoot(f.child);config.addSyntheticRoot(extra.rootPath);
        VaultWatcher watcher(&f.index,&f.identity,&config);watcher.processRoot(f.child,{},{});QVERIFY(sameFacts(f.index.rowsForPath(extra.path).first(),saved));QCOMPARE(f.index.rowsForPath(extra.path).first().rootPath,extra.rootPath);
        config.setRootHidden(extra.rootPath,true);f.publish({f.parent,f.child});const auto handed=f.index.rowsForPath(extra.path).first();QCOMPARE(handed.rootPath,f.child);QVERIFY(sameFacts(handed,saved));
    }
    void syntheticComicKeyIsQualifiedWithoutChangingPhysicalItems() {
        QTemporaryDir temp;const auto root=temp.path()+"/library", folder=root+"/Comic";
        const auto file=media(folder,"Volume 1.cbz");QVERIFY(!file.isEmpty());
        VaultIndex index(temp.path()+"/i.sqlite");VaultIdentity identity(temp.path()+"/identity");VaultScanner scanner(&index,&identity);
        auto scan=VaultScanner::buildScan(root,{},1,{});QCOMPARE(scan.rows.size(),1);
        scanner.applyPublish({scan},scanner.nextGeneration());auto saved=index.rowsForKind("comic").first();saved.identityId="comics:manual";saved.identitySource="MANUAL";saved.identityTitle="Comic";saved.identityState="manual";saved.progressed=true;QVERIFY(index.upsert(saved));
        auto derived=scan.rows.first();derived.groupKey=derived.subtreePath="Download series";derived.rootPath=temp.path()+"/downloads";
        scanner.applyPublish({scan},scanner.nextGeneration(),{derived});const auto row=index.rowsForKind("comic").first();QCOMPARE(row.groupKey,QString("comic::Download series"));QCOMPARE(row.subtreePath,QString("Download series"));QVERIFY(sameFacts(row,saved));
        QCOMPARE(index.filesForGroup("comic",row.groupKey).first().toMap().value("path").toString(),file);
        scanner.applyPublish({scan},scanner.nextGeneration());const auto handed=index.rowsForKind("comic").first();QCOMPARE(handed.groupKey,QString("comic::")+folder);QCOMPARE(handed.subtreePath,folder);QVERIFY(sameFacts(handed,saved));
    }
    void completeDurableFactsAndSuppressionSurviveRawReplacement() {
        Library f;f.publish({f.child});QVERIFY(f.seed());auto saved=f.index.rowsForKind("video");
        saved[0].identityState="ambiguous";saved[0].identityCandidateCount=3;
        saved[1].identityId.clear();saved[1].identityTitle.clear();saved[1].identitySource.clear();saved[1].identityType.clear();saved[1].identityYear=0;saved[1].identityRating=0;saved[1].identityGenres.clear();saved[1].identitySynopsis.clear();saved[1].identityCoverUrl.clear();saved[1].identityWorld.clear();saved[1].identitySuppressed=true;saved[1].identityState="suppressed";
        QVERIFY(f.index.upsertMany(saved));const auto original=byPath(f.index);f.publish({f.parent,f.child});
        for(const auto& row:f.index.rowsForKind("video"))QVERIFY(sameFacts(row,original.value(row.path)));
        // Force an unchanged owner transfer through incremental reconciliation too.
        VaultConfig config(f.temp.path()+"/config");config.addRoot(f.parent);config.confirmRoot(f.parent);VaultWatcher watcher(&f.index,&f.identity,&config);watcher.processRoot(f.parent,{},{});
        for(const auto& row:f.index.rowsForKind("video"))QVERIFY(sameFacts(row,original.value(row.path)));
    }
};
QTEST_GUILESS_MAIN(VaultIntegrityTest)
#include "tst_vault_integrity.moc"
