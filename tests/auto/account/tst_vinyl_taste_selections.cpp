#include "account/ProfilePreferencesStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QMetaProperty>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {
constexpr auto kTasteKey = "vinyl/taste";

QString stableArtistKey(const QString &provider, const QString &providerId) {
    const QJsonArray tuple{
        provider,
        QStringLiteral("artist"),
        providerId};
    return QString::fromUtf8(
        QJsonDocument(tuple).toJson(QJsonDocument::Compact));
}

QVariantMap artist(
    const QString &provider,
    const QString &providerId,
    const QString &name,
    const QString &mappingStatus = QStringLiteral("unresolved"),
    const QString &canonicalMbid = QString()) {
    QVariantMap result{
        {QStringLiteral("key"), stableArtistKey(provider, providerId)},
        {QStringLiteral("kind"), QStringLiteral("artist")},
        {QStringLiteral("provider"), provider},
        {QStringLiteral("providerId"), providerId},
        {QStringLiteral("name"), name},
        {QStringLiteral("image"), QString()},
        {QStringLiteral("mappingStatus"), mappingStatus}};
    if (!canonicalMbid.isEmpty())
        result.insert(QStringLiteral("canonicalMbid"), canonicalMbid);
    return result;
}

bool invokeTypedSetter(
    QObject *store,
    bool answered,
    const QVariantList &artists,
    bool *committed) {
    bool result = false;
    const bool invoked = QMetaObject::invokeMethod(
        store,
        "setVinylTasteSelections",
        Qt::DirectConnection,
        Q_RETURN_ARG(bool, result),
        Q_ARG(bool, answered),
        Q_ARG(QVariantList, artists));
    if (committed)
        *committed = result;
    return invoked;
}

bool invokeLegacySetter(
    QObject *store,
    bool answered,
    const QStringList &artistIds,
    bool *committed) {
    bool result = false;
    const bool invoked = QMetaObject::invokeMethod(
        store,
        "setVinylTaste",
        Qt::DirectConnection,
        Q_RETURN_ARG(bool, result),
        Q_ARG(bool, answered),
        Q_ARG(QStringList, artistIds));
    if (committed)
        *committed = result;
    return invoked;
}

bool answeredValue(const QObject *store) {
    return store->property("vinylTasteAnswered").toBool();
}

QStringList artistIdsValue(const QObject *store) {
    return store->property("vinylTasteArtistIds").toStringList();
}

QVariantList artistsValue(const QObject *store) {
    return store->property("vinylTasteArtists").toList();
}

QString persistenceError(const QObject *store) {
    return store->property("vinylPersistenceError").toString();
}
}

class tst_vinyl_taste_selections : public QObject {
    Q_OBJECT

private slots:
    void emptyDoneAndSkipPersistAcrossReload();
    void typedProviderIdentityRoundTripsThroughAtomicPayload();
    void legacyIdsMigrateWithoutRewritingAndRemainExact();
    void legacyWhitespaceAndUtf8IdsSurviveMigrationAndEdit();
    void oversizedLegacyPayloadLoadsButFailedEditPreservesIt();
    void legacySetterRemainsCompatible();
    void duplicateAndMalformedSelectionsAreRejected();
    void mappingStatusRequiresConsistentCanonicalIdentity();
    void unansweredSelectionsAreRejected();
    void failedSyncRetainsPreviousTasteValues();
};

