#include "vinyl/VinylLocalSource.h"
#include "engine/VaultIdentity.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantList>
#include <memory>

namespace {
struct Tags {
    QString title;
    QString artist = QStringLiteral("Nas");
    QString album = QStringLiteral("Illmatic XX");
    QString albumArtist = QStringLiteral("Nas");
    QString disc = QStringLiteral("1/1");
    QString track = QStringLiteral("1/1");
    QString date = QStringLiteral("2014");
    QString genre = QStringLiteral("Hip Hop");
};

QString albumArtist(const QVariantMap &album) {
    const QString explicitAlbumArtist = album.value(QStringLiteral("albumArtist")).toString();
    if (!explicitAlbumArtist.isEmpty()) return explicitAlbumArtist;
    const QString artistObject = album.value(QStringLiteral("artist")).toMap()
        .value(QStringLiteral("name")).toString();
    if (!artistObject.isEmpty()) return artistObject;
    const QStringList artists = album.value(QStringLiteral("artists")).toStringList();
    return artists.isEmpty() ? QString() : artists.first();
}

QVariantMap albumNamed(const QVariantList &albums, const QString &title, const QString &artist = {}) {
    for (const auto &value : albums) {
        const auto album = value.toMap();
        if (album.value(QStringLiteral("title")).toString() != title) continue;
        if (!artist.isEmpty() && albumArtist(album) != artist) continue;
        return album;
    }
    return {};
}
}

class VinylTaggedImportTest final : public QObject {
    Q_OBJECT
private slots:
    void init();
    void initTestCase();
    void sameTitleWithDifferentAlbumArtistsStaysSeparate();
    void matchingDiscSuffixesGroupAndSort();
    void explicitDiscNumbersUseMatchingFolderSuffixWithoutTotal();
    void suffixNormalizationNeedsMatchingMultidiscTags();
    void manifestAudioDoesNotCreateAnAutomaticDuplicate();
    void manifestImportReplacesPriorAutomaticAlbum();
    void manifestRejectsAudioChangedAfterInspection();
    void missingAlbumArtistGroupsByAlbumAndKeepsTrackArtists();
    void embeddedAndSidecarCoversResolveToLocalImages();
    void partialReimportMergesWithoutChangingIdentity();
    void taglessAudioRemainsUsable();
    void missingInputReturnsAnError();
    void cancellationSuppressesCompletion();
    void profileFenceSuppressesCompletion();

private:
    QString makeFlac(const QString &relativePath, const Tags *tags = nullptr, const QString &picturePath = {});
    QString writePixel(const QString &relativePath);
    QString makeLibraryCopies(const QString &seed, int count);
    void waitForImport(QSignalSpy &completed, int requestId, QVariantMap *result);

    QString m_root;
    QString m_registry;
    QString m_ffmpeg;
    std::unique_ptr<QTemporaryDir> m_temp;
};

void VinylTaggedImportTest::init() {
    m_temp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temp->isValid());
    m_root = m_temp->path();
    m_registry = m_root + QStringLiteral("/vault");
    m_ffmpeg = qEnvironmentVariable("VINYL_TEST_FFMPEG");
    QVERIFY2(!m_ffmpeg.isEmpty(), "Set VINYL_TEST_FFMPEG for temporary audio fixtures");
    QVERIFY2(QFileInfo::exists(m_ffmpeg), qPrintable(m_ffmpeg));
}

void VinylTaggedImportTest::initTestCase() {
    QCoreApplication::setApplicationName(QStringLiteral("vinyl-tagged-import-test"));
    QStandardPaths::setTestModeEnabled(true);
}

