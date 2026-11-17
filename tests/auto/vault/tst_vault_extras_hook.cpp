// VaultLibrary::fileFolderExtras() through its real caller (the auto-identify pass), plus the
// declined marker: a deliberate detach survives rescans, a manual attach clears it.
#include "engine/VaultConfig.h"
#include "engine/VaultIdentifier.h"
#include "engine/VaultIndex.h"
#include "engine/VaultLibrary.h"
#include <QTemporaryDir>
#include <QtTest>

namespace
{
VaultIndex::FileRow row(const QString &path, const QString &identity = QString(), const QString &title = QString())
{
    VaultIndex::FileRow r;
    r.id = "vault:" + path;
    r.kind = "video";
    r.path = path;
    r.realName = QFileInfo(path).fileName();
    r.rootPath = "D:/Library";
    r.groupKey = r.subtreePath = QFileInfo(path).absolutePath();
    r.groupTitle = QFileInfo(path).completeBaseName();
    r.size = 100;
    r.mtimeMs = 1;
    r.identityId = identity;
    r.identityTitle = title;
    r.identitySource = identity.isEmpty() ? QString() : QString("IMDB");
    return r;
}
const QString kSop = "D:/Library/TV/The Sopranos - The Complete Series (Season 1, 2, 3, 4, 5 & 6) + Extras";
const QString kBonus = kSop + "/The Complete Series Bonus + Extras";
const QString kOffice = "D:/Library/Sitcoms/The Office (UK) Series 1 + 2 Christmas Specials And Extras/The Office - 1.1 - Downsize.avi";
QList<VaultIndex::FileRow> fixture()
{
    QList<VaultIndex::FileRow> rows;
    rows << row(kSop + "/Season 1/S01E01.mkv", "imdb:ttSop", "The Sopranos")
         << row(kSop + "/Season 2/S02E01.mkv", "imdb:ttSop", "The Sopranos")
         << row("D:/Library/Sitcoms/Parks and Recreation/S01E01.mkv", "imdb:ttParks", "Parks and Recreation")
         << row(kOffice);
    for (int i = 1; i <= 12; ++i)
        rows << row(QString("%1/Bonus Feature %2.mp4").arg(kBonus).arg(i));
    return rows;
}
} // namespace