void tst_vinyl_taste_selections::emptyDoneAndSkipPersistAcrossReload() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("preferences.ini"));
    bool committed = false;

    {
        ProfilePreferencesStore store(path);
        const int propertyIndex = store.metaObject()->indexOfProperty("vinylTasteArtists");
        QVERIFY2(propertyIndex >= 0, "typed taste artists must be exposed as a Qt property");
        const QMetaProperty property = store.metaObject()->property(propertyIndex);
        QVERIFY(property.isReadable());
        QVERIFY(property.hasNotifySignal());
        QVERIFY(store.metaObject()->indexOfMethod(
                    "setVinylTasteSelections(bool,QVariantList)") >= 0);

        QVERIFY(invokeTypedSetter(&store, true, {}, &committed));
        QVERIFY(committed);
        QVERIFY(answeredValue(&store));
        QVERIFY(artistIdsValue(&store).isEmpty());
        QVERIFY(artistsValue(&store).isEmpty());
    }
    {
        ProfilePreferencesStore reloaded(path);
        QVERIFY(answeredValue(&reloaded));
        QVERIFY(artistIdsValue(&reloaded).isEmpty());
        QVERIFY(artistsValue(&reloaded).isEmpty());

        QVERIFY(invokeTypedSetter(&reloaded, false, {}, &committed));
        QVERIFY(committed);
        QVERIFY(!answeredValue(&reloaded));
    }
    {
        ProfilePreferencesStore skipped(path);
        QVERIFY(!answeredValue(&skipped));
        QVERIFY(artistIdsValue(&skipped).isEmpty());
        QVERIFY(artistsValue(&skipped).isEmpty());
    }
}

void tst_vinyl_taste_selections::typedProviderIdentityRoundTripsThroughAtomicPayload() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("preferences.ini"));

    const QString youtubeId = QStringLiteral("UC-music-channel");
    const QString localId = QStringLiteral("local-artist-7");
    const QString mbid = QStringLiteral("2f9ecbed-27be-40e6-abca-6de49d50299e");
    const QVariantList selections{
        artist(QStringLiteral("youtube-music"), youtubeId, QStringLiteral("Example Band")),
        artist(QStringLiteral("local"), localId, QStringLiteral("Saved Local Artist")),
        artist(QStringLiteral("musicbrainz"), mbid, QStringLiteral("Verified Artist"),
               QStringLiteral("verified"), mbid)};
    const QStringList legacyIds{
        QStringLiteral("ytm:artist:") + youtubeId,
        localId,
        mbid};

    {
        ProfilePreferencesStore store(path);
        bool committed = false;
        QVERIFY(invokeTypedSetter(&store, true, selections, &committed));
        QVERIFY(committed);
        QVERIFY(answeredValue(&store));
        QCOMPARE(artistIdsValue(&store), legacyIds);
        QCOMPARE(artistsValue(&store), selections);

        QSettings settings(path, QSettings::IniFormat);
        const QVariantMap payload = settings.value(QString::fromLatin1(kTasteKey)).toMap();
        QCOMPARE(payload.value(QStringLiteral("answered")).toBool(), true);
        QCOMPARE(payload.value(QStringLiteral("artistIds")).toStringList(), legacyIds);
        QCOMPARE(payload.value(QStringLiteral("artists")).toList(), selections);
        QVERIFY(payload.value(QStringLiteral("version")).toInt() > 0);
    }
    {
        ProfilePreferencesStore reloaded(path);
        QVERIFY(answeredValue(&reloaded));
        QCOMPARE(artistIdsValue(&reloaded), legacyIds);
        QCOMPARE(artistsValue(&reloaded), selections);
        QVERIFY(persistenceError(&reloaded).isEmpty());
    }
}

void tst_vinyl_taste_selections::legacyIdsMigrateWithoutRewritingAndRemainExact() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("legacy.ini"));
    const QStringList legacyIds{
        QStringLiteral("ytm:artist:UC-old-channel"),
        QStringLiteral("Unresolved legacy artist")};
    const QVariantMap oldPayload{
        {QStringLiteral("answered"), true},
        {QStringLiteral("artistIds"), legacyIds}};
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(QString::fromLatin1(kTasteKey), oldPayload);
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }

    QFile beforeFile(path);
    QVERIFY(beforeFile.open(QIODevice::ReadOnly));
    const QByteArray before = beforeFile.readAll();
    beforeFile.close();

    {
        ProfilePreferencesStore migrated(path);
        QVERIFY(answeredValue(&migrated));
        QCOMPARE(artistIdsValue(&migrated), legacyIds);
        const QVariantList artists = artistsValue(&migrated);
        QCOMPARE(artists.size(), 2);
        const QVariantMap youtube = artists.at(0).toMap();
        QCOMPARE(youtube.value(QStringLiteral("provider")).toString(),
                 QStringLiteral("youtube-music"));
        QCOMPARE(youtube.value(QStringLiteral("providerId")).toString(),
                 QStringLiteral("UC-old-channel"));
        QCOMPARE(youtube.value(QStringLiteral("key")).toString(),
                 stableArtistKey(QStringLiteral("youtube-music"), QStringLiteral("UC-old-channel")));
        const QVariantMap local = artists.at(1).toMap();
        QCOMPARE(local.value(QStringLiteral("provider")).toString(), QStringLiteral("local"));
        QCOMPARE(local.value(QStringLiteral("providerId")).toString(), legacyIds.at(1));
        QCOMPARE(local.value(QStringLiteral("key")).toString(),
                 stableArtistKey(QStringLiteral("local"), legacyIds.at(1)));
        for (const QVariant &entry : artists)
            QVERIFY(!entry.toMap().contains(QStringLiteral("canonicalMbid")));
    }

    QFile afterFile(path);
    QVERIFY(afterFile.open(QIODevice::ReadOnly));
    QCOMPARE(afterFile.readAll(), before);

    ProfilePreferencesStore reloaded(path);
    QVERIFY(answeredValue(&reloaded));
    QCOMPARE(artistIdsValue(&reloaded), legacyIds);
    QCOMPARE(artistsValue(&reloaded).size(), 2);
}