QString VinylTaggedImportTest::makeFlac(const QString &relativePath, const Tags *tags, const QString &picturePath) {
    const QString path = QDir(m_root).filePath(relativePath);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return {};

    QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("anullsrc=r=44100:cl=stereo"),
    };
    if (!picturePath.isEmpty()) args << QStringLiteral("-i") << picturePath;
    if (tags) {
        const auto metadata = [&args](const QString &name, const QString &value) {
            if (!value.isEmpty()) args << QStringLiteral("-metadata") << name + QLatin1Char('=') + value;
        };
        metadata(QStringLiteral("title"), tags->title);
        metadata(QStringLiteral("artist"), tags->artist);
        metadata(QStringLiteral("album"), tags->album);
        metadata(QStringLiteral("album_artist"), tags->albumArtist);
        metadata(QStringLiteral("date"), tags->date);
        metadata(QStringLiteral("disc"), tags->disc);
        metadata(QStringLiteral("track"), tags->track);
        metadata(QStringLiteral("genre"), tags->genre);
    }
    if (!picturePath.isEmpty())
        args << QStringLiteral("-map") << QStringLiteral("0:a:0")
             << QStringLiteral("-map") << QStringLiteral("1:v:0");
    args << QStringLiteral("-t") << QStringLiteral("0.08")
         << QStringLiteral("-c:a") << QStringLiteral("flac");
    if (!picturePath.isEmpty())
        args << QStringLiteral("-c:v") << QStringLiteral("copy")
             << QStringLiteral("-disposition:v:0") << QStringLiteral("attached_pic");
    args << QStringLiteral("-compression_level") << QStringLiteral("0") << path;

    QProcess process;
    process.start(m_ffmpeg, args);
    if (!process.waitForStarted(5000) || !process.waitForFinished(20000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qWarning().noquote() << "ffmpeg fixture generation failed:" << process.errorString()
                             << process.readAllStandardError();
        return {};
    }
    return QFileInfo::exists(path) && QFileInfo(path).size() > 0 ? path : QString();
}

QString VinylTaggedImportTest::writePixel(const QString &relativePath) {
    const QString path = QDir(m_root).filePath(relativePath);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return {};
    const QByteArray png = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jG9kAAAAASUVORK5CYII=");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size()) return {};
    file.close();
    return path;
}

QString VinylTaggedImportTest::makeLibraryCopies(const QString &seed, int count) {
    const QString directory = QDir(m_root).filePath(QStringLiteral("many-tracks"));
    if (!QDir().mkpath(directory)) return {};
    for (int i = 0; i < count; ++i) {
        const QString path = QDir(directory).filePath(QStringLiteral("track-%1.flac").arg(i, 3, 10, QLatin1Char('0')));
        if (!QFile::copy(seed, path)) return {};
    }
    return directory;
}

void VinylTaggedImportTest::waitForImport(QSignalSpy &completed, int requestId, QVariantMap *result) {
    QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(), 20000);
    if (completed.isEmpty()) return;
    const QList<QVariant> args = completed.takeFirst();
    QCOMPARE(args.at(0).toInt(), requestId);
    *result = args.at(1).toMap();
}

void VinylTaggedImportTest::sameTitleWithDifferentAlbumArtistsStaysSeparate() {
    Tags a1{QStringLiteral("First by A"), QStringLiteral("Various"), QStringLiteral("Shared Title"),
            QStringLiteral("Artist A"), QStringLiteral("1/1"), QStringLiteral("1/2")};
    Tags a2 = a1; a2.title = QStringLiteral("Second by A"); a2.track = QStringLiteral("2/2");
    Tags b1 = a1; b1.title = QStringLiteral("First by B"); b1.albumArtist = QStringLiteral("Artist B"); b1.track = QStringLiteral("1/2");
    Tags b2 = b1; b2.title = QStringLiteral("Second by B"); b2.track = QStringLiteral("2/2");
    const QString p1 = makeFlac(QStringLiteral("artists/a1.flac"), &a1);
    const QString p2 = makeFlac(QStringLiteral("artists/a2.flac"), &a2);
    const QString p3 = makeFlac(QStringLiteral("artists/b1.flac"), &b1);
    const QString p4 = makeFlac(QStringLiteral("artists/b2.flac"), &b2);
    QVERIFY(!p1.isEmpty() && !p2.isEmpty() && !p3.isEmpty() && !p4.isEmpty());

    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QVERIFY(completed.isValid());
    const int requestId = source.startImport({p1, p2, p3, p4});
    QCOMPARE(completed.count(), 0);
    QVariantMap result;
    waitForImport(completed, requestId, &result);

    const QVariantList albums = result.value(QStringLiteral("albums")).toList();
    QCOMPARE(result.value(QStringLiteral("errors")).toStringList().size(), 0);
    QCOMPARE(albums.size(), 2);
    QSet<QString> ids;
    QSet<QString> artists;
    for (const auto &value : albums) {
        const QVariantMap album = value.toMap();
        QCOMPARE(album.value(QStringLiteral("title")).toString(), QStringLiteral("Shared Title"));
        QCOMPARE(album.value(QStringLiteral("tracks")).toList().size(), 2);
        ids.insert(album.value(QStringLiteral("id")).toString());
        artists.insert(albumArtist(album));
    }
    QCOMPARE(ids.size(), 2);
    const QSet<QString> expectedArtists{QStringLiteral("Artist A"), QStringLiteral("Artist B")};
    QCOMPARE(artists, expectedArtists);
}