class ExtrasHookTest : public QObject
{
    Q_OBJECT
  private slots:
    void hookFilesFolderExtrasAndDetachSurvivesRescans()
    {
        QTemporaryDir d;
        VaultIndex index(d.filePath("index.sqlite"));
        VaultConfig config(d.filePath("config"));
        VaultLibrary library(&index, nullptr, &config, nullptr, d.filePath("cache"));
        VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
        library.setIdentifier(&identifier);
        const auto rows = fixture();
        QVERIFY(index.publish(rows));

        const QString first = "vault:" + kBonus + "/Bonus Feature 1.mp4";
        QTRY_COMPARE(config.extrasParent(first), QString("imdb:ttSop"));
        int filed = 0;
        for (const auto &r : rows)
            if (!config.extrasParent(r.id).isEmpty())
            {
                ++filed;
                QVERIFY2(r.path.startsWith(kBonus), qPrintable(r.path));
            }
        QCOMPARE(filed, 12);
        QVERIFY(config.extrasParent("vault:" + kOffice).isEmpty()); // a sibling show's "... And Extras" folder

        // the user detaches one: removed, declined, and it survives a rescan and a restart
        QVERIFY(library.detachExtras(first));
        QVERIFY(config.extrasParent(first).isEmpty());
        QVERIFY(config.extrasDeclined(first));
        QVERIFY(index.publish(rows)); // rescan: schedules the pass again
        QTest::qWait(300);
        QVERIFY(config.extrasParent(first).isEmpty());
        QCOMPARE(config.extrasParent("vault:" + kBonus + "/Bonus Feature 2.mp4"), QString("imdb:ttSop"));
        VaultConfig reopened(d.filePath("config"));
        QVERIFY(reopened.extrasDeclined(first));
        QVERIFY(reopened.extrasParent(first).isEmpty());

        // a deliberate attach clears the marker
        QVERIFY(config.setExtrasParent({first}, "imdb:ttSop"));
        QVERIFY(!config.extrasDeclined(first));
        QCOMPARE(config.extrasParent(first), QString("imdb:ttSop"));
        VaultConfig again(d.filePath("config"));
        QVERIFY(!again.extrasDeclined(first));
    }
    void folderEndingInExtrasWithMatchedEpisodesIsAShow()
    {
        const QString show = "D:/Library/The Show + Extras";
        auto episode = row(show + "/Season 1/The Show Season 1 Episode 01 - Pilot.avi", "imdb:show", "The Show");
        auto bonus = row(show + "/The Complete Series Bonus + Extras/Interview.mp4");
        const auto parents = VaultIdentifier::physicalExtrasParents({episode, bonus});
        QCOMPARE(parents.value(bonus.id), QString("imdb:show"));
        QVERIFY(!parents.contains(episode.id));
        QVERIFY(VaultIdentifier::physicalExtrasParents({bonus}).isEmpty());
    }
    void samplesAndPromos()
    {
        QTemporaryDir d;
        VaultIndex index(d.filePath("index.sqlite"));
        VaultConfig config(d.filePath("config"));
        VaultLibrary library(&index, nullptr, &config, nullptr, d.filePath("cache"));
        VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
        QList<VaultIndex::FileRow> rows{row("D:/Library/Film.2010-GROUP/Film.mkv", "imdb:film", "Film")};
        const QStringList clips{"sample.avi", "Sample.mkv", "SW.1977.Sample.mkv", "batman.vs.robin.2015.720p.bluray.x264-sample.mkv", "Sample/clip.mkv", "Samples/clip.mkv", "SAMPLE/clip.mkv", "RARBG.com.mp4", "GROUP.mp4"};
        for (const auto& clip : clips) {
            auto r = row("D:/Library/Film.2010-GROUP/" + clip); r.groupKey = r.path; r.size = 5 * 1024 * 1024; rows << r;
            r.path.replace("Film.2010-GROUP", "Orphan-GROUP"); r.id = "vault:" + r.path; r.groupKey = r.path; rows << r;
        }
        auto etrg = row("D:/Library/Get.Out.2017-ETRG/ETRG.mp4"); etrg.groupKey = etrg.path; rows << etrg;
        auto large = row("D:/Library/Film.2010-GROUP/The.Sample.2019.1080p.mkv"); large.size = 1400LL * 1024 * 1024; rows << large;
        auto largePromo = row("D:/Library/Film.2010-GROUP/ETRG.mp4"); largePromo.size = 900LL * 1024 * 1024; rows << largePromo;
        auto word = row("D:/Library/Film.2010-GROUP/Sampler.mkv"); rows << word;
        auto orphanBonus = row("D:/Library/Unknown/Bonus/Interview.mp4"); rows << orphanBonus;
        QVERIFY(index.publish(rows)); library.setIdentifier(&identifier);
        QTRY_VERIFY(config.isHidden(etrg.id));
        for (const auto& r : rows) {
            if (r.path.contains("Orphan-GROUP")) QVERIFY2(config.isHidden(r.id), qPrintable(r.path));
            else if (r.identityId.isEmpty() && r.id != etrg.id && r.id != large.id && r.id != largePromo.id && r.id != word.id && r.id != orphanBonus.id)
                QCOMPARE(config.extrasParent(r.id), QString("imdb:film"));
        }
        for (const auto& r : {large, largePromo, word, orphanBonus}) { QVERIFY(!config.isHidden(r.id)); QVERIFY(config.extrasParent(r.id).isEmpty()); QVERIFY(VaultIdentifier::junkClipReason(r).isEmpty()); }
        auto adopted = row("D:/Library/Main-GROUP/Sample.mkv", "imdb:chosen", "Chosen movie");
        QVERIFY(VaultIdentifier::junkClipReason(adopted).isEmpty());
        auto exactLimit = row("D:/Library/Main-GROUP/Sample.mkv"); exactLimit.size = 400LL * 1024 * 1024;
        QVERIFY(VaultIdentifier::junkClipReason(exactLimit).isEmpty());
        exactLimit.path = "D:/Library/Main-GROUP/GROUP.mp4"; exactLimit.realName = "GROUP.mp4"; exactLimit.size = 60LL * 1024 * 1024;
        QVERIFY(VaultIdentifier::junkClipReason(exactLimit).isEmpty());
        config.setHidden(etrg.id, false);
        QVERIFY(index.publish(rows)); QTest::qWait(300); QVERIFY(!config.isHidden(etrg.id));
        VaultConfig again(d.filePath("config")); QVERIFY(!again.isHidden(etrg.id)); QVERIFY(again.extrasDeclined(etrg.id));
    }
    void declineIsPerFileAndPlainDetachOfUnfiledFilesStillWorks()
    {
        QTemporaryDir d;
        VaultConfig config(d.filePath("config"));
        QVERIFY(config.setExtrasParent({"a", "b"}, "imdb:tt1"));
        QVERIFY(config.declineExtras({"a"}));
        QVERIFY(config.extrasDeclined("a"));
        QVERIFY(!config.extrasDeclined("b"));
        QCOMPARE(config.extrasParent("b"), QString("imdb:tt1"));
        QVERIFY(config.extrasParent("a").isEmpty());
        QVERIFY(config.setExtrasParent({"b"}, QString())); // old behaviour: remove only, no decline
        QVERIFY(!config.extrasDeclined("b"));
    }
};
QTEST_GUILESS_MAIN(ExtrasHookTest)
#include "tst_vault_extras_hook.moc"