void tst_vinyl_taste_selections::legacyWhitespaceAndUtf8IdsSurviveMigrationAndEdit() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("legacy-unresolved.ini"));
    const QString whitespaceYtmId = QStringLiteral("ytm:artist:   ");
    const QString longUtf8Id(1400, QChar(0x20ac));
    const QString nullUuid = QStringLiteral("00000000-0000-0000-0000-000000000000");
    QVERIFY(longUtf8Id.size() <= 4096);
    QVERIFY(longUtf8Id.toUtf8().size() > 4096);
    const QStringList legacyIds{whitespaceYtmId, longUtf8Id, nullUuid};
    const QVariantMap oldPayload{
        {QStringLiteral("answered"), true},
        {QStringLiteral("artistIds"), legacyIds}};
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(QString::fromLatin1(kTasteKey), oldPayload);
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }

    QFile beforeFile(path);
    QVERIFY(beforeFile.open(QIODevice::ReadOnly));
    const QByteArray before = beforeFile.readAll();
    beforeFile.close();

    {
        ProfilePreferencesStore migrated(path);
        QVERIFY(answeredValue(&migrated));
        QCOMPARE(artistIdsValue(&migrated), legacyIds);
        const QVariantList artists = artistsValue(&migrated);
        QCOMPARE(artists.size(), 3);

        const QVariantMap youtube = artists.at(0).toMap();
        QCOMPARE(youtube.value(QStringLiteral("provider")).toString(),
                 QStringLiteral("youtube-music"));
        QCOMPARE(youtube.value(QStringLiteral("providerId")).toString(), QStringLiteral("   "));
        QCOMPARE(youtube.value(QStringLiteral("legacyId")).toString(), whitespaceYtmId);
        QCOMPARE(youtube.value(QStringLiteral("mappingStatus")).toString(), QStringLiteral("unresolved"));
        QVERIFY(!youtube.contains(QStringLiteral("canonicalMbid")));

        const QVariantMap unicode = artists.at(1).toMap();
        QCOMPARE(unicode.value(QStringLiteral("provider")).toString(), QStringLiteral("local"));
        QCOMPARE(unicode.value(QStringLiteral("providerId")).toString(), longUtf8Id);
        QCOMPARE(unicode.value(QStringLiteral("legacyId")).toString(), longUtf8Id);
        QVERIFY(!unicode.contains(QStringLiteral("canonicalMbid")));

        const QVariantMap nullUuidArtist = artists.at(2).toMap();
        QCOMPARE(nullUuidArtist.value(QStringLiteral("provider")).toString(), QStringLiteral("local"));
        QCOMPARE(nullUuidArtist.value(QStringLiteral("providerId")).toString(), nullUuid);
        QVERIFY(!nullUuidArtist.contains(QStringLiteral("canonicalMbid")));
    }

    QFile afterLoadFile(path);
    QVERIFY(afterLoadFile.open(QIODevice::ReadOnly));
    QCOMPARE(afterLoadFile.readAll(), before);
    afterLoadFile.close();

    {
        ProfilePreferencesStore edited(path);
        bool committed = false;
        QVERIFY(invokeLegacySetter(&edited, true, legacyIds, &committed));
        QVERIFY2(committed, qPrintable(persistenceError(&edited)));
        QCOMPARE(artistIdsValue(&edited), legacyIds);

        const QVariantList retainedLegacyArtists = artistsValue(&edited);
        QVERIFY(invokeTypedSetter(&edited, true, retainedLegacyArtists, &committed));
        QVERIFY(committed);
        QCOMPARE(artistIdsValue(&edited), legacyIds);
        QCOMPARE(artistsValue(&edited).size(), 3);

        QVariantList withNativeArtist = retainedLegacyArtists;
        withNativeArtist.append(artist(
            QStringLiteral("local"), QStringLiteral("native-added"),
            QStringLiteral("Native Onboarding Artist")));
        QStringList idsWithNativeArtist = legacyIds;
        idsWithNativeArtist.append(QStringLiteral("native-added"));
        QVERIFY(invokeTypedSetter(&edited, true, withNativeArtist, &committed));
        QVERIFY(committed);
        QCOMPARE(artistIdsValue(&edited), idsWithNativeArtist);
        QCOMPARE(artistsValue(&edited).size(), 4);

        QVERIFY(invokeTypedSetter(&edited, true, retainedLegacyArtists, &committed));
        QVERIFY(committed);
        QCOMPARE(artistIdsValue(&edited), legacyIds);
        QCOMPARE(artistsValue(&edited).size(), 3);
    }

    ProfilePreferencesStore reloaded(path);
    QVERIFY(answeredValue(&reloaded));
    QCOMPARE(artistIdsValue(&reloaded), legacyIds);
    QCOMPARE(artistsValue(&reloaded).at(0).toMap().value(QStringLiteral("providerId")).toString(),
             QStringLiteral("   "));
    QCOMPARE(artistsValue(&reloaded).at(1).toMap().value(QStringLiteral("providerId")).toString(),
             longUtf8Id);
    QVERIFY(!artistsValue(&reloaded).at(2).toMap().contains(QStringLiteral("canonicalMbid")));
}