void VinylTaggedImportTest::matchingDiscSuffixesGroupAndSort() {
    Tags disc1b{QStringLiteral("Second Song"), QStringLiteral("Nas"), QStringLiteral("Illmatic XX CD 1"),
                QStringLiteral("Nas"), QStringLiteral("1/2"), QStringLiteral("2/10")};
    Tags disc1a = disc1b; disc1a.title = QStringLiteral("First Song"); disc1a.track = QStringLiteral("1/10");
    Tags disc2a = disc1b; disc2a.title = QStringLiteral("Bonus Song"); disc2a.album = QStringLiteral("Illmatic XX CD 2");
    disc2a.disc = QStringLiteral("2/2"); disc2a.track = QStringLiteral("1/4");
    const QString first = makeFlac(QStringLiteral("collection/CD 1/02.flac"), &disc1b);
    const QString second = makeFlac(QStringLiteral("collection/CD 1/01.flac"), &disc1a);
    const QString third = makeFlac(QStringLiteral("collection/CD 2/01.flac"), &disc2a);
    QVERIFY(!first.isEmpty() && !second.isEmpty() && !third.isEmpty());

    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QSignalSpy progress(&source, &VinylLocalSource::importProgress);
    QVERIFY(completed.isValid() && progress.isValid());
    const QString directory = QDir(m_root).filePath(QStringLiteral("collection"));
    const int requestId = source.startImport({directory, first}); // The same path is discovered twice.
    QVariantMap result;
    waitForImport(completed, requestId, &result);

    QCOMPARE(result.value(QStringLiteral("errors")).toStringList().size(), 0);
    QCOMPARE(result.value(QStringLiteral("importedTracks")).toInt(), 3);
    QCOMPARE(result.value(QStringLiteral("albums")).toList().size(), 1);
    const QVariantMap album = result.value(QStringLiteral("albums")).toList().first().toMap();
    QCOMPARE(album.value(QStringLiteral("title")).toString(), QStringLiteral("Illmatic XX"));
    const QVariantList tracks = album.value(QStringLiteral("tracks")).toList();
    QCOMPARE(tracks.size(), 3);
    QCOMPARE(tracks.at(0).toMap().value(QStringLiteral("disc")).toInt(), 1);
    QCOMPARE(tracks.at(0).toMap().value(QStringLiteral("number")).toInt(), 1);
    QCOMPARE(tracks.at(1).toMap().value(QStringLiteral("number")).toInt(), 2);
    QCOMPARE(tracks.at(2).toMap().value(QStringLiteral("disc")).toInt(), 2);
    QCOMPARE(tracks.at(0).toMap().value(QStringLiteral("taggedAlbumTitle")).toString(), QStringLiteral("Illmatic XX CD 1"));
    QCOMPARE(tracks.at(2).toMap().value(QStringLiteral("taggedAlbumTitle")).toString(), QStringLiteral("Illmatic XX CD 2"));
    QVERIFY(progress.count() >= 3);
    for (const auto &event : progress) QCOMPARE(event.at(0).toInt(), requestId);
}