void tst_vinyl_taste_selections::oversizedLegacyPayloadLoadsButFailedEditPreservesIt() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("legacy-oversized.ini"));
    const QString unicodeSuffix(4090, QChar(0x20ac));
    QStringList legacyIds;
    for (int index = 0; index < 350; ++index)
        legacyIds.append(QString::number(index) + unicodeSuffix);
    const QVariantMap oldPayload{
        {QStringLiteral("answered"), true},
        {QStringLiteral("artistIds"), legacyIds}};
    QVERIFY(QJsonDocument::fromVariant(oldPayload).toJson(QJsonDocument::Compact).size()
            > 4 * 1024 * 1024);
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(QString::fromLatin1(kTasteKey), oldPayload);
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }

    QFile beforeFile(path);
    QVERIFY(beforeFile.open(QIODevice::ReadOnly));
    const QByteArray before = beforeFile.readAll();
    beforeFile.close();

    ProfilePreferencesStore store(path);
    QVERIFY(answeredValue(&store));
    QCOMPARE(artistIdsValue(&store), legacyIds);
    QCOMPARE(artistsValue(&store).size(), legacyIds.size());

    bool committed = true;
    QVERIFY(invokeLegacySetter(&store, true, legacyIds, &committed));
    QVERIFY(!committed);
    QVERIFY(answeredValue(&store));
    QCOMPARE(artistIdsValue(&store), legacyIds);
    QVERIFY(!persistenceError(&store).isEmpty());

    QFile afterFile(path);
    QVERIFY(afterFile.open(QIODevice::ReadOnly));
    QCOMPARE(afterFile.readAll(), before);
}