void VinylTaggedImportTest::explicitDiscNumbersUseMatchingFolderSuffixWithoutTotal() {
    Tags disc1{QStringLiteral("Disc One Song"), QStringLiteral("Nas"), QStringLiteral("Two Disc Album"),
              QStringLiteral("Nas"), QStringLiteral("1"), QStringLiteral("1")};
    Tags disc2 = disc1; disc2.title = QStringLiteral("Disc Two Song"); disc2.disc = QStringLiteral("2");
    const QString first = makeFlac(QStringLiteral("without-total/CD 1/01.flac"), &disc1);
    const QString second = makeFlac(QStringLiteral("without-total/CD 2/01.flac"), &disc2);
    QVERIFY(!first.isEmpty() && !second.isEmpty());

    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QVERIFY(completed.isValid());
    const QString directory = QDir(m_root).filePath(QStringLiteral("without-total"));
    const int requestId = source.startImport({directory});
    QVariantMap result;
    waitForImport(completed, requestId, &result);

    QCOMPARE(result.value(QStringLiteral("errors")).toStringList().size(), 0);
    QCOMPARE(result.value(QStringLiteral("albums")).toList().size(), 1);
    const QVariantMap album = result.value(QStringLiteral("albums")).toList().first().toMap();
    QCOMPARE(album.value(QStringLiteral("title")).toString(), QStringLiteral("Two Disc Album"));
    const QVariantList tracks = album.value(QStringLiteral("tracks")).toList();
    QCOMPARE(tracks.size(), 2);
    QCOMPARE(tracks.at(0).toMap().value(QStringLiteral("disc")).toInt(), 1);
    QCOMPARE(tracks.at(1).toMap().value(QStringLiteral("disc")).toInt(), 2);
}

void VinylTaggedImportTest::suffixNormalizationNeedsMatchingMultidiscTags() {
    Tags single{QStringLiteral("One"), QStringLiteral("Artist"), QStringLiteral("Solo CD 1"),
                QStringLiteral("Artist"), QStringLiteral("1/1"), QStringLiteral("1/1")};
    Tags mismatch{QStringLiteral("Two"), QStringLiteral("Artist"), QStringLiteral("Mismatch CD 2"),
                  QStringLiteral("Artist"), QStringLiteral("1/2"), QStringLiteral("1/1")};
    const QString singlePath = makeFlac(QStringLiteral("suffix/single.flac"), &single);
    const QString mismatchPath = makeFlac(QStringLiteral("suffix/mismatch.flac"), &mismatch);
    QVERIFY(!singlePath.isEmpty() && !mismatchPath.isEmpty());

    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QVERIFY(completed.isValid());
    const int requestId = source.startImport({singlePath, mismatchPath});
    QVariantMap result;
    waitForImport(completed, requestId, &result);
    QCOMPARE(result.value(QStringLiteral("errors")).toStringList().size(), 0);

    QStringList titles;
    for (const auto &value : result.value(QStringLiteral("albums")).toList())
        titles.append(value.toMap().value(QStringLiteral("title")).toString());
    titles.sort();
    const QStringList expectedTitles{QStringLiteral("Mismatch CD 2"), QStringLiteral("Solo CD 1")};
    QCOMPARE(titles, expectedTitles);
}