void tst_vinyl_taste_selections::legacySetterRemainsCompatible() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("preferences.ini"));
    const QStringList ids{
        QStringLiteral("ytm:artist:UC-legacy"),
        QStringLiteral("Local legacy name")};

    ProfilePreferencesStore store(path);
    bool committed = false;
    QVERIFY(invokeLegacySetter(&store, true, ids, &committed));
    QVERIFY(committed);
    QVERIFY(answeredValue(&store));
    QCOMPARE(artistIdsValue(&store), ids);
    QCOMPARE(artistsValue(&store).size(), 2);

    QSettings settings(path, QSettings::IniFormat);
    const QVariantMap payload = settings.value(QString::fromLatin1(kTasteKey)).toMap();
    QCOMPARE(payload.value(QStringLiteral("artistIds")).toStringList(), ids);
    QCOMPARE(payload.value(QStringLiteral("artists")).toList().size(), 2);
    QVERIFY(payload.contains(QStringLiteral("version")));
}

void tst_vinyl_taste_selections::duplicateAndMalformedSelectionsAreRejected() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ProfilePreferencesStore store(temporary.filePath(QStringLiteral("preferences.ini")));
    const QVariantMap good = artist(
        QStringLiteral("local"), QStringLiteral("artist-1"), QStringLiteral("Artist One"));
    bool committed = false;

    QVERIFY(invokeTypedSetter(&store, true, {good}, &committed));
    QVERIFY(committed);
    const QStringList previousIds = artistIdsValue(&store);
    const QVariantList previousArtists = artistsValue(&store);

    const QVariantList duplicate{good, good};
    QVERIFY(invokeTypedSetter(&store, true, duplicate, &committed));
    QVERIFY(!committed);
    QCOMPARE(artistIdsValue(&store), previousIds);
    QCOMPARE(artistsValue(&store), previousArtists);
    QVERIFY(!persistenceError(&store).isEmpty());

    QVariantMap malformed = good;
    malformed.insert(QStringLiteral("key"), QStringLiteral("wrong-key"));
    QVERIFY(invokeTypedSetter(&store, true, {malformed}, &committed));
    QVERIFY(!committed);
    QCOMPARE(artistIdsValue(&store), previousIds);
    QCOMPARE(artistsValue(&store), previousArtists);

    malformed = good;
    malformed.insert(QStringLiteral("provider"), QStringLiteral("unknown-provider"));
    QVERIFY(invokeTypedSetter(&store, true, {malformed}, &committed));
    QVERIFY(!committed);
    QCOMPARE(artistIdsValue(&store), previousIds);
    QCOMPARE(artistsValue(&store), previousArtists);
}

void tst_vinyl_taste_selections::mappingStatusRequiresConsistentCanonicalIdentity() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("mapping-status.ini"));
    ProfilePreferencesStore store(path);
    bool committed = false;

    const QVariantMap unknown = artist(
        QStringLiteral("local"), QStringLiteral("artist-status"), QStringLiteral("Status Artist"),
        QStringLiteral("matched"));
    QVERIFY(invokeTypedSetter(&store, true, {unknown}, &committed));
    QVERIFY(!committed);
    QVERIFY(artistsValue(&store).isEmpty());

    const QVariantMap verifiedWithoutMbid = artist(
        QStringLiteral("local"), QStringLiteral("artist-status"), QStringLiteral("Status Artist"),
        QStringLiteral("verified"));
    QVERIFY(invokeTypedSetter(&store, true, {verifiedWithoutMbid}, &committed));
    QVERIFY(!committed);
    QVERIFY(artistsValue(&store).isEmpty());

    const QString mbid = QStringLiteral("2f9ecbed-27be-40e6-abca-6de49d50299e");
    const QVariantMap unresolvedWithMbid = artist(
        QStringLiteral("local"), QStringLiteral("artist-status"), QStringLiteral("Status Artist"),
        QStringLiteral("unresolved"), mbid);
    QVERIFY(invokeTypedSetter(&store, true, {unresolvedWithMbid}, &committed));
    QVERIFY(!committed);
    QVERIFY(artistsValue(&store).isEmpty());

    const QString nullUuid = QStringLiteral("00000000-0000-0000-0000-000000000000");
    const QVariantMap nullMusicBrainzIdentity = artist(
        QStringLiteral("musicbrainz"), nullUuid, QStringLiteral("Unverified Null UUID"),
        QStringLiteral("verified"), nullUuid);
    QVERIFY(invokeTypedSetter(&store, true, {nullMusicBrainzIdentity}, &committed));
    QVERIFY(!committed);
    QVERIFY(artistsValue(&store).isEmpty());

    for (const QString &status : {QStringLiteral("ambiguous"), QStringLiteral("unavailable")}) {
        const QVariantMap selection = artist(
            QStringLiteral("local"), QStringLiteral("artist-status"), QStringLiteral("Status Artist"),
            status);
        QVERIFY(invokeTypedSetter(&store, true, {selection}, &committed));
        QVERIFY(committed);
        QCOMPARE(artistsValue(&store).first().toMap().value(QStringLiteral("mappingStatus")).toString(),
                 status);
    }

    const QVariantMap verified = artist(
        QStringLiteral("local"), QStringLiteral("artist-status"), QStringLiteral("Status Artist"),
        QStringLiteral("verified"), mbid);
    QVERIFY(invokeTypedSetter(&store, true, {verified}, &committed));
    QVERIFY(committed);
    QCOMPARE(artistsValue(&store).first().toMap().value(QStringLiteral("canonicalMbid")).toString(), mbid);

    ProfilePreferencesStore reloaded(path);
    QCOMPARE(artistsValue(&reloaded), QVariantList{verified});
}

void tst_vinyl_taste_selections::unansweredSelectionsAreRejected() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ProfilePreferencesStore store(temporary.filePath(QStringLiteral("preferences.ini")));
    const QVariantList selections{
        artist(QStringLiteral("local"), QStringLiteral("artist-2"), QStringLiteral("Artist Two"))};
    bool committed = false;

    QVERIFY(invokeTypedSetter(&store, true, selections, &committed));
    QVERIFY(committed);
    const QStringList previousIds = artistIdsValue(&store);
    const QVariantList previousArtists = artistsValue(&store);

    QVERIFY(invokeTypedSetter(&store, false, selections, &committed));
    QVERIFY(!committed);
    QVERIFY(answeredValue(&store));
    QCOMPARE(artistIdsValue(&store), previousIds);
    QCOMPARE(artistsValue(&store), previousArtists);
    QVERIFY(!persistenceError(&store).isEmpty());
}

void tst_vinyl_taste_selections::failedSyncRetainsPreviousTasteValues() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = temporary.filePath(QStringLiteral("live"));
    const QString backup = directory + QStringLiteral("-backup");
    QVERIFY(QDir().mkdir(directory));
    const QString path = directory + QStringLiteral("/preferences.ini");

    {
        ProfilePreferencesStore store(path);
        const QVariantList original{
            artist(QStringLiteral("local"), QStringLiteral("original"), QStringLiteral("Original"))};
        bool committed = false;
        QVERIFY(invokeTypedSetter(&store, true, original, &committed));
        QVERIFY(committed);
        const QStringList previousIds = artistIdsValue(&store);
        const QVariantList previousArtists = artistsValue(&store);
        QVERIFY(QDir().rename(directory, backup));

        {
            QFile blocker(directory);
            QVERIFY(blocker.open(QIODevice::WriteOnly));
            const QVariantList replacement{
                artist(QStringLiteral("local"), QStringLiteral("replacement"), QStringLiteral("Replacement"))};
            QVERIFY(invokeTypedSetter(&store, true, replacement, &committed));
            QVERIFY(!committed);
            QVERIFY(answeredValue(&store));
            QCOMPARE(artistIdsValue(&store), previousIds);
            QCOMPARE(artistsValue(&store), previousArtists);
            QVERIFY(!persistenceError(&store).isEmpty());
        }

        QVERIFY(QFile::remove(directory));
        QVERIFY(QDir().rename(backup, directory));
    }

    ProfilePreferencesStore reloaded(path);
    QVERIFY(answeredValue(&reloaded));
    QCOMPARE(artistIdsValue(&reloaded), QStringList{QStringLiteral("original")});
    QCOMPARE(artistsValue(&reloaded).size(), 1);
    QCOMPARE(artistsValue(&reloaded).first().toMap().value(QStringLiteral("providerId")).toString(),
             QStringLiteral("original"));
}

QTEST_GUILESS_MAIN(tst_vinyl_taste_selections)
#include "tst_vinyl_taste_selections.moc"