void VinylTaggedImportTest::manifestAudioDoesNotCreateAnAutomaticDuplicate() {
    Tags tags{QStringLiteral("Tag Title"), QStringLiteral("Nas"), QStringLiteral("Tag Album"),
              QStringLiteral("Nas"), QStringLiteral("1/1"), QStringLiteral("1/1")};
    const QString audio = makeFlac(QStringLiteral("manifest/track.flac"), &tags);
    QVERIFY(!audio.isEmpty());
    const QString manifest = QDir(m_root).filePath(QStringLiteral("manifest/collection.vinyl.json"));
    QFile file(manifest);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray contents = R"({"version":1,"artist":{"key":"nas","name":"Nas"},"releases":[{"key":"curated","title":"Curated Album","kind":"album","editions":[{"key":"standard","tracks":[{"file":"track.flac","title":"Curated Song","disc":1,"number":1}]}]}]})";
    QCOMPARE(file.write(contents), qint64(contents.size()));
    file.close();

    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QVERIFY(completed.isValid());
    const QString directory = QDir(m_root).filePath(QStringLiteral("manifest"));
    const int requestId = source.startImport({directory});
    QVariantMap result;
    waitForImport(completed, requestId, &result);

    QCOMPARE(result.value(QStringLiteral("errors")).toStringList().size(), 0);
    QCOMPARE(result.value(QStringLiteral("albums")).toList().size(), 1);
    QCOMPARE(result.value(QStringLiteral("importedTracks")).toInt(), 1);
    const QVariantMap album = result.value(QStringLiteral("albums")).toList().first().toMap();
    QCOMPARE(album.value(QStringLiteral("title")).toString(), QStringLiteral("Curated Album"));
    const QVariantList tracks = album.value(QStringLiteral("tracks")).toList();
    QCOMPARE(tracks.size(), 1);
    QCOMPARE(tracks.first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Curated Song"));
    QCOMPARE(result.value(QStringLiteral("replacedManifestPaths")).toStringList().size(), 1);
}

void VinylTaggedImportTest::manifestImportReplacesPriorAutomaticAlbum() {
    Tags tags{QStringLiteral("Tagged Song"), QStringLiteral("Nas"), QStringLiteral("Tagged Release"),
              QStringLiteral("Nas"), QStringLiteral("1/1"), QStringLiteral("1/1")};
    const QString audio = makeFlac(QStringLiteral("manifest-replaces/track.flac"), &tags);
    QVERIFY(!audio.isEmpty());
    const QString directory = QDir(m_root).filePath(QStringLiteral("manifest-replaces"));
    const QString manifest = QDir(directory).filePath(QStringLiteral("collection.vinyl.json"));

    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QVERIFY(completed.isValid());
    const int initialRequest = source.startImport({audio});
    QVariantMap initialResult;
    waitForImport(completed, initialRequest, &initialResult);
    QCOMPARE(initialResult.value(QStringLiteral("albums")).toList().size(), 1);
    const QVariantMap automaticAlbum = initialResult.value(QStringLiteral("albums")).toList().first().toMap();
    const QString automaticAlbumId = automaticAlbum.value(QStringLiteral("id")).toString();
    QVERIFY(!automaticAlbumId.isEmpty());
    source.setAlbums({automaticAlbum});

    QFile file(manifest);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray contents = R"({"version":1,"artist":{"key":"nas","name":"Nas"},"releases":[{"key":"curated","title":"Curated Edition","kind":"album","editions":[{"key":"standard","tracks":[{"file":"track.flac","title":"Curated Song","disc":1,"number":1}]}]}]})";
    QCOMPARE(file.write(contents), qint64(contents.size()));
    file.close();

    const int requestId = source.startImport({directory});
    QVariantMap result;
    waitForImport(completed, requestId, &result);
    QCOMPARE(result.value(QStringLiteral("errors")).toStringList().size(), 0);
    QCOMPARE(result.value(QStringLiteral("albums")).toList().size(), 1);
    QCOMPARE(result.value(QStringLiteral("albums")).toList().first().toMap()
                 .value(QStringLiteral("title")).toString(), QStringLiteral("Curated Edition"));
    QVERIFY(result.value(QStringLiteral("replacedAlbumIds")).toStringList().contains(automaticAlbumId));
}

void VinylTaggedImportTest::manifestRejectsAudioChangedAfterInspection() {
    Tags tags{QStringLiteral("Tagged Song"), QStringLiteral("Nas"), QStringLiteral("Tagged Release"),
              QStringLiteral("Nas"), QStringLiteral("1/1"), QStringLiteral("1/1")};
    const QString audio = makeFlac(QStringLiteral("manifest-race/track.flac"), &tags);
    QVERIFY(!audio.isEmpty());
    const QString directory = QDir(m_root).filePath(QStringLiteral("manifest-race"));
    const QString manifest = QDir(directory).filePath(QStringLiteral("collection.vinyl.json"));
    QFile manifestFile(manifest);
    QVERIFY(manifestFile.open(QIODevice::WriteOnly));
    const QByteArray contents = R"({"version":1,"artist":{"key":"nas","name":"Nas"},"releases":[{"key":"curated","title":"Race Edition","kind":"album","editions":[{"key":"standard","tracks":[{"file":"track.flac","title":"Curated Song","disc":1,"number":1}]}]}]})";
    QCOMPARE(manifestFile.write(contents), qint64(contents.size()));
    manifestFile.close();

    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QSignalSpy progress(&source, &VinylLocalSource::importProgress);
    QVERIFY(completed.isValid() && progress.isValid());
    bool modifiedAfterInspection = false;
    connect(&source, &VinylLocalSource::importProgress, &source,
            [&](int, int processed, int total, const QString &) {
        if (processed != total || modifiedAfterInspection) return;
        QFile changed(audio);
        if (changed.open(QIODevice::WriteOnly | QIODevice::Append)) {
            const QByteArray suffix("race-after-inspection");
            modifiedAfterInspection = changed.write(suffix) == suffix.size();
            changed.close();
        }
    });
    const int requestId = source.startImport({directory});
    QVariantMap result;
    waitForImport(completed, requestId, &result);

    QVERIFY(modifiedAfterInspection);
    QVERIFY(result.value(QStringLiteral("albums")).toList().isEmpty());
    QVERIFY(!result.value(QStringLiteral("errors")).toStringList().isEmpty());
    QVERIFY(result.value(QStringLiteral("replacedManifestPaths")).toStringList().isEmpty());
}

void VinylTaggedImportTest::missingAlbumArtistGroupsByAlbumAndKeepsTrackArtists() {
    Tags first{QStringLiteral("Singer A Song"), QStringLiteral("Singer A"), QStringLiteral("Shared Compilation"),
              QString(), QStringLiteral("1/1"), QStringLiteral("1/2")};
    Tags second = first; second.title = QStringLiteral("Singer B Song");
    second.artist = QStringLiteral("Singer B"); second.track = QStringLiteral("2/2");
    const QString firstPath = makeFlac(QStringLiteral("compilation/01.flac"), &first);
    const QString secondPath = makeFlac(QStringLiteral("compilation/02.flac"), &second);
    QVERIFY(!firstPath.isEmpty() && !secondPath.isEmpty());

    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QVERIFY(completed.isValid());
    const QString directory = QDir(m_root).filePath(QStringLiteral("compilation"));
    const int requestId = source.startImport({directory});
    QVariantMap result;
    waitForImport(completed, requestId, &result);

    QCOMPARE(result.value(QStringLiteral("errors")).toStringList().size(), 0);
    QCOMPARE(result.value(QStringLiteral("albums")).toList().size(), 1);
    const QVariantMap album = result.value(QStringLiteral("albums")).toList().first().toMap();
    QCOMPARE(album.value(QStringLiteral("title")).toString(), QStringLiteral("Shared Compilation"));
    QVERIFY(!album.value(QStringLiteral("albumArtistTagged")).toBool());
    const QVariantList tracks = album.value(QStringLiteral("tracks")).toList();
    QCOMPARE(tracks.size(), 2);
    QSet<QString> trackArtists;
    for (const auto &value : tracks) {
        const QStringList artists = value.toMap().value(QStringLiteral("artists")).toStringList();
        if (!artists.isEmpty()) trackArtists.insert(artists.first());
    }
    const QSet<QString> expectedArtists{QStringLiteral("Singer A"), QStringLiteral("Singer B")};
    QCOMPARE(trackArtists, expectedArtists);
}

void VinylTaggedImportTest::embeddedAndSidecarCoversResolveToLocalImages() {
    const QString embeddedSourceImage = writePixel(QStringLiteral("artwork-source/embedded.png"));
    QVERIFY(!embeddedSourceImage.isEmpty());
    Tags embeddedTags{QStringLiteral("Embedded Art Track"), QStringLiteral("Nas"), QStringLiteral("Embedded Art Album"),
                      QStringLiteral("Nas"), QStringLiteral("1/1"), QStringLiteral("1/1")};
    const QString embeddedAudio = makeFlac(QStringLiteral("embedded/track.flac"), &embeddedTags, embeddedSourceImage);
    QVERIFY(!embeddedAudio.isEmpty());

    const QString sidecarImage = writePixel(QStringLiteral("sidecar/Cover/front.png"));
    QVERIFY(!sidecarImage.isEmpty());
    const QString sidecarAudio = makeFlac(QStringLiteral("sidecar/track.flac"));
    QVERIFY(!sidecarAudio.isEmpty());

    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QVERIFY(completed.isValid());
    const int requestId = source.startImport({embeddedAudio, sidecarAudio});
    QVariantMap result;
    waitForImport(completed, requestId, &result);

    QCOMPARE(result.value(QStringLiteral("errors")).toStringList().size(), 0);
    const QVariantList albums = result.value(QStringLiteral("albums")).toList();
    QCOMPARE(albums.size(), 2);
    const QVariantMap embeddedAlbum = albumNamed(albums, QStringLiteral("Embedded Art Album"), QStringLiteral("Nas"));
    QVERIFY(!embeddedAlbum.isEmpty());
    const QString embeddedCover = QUrl(embeddedAlbum.value(QStringLiteral("cover")).toString()).toLocalFile();
    QVERIFY(QFileInfo::exists(embeddedCover));
    QFile embeddedFile(embeddedCover);
    QVERIFY(embeddedFile.open(QIODevice::ReadOnly));
    QFile sourceImage(embeddedSourceImage);
    QVERIFY(sourceImage.open(QIODevice::ReadOnly));
    QCOMPARE(embeddedFile.readAll(), sourceImage.readAll());

    const QVariantMap sidecarAlbum = albumNamed(albums, QStringLiteral("track"));
    QVERIFY(!sidecarAlbum.isEmpty());
    const QString foundSidecar = QUrl(sidecarAlbum.value(QStringLiteral("cover")).toString()).toLocalFile();
    QCOMPARE(QFileInfo(foundSidecar).canonicalFilePath(), QFileInfo(sidecarImage).canonicalFilePath());
}

void VinylTaggedImportTest::partialReimportMergesWithoutChangingIdentity() {
    Tags d1{QStringLiteral("First Disc One Track"), QStringLiteral("Nas"), QStringLiteral("Illmatic XX CD 1"),
            QStringLiteral("Nas"), QStringLiteral("1/2"), QStringLiteral("1/10")};
    Tags d2 = d1; d2.title = QStringLiteral("First Disc Two Track"); d2.album = QStringLiteral("Illmatic XX CD 2");
    d2.disc = QStringLiteral("2/2"); d2.track = QStringLiteral("1/4");
    const QString first = makeFlac(QStringLiteral("partial/disc1.flac"), &d1);
    const QString second = makeFlac(QStringLiteral("partial/disc2.flac"), &d2);
    QVERIFY(!first.isEmpty() && !second.isEmpty());

    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QVERIFY(completed.isValid());

    int requestId = source.startImport({first});
    QVariantMap firstResult;
    waitForImport(completed, requestId, &firstResult);
    QCOMPARE(firstResult.value(QStringLiteral("albums")).toList().size(), 1);
    QVariantMap firstAlbum = firstResult.value(QStringLiteral("albums")).toList().first().toMap();
    const QString albumId = firstAlbum.value(QStringLiteral("id")).toString();
    const QVariantMap firstTrack = firstAlbum.value(QStringLiteral("tracks")).toList().first().toMap();
    const QString firstTrackId = firstTrack.value(QStringLiteral("id")).toString();
    QVERIFY(firstTrackId.startsWith(QStringLiteral("vault:")));
    QCOMPARE(firstAlbum.value(QStringLiteral("title")).toString(), QStringLiteral("Illmatic XX"));
    source.setAlbums({firstAlbum});

    requestId = source.startImport({second});
    QVariantMap secondResult;
    waitForImport(completed, requestId, &secondResult);
    QCOMPARE(secondResult.value(QStringLiteral("errors")).toStringList().size(), 0);
    QCOMPARE(secondResult.value(QStringLiteral("albums")).toList().size(), 1);
    QVariantMap merged = secondResult.value(QStringLiteral("albums")).toList().first().toMap();
    QCOMPARE(merged.value(QStringLiteral("id")).toString(), albumId);
    QVariantList tracks = merged.value(QStringLiteral("tracks")).toList();
    QCOMPARE(tracks.size(), 2);
    QCOMPARE(tracks.first().toMap().value(QStringLiteral("id")).toString(), firstTrackId);
    source.setAlbums({merged});

    requestId = source.startImport({second});
    QVariantMap repeatedResult;
    waitForImport(completed, requestId, &repeatedResult);
    QCOMPARE(repeatedResult.value(QStringLiteral("albums")).toList().size(), 1);
    const QVariantMap repeated = repeatedResult.value(QStringLiteral("albums")).toList().first().toMap();
    QCOMPARE(repeated.value(QStringLiteral("id")).toString(), albumId);
    tracks = repeated.value(QStringLiteral("tracks")).toList();
    QCOMPARE(tracks.size(), 2);
    QCOMPARE(tracks.at(1).toMap().value(QStringLiteral("id")).toString(),
             merged.value(QStringLiteral("tracks")).toList().at(1).toMap().value(QStringLiteral("id")).toString());
}

void VinylTaggedImportTest::taglessAudioRemainsUsable() {
    const QString file = makeFlac(QStringLiteral("loose/Standalone Piece.flac"));
    QVERIFY(!file.isEmpty());
    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QVERIFY(completed.isValid());
    const int requestId = source.startImport({file});
    QVariantMap result;
    waitForImport(completed, requestId, &result);

    QCOMPARE(result.value(QStringLiteral("errors")).toStringList().size(), 0);
    QCOMPARE(result.value(QStringLiteral("albums")).toList().size(), 1);
    const QVariantMap album = result.value(QStringLiteral("albums")).toList().first().toMap();
    QCOMPARE(album.value(QStringLiteral("tracks")).toList().size(), 1);
    const QVariantMap track = album.value(QStringLiteral("tracks")).toList().first().toMap();
    QVERIFY(!track.value(QStringLiteral("title")).toString().trimmed().isEmpty());
    QVERIFY(track.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("vault:")));
    QCOMPARE(QFileInfo(track.value(QStringLiteral("localPath")).toString()).canonicalFilePath(),
             QFileInfo(file).canonicalFilePath());
}

void VinylTaggedImportTest::missingInputReturnsAnError() {
    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QVERIFY(completed.isValid());
    const int requestId = source.startImport({QDir(m_root).filePath(QStringLiteral("missing.flac"))});
    QVariantMap result;
    waitForImport(completed, requestId, &result);
    QVERIFY(result.value(QStringLiteral("albums")).toList().isEmpty());
    QVERIFY(!result.value(QStringLiteral("errors")).toStringList().isEmpty());
}

void VinylTaggedImportTest::cancellationSuppressesCompletion() {
    Tags tags{QStringLiteral("Seed"), QStringLiteral("Nas"), QStringLiteral("Seed Album"), QStringLiteral("Nas"),
              QStringLiteral("1/1"), QStringLiteral("1/1")};
    const QString seed = makeFlac(QStringLiteral("cancel/seed.flac"), &tags);
    QVERIFY(!seed.isEmpty());
    const QString directory = makeLibraryCopies(seed, 80);
    QVERIFY(!directory.isEmpty());

    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QSignalSpy progress(&source, &VinylLocalSource::importProgress);
    QVERIFY(completed.isValid() && progress.isValid());
    const int requestId = source.startImport({directory});
    QCOMPARE(completed.count(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(progress.count() > 0, 10000);
    QVERIFY(progress.first().at(2).toInt() > 1);
    QCOMPARE(progress.first().at(0).toInt(), requestId);
    source.cancelImport();
    QTest::qWait(200);
    QCOMPARE(completed.count(), 0);
}

void VinylTaggedImportTest::profileFenceSuppressesCompletion() {
    Tags tags{QStringLiteral("Seed"), QStringLiteral("Nas"), QStringLiteral("Seed Album"), QStringLiteral("Nas"),
              QStringLiteral("1/1"), QStringLiteral("1/1")};
    const QString seed = makeFlac(QStringLiteral("profile/seed.flac"), &tags);
    QVERIFY(!seed.isEmpty());
    const QString directory = makeLibraryCopies(seed, 80);
    QVERIFY(!directory.isEmpty());

    VaultIdentity identity(m_registry);
    VinylLocalSource source(&identity);
    QSignalSpy completed(&source, &VinylLocalSource::importCompleted);
    QSignalSpy progress(&source, &VinylLocalSource::importProgress);
    QVERIFY(completed.isValid() && progress.isValid());
    source.startImport({directory});
    QTRY_VERIFY_WITH_TIMEOUT(progress.count() > 0, 10000);
    emit source.profileLeaving();
    QTest::qWait(200);
    QCOMPARE(completed.count(), 0);
}

QTEST_GUILESS_MAIN(VinylTaggedImportTest)
#include "tst_vinyl_tagged_import.moc"
