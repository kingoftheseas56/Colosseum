#include "../../native/CollectionStore.h"
#include "../../native/devtools/CollectionsTestProfileController.h"
#include "../../native/engine/VaultIdentity.h"
#include "../../native/vinyl/VinylLocalSource.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <memory>

namespace {
int fail(const QString &message)
{
    QTextStream(stderr) << "vinyl_seed: " << message << '\n';
    return 2;
}

QString absoluteClean(const QString &path)
{
    const QString native = QDir::fromNativeSeparators(path);
    if (!QFileInfo(native).isAbsolute() || QDir::cleanPath(native) != native) return {};
    return native;
}

bool readBounded(const QString &path, qint64 limit, QByteArray *bytes)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() > limit) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    *bytes = file.read(limit + 1);
    return bytes->size() <= limit && file.error() == QFileDevice::NoError;
}

bool emptyLibrary(const QString &path, QByteArray *original)
{
    if (!readBounded(path, 65536, original)) return false;
    QSettings settings(path, QSettings::IniFormat);
    const QStringList keys = settings.allKeys();
    for (const QString &key : keys)
        if (key != QLatin1String("collection/entries")) return false;
    if (settings.status() != QSettings::NoError) return false;
    const QByteArray records = settings.value(QStringLiteral("collection/entries")).toByteArray();
    if (records.isEmpty()) return true;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(records, &parseError);
    return parseError.error == QJsonParseError::NoError && doc.isObject() && doc.object().isEmpty();
}

bool noKnownIdentity(const QString &vault)
{
    for (const QString &suffix : {QString(), QStringLiteral(".bak"), QStringLiteral(".tmp")}) {
        const QFileInfo file(QDir(vault).filePath(QStringLiteral("identity.json") + suffix));
        if (file.exists() || file.isSymLink()) return false;
    }
    return true;
}

// This CLI deliberately accepts only a small existing WAV catalogue. The real
// importer validates its complete manifest schema and owns every identity.
bool boundedManifest(const QString &root, const QString &path, QString *error)
{
    const auto bad = [&](const QString &reason) { *error = reason; return false; };
    if (!path.startsWith(root + QLatin1Char('/')) || !path.endsWith(QLatin1String(".vinyl.json")))
        return bad(QStringLiteral("Manifest must be a .vinyl.json file inside the marked fixture root."));
    QByteArray bytes;
    if (!readBounded(path, 256 * 1024, &bytes))
        return bad(QStringLiteral("Manifest must be an existing readable file of at most 256 KiB."));
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return bad(QStringLiteral("Manifest is not a JSON object."));
    const QJsonObject object = doc.object();
    const QJsonArray releases = object.contains(QStringLiteral("releases"))
        ? object.value(QStringLiteral("releases")).toArray() : QJsonArray{object};
    if (releases.isEmpty() || releases.size() > 4)
        return bad(QStringLiteral("Fixture must contain 1–4 releases."));
    const QString directory = QFileInfo(path).canonicalPath();
    QSet<QString> files;
    int tracks = 0;
    qint64 totalBytes = 0;
    for (const QJsonValue &release : releases) {
        const QJsonArray editions = release.toObject().value(QStringLiteral("editions")).toArray();
        if (editions.isEmpty() || editions.size() > 4)
            return bad(QStringLiteral("Each fixture release must contain 1–4 editions."));
        for (const QJsonValue &edition : editions) {
            const QJsonArray rows = edition.toObject().value(QStringLiteral("tracks")).toArray();
            if (rows.isEmpty()) return bad(QStringLiteral("Fixture editions must contain tracks."));
            for (const QJsonValue &row : rows) {
                if (++tracks > 32) return bad(QStringLiteral("Fixture exceeds 32 track references."));
                const QString relative = row.toObject().value(QStringLiteral("file")).toString();
                const QFileInfo audio(QDir(directory).filePath(relative));
                const QString canonical = audio.canonicalFilePath();
                if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(QLatin1Char('\\'))
                    || relative.split(QLatin1Char('/')).contains(QStringLiteral(".."))
                    || !audio.isFile() || audio.isSymLink() || audio.suffix().compare(QLatin1String("wav"), Qt::CaseInsensitive)
                    || !canonical.startsWith(directory + QLatin1Char('/'))
                    || audio.size() < 44 || audio.size() > 4 * 1024 * 1024)
                    return bad(QStringLiteral("Fixture audio must be existing WAV files beneath the manifest, each at most 4 MiB."));
                QFile wav(canonical);
                if (!wav.open(QIODevice::ReadOnly)) return bad(QStringLiteral("Fixture WAV is unreadable."));
                const QByteArray header = wav.read(12);
                if (!header.startsWith("RIFF") || header.mid(8, 4) != "WAVE")
                    return bad(QStringLiteral("Fixture audio does not have a RIFF/WAVE header."));
                if (!files.contains(canonical)) { files.insert(canonical); totalBytes += audio.size(); }
                if (totalBytes > 16 * 1024 * 1024)
                    return bad(QStringLiteral("Fixture audio exceeds 16 MiB in total."));
            }
        }
    }
    return true;
}

QVariantMap candidate(const QString &kind, const QString &id, const QString &edition,
    const QString &title, const QString &cover, const QString &creator,
    const QString &year, const QVariantMap &payload)
{
    return {{QStringLiteral("world"), QStringLiteral("vinyl")}, {QStringLiteral("kind"), kind},
        {QStringLiteral("source"), QStringLiteral("vinyl-local")}, {QStringLiteral("sourceId"), id},
        {QStringLiteral("editionId"), edition}, {QStringLiteral("title"), title},
        {QStringLiteral("cover"), cover}, {QStringLiteral("creator"), creator}, {QStringLiteral("year"), year},
        {QStringLiteral("route"), QStringLiteral("vinyl-") + kind + QStringLiteral("-detail")},
        {QStringLiteral("payload"), payload}};
}

bool verifyReadOnlyRoute(VinylLocalSource &source, const QString &albumId,
    const QString &trackId, const QVariantMap &expectedAlbum, QString *error)
{
    if (source.request(QStringLiteral("named-collections"), QStringLiteral("resolve"), trackId) >= 0) {
        source.cancel(QStringLiteral("named-collections"));
        *error = QStringLiteral("The named Collections lane unexpectedly admitted a playback-resolution operation.");
        return false;
    }
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool answered = false;
    bool correct = false;
    int expected = -1;
    const auto connection = QObject::connect(&source, &VinylLocalSource::completed, &loop,
        [&](const QString &lane, int request, const QVariant &data, const QString &failure) {
            if (lane != QLatin1String("named-collections") || request != expected) return;
            answered = true;
            correct = failure.isEmpty() && data.toMap().value(QStringLiteral("id")).toString() == albumId
                && QJsonObject::fromVariantMap(data.toMap()) == QJsonObject::fromVariantMap(expectedAlbum);
            if (!correct) *error = failure.isEmpty() ? QStringLiteral("Local album route returned a different JSON snapshot.") : failure;
            loop.quit();
        });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    expected = source.request(QStringLiteral("named-collections"), QStringLiteral("album"), albumId);
    if (expected >= 0) { deadline.start(2000); loop.exec(); }
    QObject::disconnect(connection);
    if (!answered) *error = expected < 0 ? QStringLiteral("Real local owner rejected the named-collections album lane.")
        : QStringLiteral("Real local owner did not answer the album route within two seconds.");
    return answered && correct;
}

// JSON is CollectionStore's persistence contract. QStringList becomes
// QVariantList after its QJsonObject -> JSON -> toVariantMap round trip;
// comparing nested QVariant metatypes would incorrectly reject that round trip.
bool sameSavedEntry(QVariantMap saved, const QVariantMap &expected)
{
    if (saved.take(QStringLiteral("world")).toString() != QLatin1String("Vinyl")
        || saved.take(QStringLiteral("addedAt")).toLongLong() <= 0) return false;
    return QJsonObject::fromVariantMap(saved) == QJsonObject::fromVariantMap(expected);
}

// A read-only test oracle for the exact importer projection. It checks every
// persisted album field against the still-present bounded manifest and uses
// VaultIdentity's PURE computeId/knows APIs for current file facts. It never
// calls importFiles, observeFile(s), idForFile, CollectionStore::add or save.
bool verifyExistingCatalogue(const QString &manifest, const QVariantList &saved,
    VaultIdentity &identity, QVariantList *albums, QString *error)
{
    const auto bad = [&](const QString &message) { *error = message; return false; };
    QByteArray bytes;
    if (!readBounded(manifest, 256 * 1024, &bytes)) return bad(QStringLiteral("Existing catalogue is unreadable."));
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    const auto releases = root.contains(QStringLiteral("releases")) ? root.value(QStringLiteral("releases")).toArray() : QJsonArray{root};
    const QFileInfo manifestFile(manifest);
    const QString manifestPath = manifestFile.canonicalFilePath();
    const QString manifestId = VaultIdentity::computeId(manifestPath, manifestFile.size(), manifestFile.lastModified().toMSecsSinceEpoch());
    if (root.value(QStringLiteral("version")).toInt(-1) != 1 || !identity.knows(manifestId)
        || !identity.pendingCeremonies().isEmpty() || saved.size() != releases.size())
        return bad(QStringLiteral("Existing catalogue identity, decisions, or album count do not match the fixture."));
    const QJsonObject artistInput = root.value(QStringLiteral("artist")).toObject();
    const QString artistName = artistInput.value(QStringLiteral("name")).toString().trimmed();
    const QString artistId = QStringLiteral("vinyl:local-artist:") + manifestId + QLatin1Char(':')
        + artistInput.value(QStringLiteral("key")).toString(QStringLiteral("artist"));
    QVariantMap artist{{QStringLiteral("name"), artistName}, {QStringLiteral("id"), artistId}};
    if (artistInput.contains(QStringLiteral("biography"))) artist.insert(QStringLiteral("biography"), artistInput.value(QStringLiteral("biography")).toVariant());
    const auto localPath = [&](const QJsonValue &value) { return QFileInfo(QDir(manifestFile.canonicalPath()).filePath(value.toString())).canonicalFilePath(); };
    if (artistInput.contains(QStringLiteral("backdrops"))) {
        QStringList urls;
        for (const auto &image : artistInput.value(QStringLiteral("backdrops")).toArray()) urls.append(QUrl::fromLocalFile(localPath(image)).toString());
        artist.insert(QStringLiteral("backdrops"), urls);
    }
    const auto copyDetails = [](const QJsonObject &from, QVariantMap &to) {
        for (const QString &name : {QStringLiteral("lyrics"), QStringLiteral("notes"), QStringLiteral("credits"),
                 QStringLiteral("about"), QStringLiteral("genre"), QStringLiteral("country"), QStringLiteral("language"), QStringLiteral("year")})
            if (from.contains(name)) to.insert(name, from.value(name).toVariant());
    };
    QSet<QString> matched;
    for (const auto &releaseValue : releases) {
        const QJsonObject release = releaseValue.toObject();
        const QString releaseKey = release.value(QStringLiteral("key")).toString(root.contains(QStringLiteral("releases")) ? QString() : QStringLiteral("release"));
        const QString albumId = QStringLiteral("vinyl:local-release:") + manifestId + QLatin1Char(':') + releaseKey;
        if (releaseKey.isEmpty() || matched.contains(albumId)) return bad(QStringLiteral("Catalogue release identity is not unique."));
        matched.insert(albumId);
        QVariantMap actual;
        for (const auto &row : saved)
            if (row.toMap().value(QStringLiteral("id")).toString() == albumId)
                actual = row.toMap().value(QStringLiteral("payload")).toMap().value(QStringLiteral("album")).toMap();
        if (actual.isEmpty()) return bad(QStringLiteral("An owner-generated catalogue release is missing from Library."));
        QVariantMap expected{{QStringLiteral("id"), albumId}, {QStringLiteral("browseId"), albumId},
            {QStringLiteral("title"), release.value(QStringLiteral("title")).toString().trimmed()},
            {QStringLiteral("artists"), QStringList{artistName}}, {QStringLiteral("artistIds"), QStringList{artistId}},
            {QStringLiteral("artist"), artist}, {QStringLiteral("origin"), QStringLiteral("local")},
            {QStringLiteral("kind"), release.value(QStringLiteral("kind")).toString(QStringLiteral("album"))},
            {QStringLiteral("externalIds"), QVariantMap{}}, {QStringLiteral("manifestPath"), manifestPath}, {QStringLiteral("manifestId"), manifestId}};
        copyDetails(release, expected);
        if (release.contains(QStringLiteral("cover"))) expected.insert(QStringLiteral("cover"), QUrl::fromLocalFile(localPath(release.value(QStringLiteral("cover")))).toString());
        QVariantList editions;
        for (const auto &editionValue : release.value(QStringLiteral("editions")).toArray()) {
            const QJsonObject edition = editionValue.toObject();
            const QString editionKey = edition.value(QStringLiteral("key")).toString(QStringLiteral("standard"));
            QVariantList tracks;
            for (const auto &trackValue : edition.value(QStringLiteral("tracks")).toArray()) {
                const QJsonObject track = trackValue.toObject();
                const QString path = localPath(track.value(QStringLiteral("file")));
                const QFileInfo facts(path);
                const QString id = VaultIdentity::computeId(path, facts.size(), facts.lastModified().toMSecsSinceEpoch());
                if (!facts.isFile() || !identity.knows(id)) return bad(QStringLiteral("A fixture track's current file facts have no admitted owner identity."));
                QVariantMap row{{QStringLiteral("id"), id}, {QStringLiteral("localPath"), path},
                    {QStringLiteral("title"), track.value(QStringLiteral("title")).toString().trimmed()},
                    {QStringLiteral("artists"), QStringList{artistName}}, {QStringLiteral("seconds"), track.value(QStringLiteral("seconds")).toDouble(0)},
                    {QStringLiteral("disc"), track.value(QStringLiteral("disc")).toInt(1)},
                    {QStringLiteral("number"), track.value(QStringLiteral("number")).toInt(int(tracks.size()) + 1)},
                    {QStringLiteral("available"), true}, {QStringLiteral("externalIds"), QVariantMap{}}};
                copyDetails(track, row);
                tracks.append(row);
            }
            std::sort(tracks.begin(), tracks.end(), [](const QVariant &a, const QVariant &b) {
                const auto x = a.toMap(), y = b.toMap();
                return x.value(QStringLiteral("disc")).toInt() == y.value(QStringLiteral("disc")).toInt()
                    ? x.value(QStringLiteral("number")).toInt() < y.value(QStringLiteral("number")).toInt()
                    : x.value(QStringLiteral("disc")).toInt() < y.value(QStringLiteral("disc")).toInt();
            });
            QVariantList discs, discTracks;
            int discNumber = -1;
            const auto appendDisc = [&] { discs.append(QVariantMap{{QStringLiteral("number"), discNumber},
                {QStringLiteral("title"), QStringLiteral("Disc %1").arg(discNumber)}, {QStringLiteral("tracks"), discTracks}}); };
            for (const auto &track : tracks) {
                const int number = track.toMap().value(QStringLiteral("disc")).toInt();
                if (number != discNumber && !discTracks.isEmpty()) { appendDisc(); discTracks.clear(); }
                discNumber = number;
                discTracks.append(track);
            }
            if (!discTracks.isEmpty()) appendDisc();
            editions.append(QVariantMap{{QStringLiteral("id"), albumId + QLatin1Char(':') + editionKey},
                {QStringLiteral("title"), edition.value(QStringLiteral("title")).toString(editionKey)},
                {QStringLiteral("tracks"), tracks}, {QStringLiteral("discs"), discs}});
        }
        if (editions.isEmpty()) return bad(QStringLiteral("Existing catalogue has no editions."));
        expected.insert(QStringLiteral("editions"), editions);
        expected.insert(QStringLiteral("tracks"), editions.first().toMap().value(QStringLiteral("tracks")));
        expected.insert(QStringLiteral("discs"), editions.first().toMap().value(QStringLiteral("discs")));
        if (QJsonObject::fromVariantMap(expected) != QJsonObject::fromVariantMap(actual))
            return bad(QStringLiteral("Existing Library album differs from the full manifest/file-facts projection: ") + albumId);
        albums->append(actual);
    }
    return true;
}

bool existingIdentityIsFresh(const QByteArray &bytes, const QString &fixtureRoot)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto root = document.object();
    if (root.size() != 6 || root.value(QStringLiteral("version")).toDouble(-1) != 1
        || !root.value(QStringLiteral("entries")).isArray() || root.value(QStringLiteral("entries")).toArray().isEmpty()) return false;
    for (const QString &name : {QStringLiteral("aliases"), QStringLiteral("pathAliases"), QStringLiteral("ceremonyDecisions"), QStringLiteral("pendingCeremonies")})
        if (!root.value(name).isArray() || !root.value(name).toArray().isEmpty()) return false;
    QSet<QString> seen;
    for (const auto &value : root.value(QStringLiteral("entries")).toArray()) {
        const auto row = value.toObject();
        const QString id = row.value(QStringLiteral("id")).toString();
        const QString path = row.value(QStringLiteral("path")).toString();
        const QFileInfo facts(path);
        if (row.size() != 4 || id.isEmpty() || seen.contains(id) || !facts.isFile() || facts.isSymLink()
            || !VaultIdentity::normalizePath(facts.canonicalFilePath()).startsWith(VaultIdentity::normalizePath(fixtureRoot) + QLatin1Char('/'))
            || !row.value(QStringLiteral("size")).isDouble() || !row.value(QStringLiteral("mtimeMs")).isDouble()
            || row.value(QStringLiteral("size")).toDouble(-1) != double(facts.size())
            || row.value(QStringLiteral("mtimeMs")).toDouble(-1) != double(facts.lastModified().toMSecsSinceEpoch())
            || id != VaultIdentity::computeId(facts.canonicalFilePath(), facts.size(), facts.lastModified().toMSecsSinceEpoch())) return false;
        seen.insert(id);
    }
    return true;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vinyl_seed"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Test-only real-owner Vinyl seeder. Run with the fixture app stopped. Emits collections_seed fixture JSON on stdout."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("root"), QStringLiteral("Existing marked and tagged synthetic app-data root."), QStringLiteral("absolute-path")});
    parser.addOption({QStringLiteral("manifest"), QStringLiteral("Existing small WAV .vinyl.json beneath that root."), QStringLiteral("absolute-path")});
    parser.addOption({QStringLiteral("library"), QStringLiteral("Existing fixture Library INI for local, A, B, or legacy; empty unless exporting."), QStringLiteral("absolute-path")});
    parser.addOption({QStringLiteral("read-only-export"), QStringLiteral("Validate and export existing fixture owner data without importing or writing it.")});
    parser.process(app);
    if (!parser.positionalArguments().isEmpty()) return fail(QStringLiteral("Positional arguments are not accepted."));
    const QString root = absoluteClean(parser.value(QStringLiteral("root")));
    const QString manifest = absoluteClean(parser.value(QStringLiteral("manifest")));
    const QString library = absoluteClean(parser.value(QStringLiteral("library")));
    const bool readOnlyExport = parser.isSet(QStringLiteral("read-only-export"));
    if (root.isEmpty() || manifest.isEmpty() || library.isEmpty())
        return fail(QStringLiteral("All three options must be explicit absolute, clean paths."));
    CollectionsTestProfileController containment(root, {}, {});
    if (!containment.enabled()) return fail(QStringLiteral("Refusing an unmarked, untagged, linked, or non-fixture root."));
    const QStringList allowed{
        root + QStringLiteral("/profiles/local/collection.ini"),
        root + QStringLiteral("/profiles/11111111-1111-4111-8111-111111111181/collection.ini"),
        root + QStringLiteral("/profiles/11111111-1111-4111-8111-111111111182/collection.ini"),
        root + QStringLiteral("/profiles/legacy/collection.ini"),
        root + QStringLiteral("/collection-store.ini")};
    if (!allowed.contains(library)) return fail(QStringLiteral("Library path is outside the fixed synthetic profiles."));
    std::unique_ptr<QLockFile> fixtureLock;
    if (!readOnlyExport) {
        fixtureLock = std::make_unique<QLockFile>(root + QStringLiteral("/.collections-vinyl-seeding.lock"));
        if (!fixtureLock->tryLock(0)) return fail(QStringLiteral("Another fixture seed operation owns this root."));
    }
    QByteArray original;
    if ((!readOnlyExport && !emptyLibrary(library, &original))
        || (readOnlyExport && !readBounded(library, 2 * 1024 * 1024, &original)))
        return fail(QStringLiteral("Target Library INI must exist and contain no entries or unrelated settings."));
    const QString vault = root + QStringLiteral("/vault");
    if (!readOnlyExport && !noKnownIdentity(vault)) return fail(QStringLiteral("Existing Vault identity data or recovery files are never replaced."));
    QString error;
    if (!boundedManifest(root, manifest, &error)) return fail(error);
    QMap<QString, QByteArray> preserved;
    if (readOnlyExport) {
        preserved.insert(library, original);
        const QString identityPath = vault + QStringLiteral("/identity.json");
        QByteArray identityBytes;
        if (!readBounded(identityPath, 8 * 1024 * 1024, &identityBytes) || !existingIdentityIsFresh(identityBytes, root)
            || QFileInfo::exists(identityPath + QStringLiteral(".tmp")))
            return fail(QStringLiteral("Read-only export requires valid fresh fixture identity data without pending decisions/recovery."));
        preserved.insert(identityPath, identityBytes);
        if (QFileInfo::exists(identityPath + QStringLiteral(".bak"))) {
            QByteArray backup;
            if (!readBounded(identityPath + QStringLiteral(".bak"), 8 * 1024 * 1024, &backup)) return fail(QStringLiteral("Identity backup is unreadable."));
            preserved.insert(identityPath + QStringLiteral(".bak"), backup);
        }
        QByteArray manifestBytes;
        if (!readBounded(manifest, 256 * 1024, &manifestBytes)) return fail(QStringLiteral("Catalogue became unreadable."));
        preserved.insert(manifest, manifestBytes);
    }
    CollectionStore store(library);
    if (!store.healthy(&error) || (!readOnlyExport && !store.syncEntries().isEmpty()))
        return fail(error.isEmpty() ? QStringLiteral("Real Library owner is not empty.") : error);
    if (!containment.enabled() || (!readOnlyExport && !noKnownIdentity(vault)))
        return fail(QStringLiteral("Fixture containment or identity availability changed."));

    VaultIdentity identity(vault);
    VinylLocalSource source(&identity);
    // Passing the manifest alone uses its real importer without entering loose
    // file metadata inspection, libmpv, a player, or any network provider.
    QVariantList albums;
    if (readOnlyExport) {
        const QVariantList saved = store.items(QStringLiteral("Vinyl"));
        if (saved.isEmpty() || saved.size() > 4 || store.syncEntries().size() != saved.size()
            || !verifyExistingCatalogue(manifest, saved, identity, &albums, &error))
            return fail(QStringLiteral("Read-only fixture validation failed: ") + error);
    } else {
        const QVariantMap imported = source.importFiles({manifest});
        const QStringList errors = imported.value(QStringLiteral("errors")).toStringList();
        albums = imported.value(QStringLiteral("albums")).toList();
        if (!errors.isEmpty() || !imported.value(QStringLiteral("identityCeremonies")).toList().isEmpty()
            || albums.isEmpty() || !imported.value(QStringLiteral("replacedAlbumIds")).toList().isEmpty())
            return fail(QStringLiteral("Real importer did not produce a clean fresh fixture: ") + errors.join(QStringLiteral("; ")));
    }

    QVariantList entries;
    QJsonArray references;
    QSet<QString> artists;
    QStringList trackIds;
    QStringList manifestIds;
    for (const QVariant &value : albums) {
        const QVariantMap album = value.toMap();
        const QString id = album.value(QStringLiteral("id")).toString();
        const QString browseId = album.value(QStringLiteral("browseId"), id).toString();
        const QString title = album.value(QStringLiteral("title")).toString();
        const QString cover = album.value(QStringLiteral("cover")).toString();
        const QString creator = album.value(QStringLiteral("artists")).toStringList().join(QStringLiteral(" · "));
        const QString year = album.value(QStringLiteral("year")).toString();
        const QVariantList editions = album.value(QStringLiteral("editions")).toList();
        if (id.isEmpty() || browseId.isEmpty() || editions.isEmpty() || album.value(QStringLiteral("origin")).toString() != QLatin1String("local"))
            return fail(QStringLiteral("Real importer returned an incomplete local album identity."));
        const QString manifestId = album.value(QStringLiteral("manifestId")).toString();
        if (manifestId.isEmpty() || !identity.knows(manifestId))
            return fail(QStringLiteral("Real Vault owner did not admit the catalogue identity."));
        manifestIds.append(manifestId);
        entries.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("type"), QStringLiteral("album")},
            {QStringLiteral("title"), title}, {QStringLiteral("cover"), cover}, {QStringLiteral("artist"), creator},
            {QStringLiteral("browseId"), id}, {QStringLiteral("payload"), QVariantMap{
                {QStringLiteral("version"), 1}, {QStringLiteral("album"), album}, {QStringLiteral("saved"), true}}}});
        for (const QVariant &editionValue : editions) {
            const QVariantMap edition = editionValue.toMap();
            const QString editionId = edition.value(QStringLiteral("id")).toString();
            if (editionId.isEmpty()) return fail(QStringLiteral("Real importer returned an empty edition identity."));
            const QVariantMap route{{QStringLiteral("albumId"), id}, {QStringLiteral("browseId"), browseId}};
            references.append(QJsonObject::fromVariantMap(candidate(QStringLiteral("album"), id, editionId, title, cover, creator, year, route)));
            for (const QVariant &trackValue : edition.value(QStringLiteral("tracks")).toList()) {
                const QVariantMap track = trackValue.toMap();
                const QString trackId = track.value(QStringLiteral("id")).toString();
                if (trackId.isEmpty() || !identity.knows(trackId)) return fail(QStringLiteral("Real Vault owner did not admit a track identity."));
                trackIds.append(trackId);
                if (readOnlyExport) {
                    const QString path = track.value(QStringLiteral("localPath")).toString();
                    QByteArray bytes;
                    if (!readBounded(path, 4 * 1024 * 1024, &bytes)) return fail(QStringLiteral("Verified fixture audio became unreadable."));
                    preserved.insert(path, bytes);
                }
                QVariantMap trackRoute = route;
                trackRoute.insert(QStringLiteral("trackId"), trackId);
                const QString trackCreator = track.value(QStringLiteral("artists")).toStringList().join(QStringLiteral(" · "));
                references.append(QJsonObject::fromVariantMap(candidate(QStringLiteral("song"), trackId, editionId,
                    track.value(QStringLiteral("title")).toString(), cover, trackCreator.isEmpty() ? creator : trackCreator, year, trackRoute)));
            }
        }
        const QVariantMap artist = album.value(QStringLiteral("artist")).toMap();
        const QString artistId = artist.value(QStringLiteral("id")).toString();
        if (artistId.isEmpty()) return fail(QStringLiteral("Real importer did not expose an artist identity."));
        if (!artists.contains(artistId)) {
            artists.insert(artistId);
            references.append(QJsonObject::fromVariantMap(candidate(QStringLiteral("artist"), artistId, {},
                artist.value(QStringLiteral("name")).toString(), artist.value(QStringLiteral("cover")).toString(), {}, {}, {})));
        }
    }
    if (trackIds.isEmpty()) return fail(QStringLiteral("No owner-generated track identities were produced."));
    source.setAlbums(entries);
    for (const QVariant &album : albums)
        if (!verifyReadOnlyRoute(source, album.toMap().value(QStringLiteral("id")).toString(), trackIds.first(), album.toMap(), &error)) return fail(error);
    VaultIdentity reloadedIdentity(vault);
    for (const QString &trackId : trackIds)
        if (!reloadedIdentity.knows(trackId)) return fail(QStringLiteral("Vault identities did not survive a real owner reload."));
    for (const QString &manifestId : manifestIds)
        if (!reloadedIdentity.knows(manifestId)) return fail(QStringLiteral("Catalogue identity did not survive a real owner reload."));
    if (!readOnlyExport) {
        QByteArray current;
        if (!containment.enabled() || !emptyLibrary(library, &current) || current != original)
            return fail(QStringLiteral("Fixture containment or the empty Library changed during import."));
        for (const QVariant &entry : entries)
            if (!store.add(QStringLiteral("Vinyl"), entry.toMap())) return fail(store.persistenceError());
        store.flush();
    }
    if (!store.healthy(&error)) return fail(error);
    CollectionStore reloaded(library);
    const QVariantList saved = reloaded.items(QStringLiteral("Vinyl"));
    if (!reloaded.healthy(&error) || saved.size() != entries.size() || reloaded.syncEntries().size() != entries.size())
        return fail(QStringLiteral("Seeded Library did not survive a clean real-owner reload: ") + error);
    for (const QVariant &value : saved) {
        const QVariantMap row = value.toMap();
        bool exact = false;
        for (const QVariant &expected : entries)
            if (sameSavedEntry(row, expected.toMap())) exact = true;
        if (!exact) return fail(QStringLiteral("Library reload changed an imported album snapshot."));
    }
    if (readOnlyExport) {
        if (!containment.enabled() || QFileInfo::exists(vault + QStringLiteral("/identity.json.tmp")))
            return fail(QStringLiteral("Read-only export containment/recovery state changed."));
        for (auto it = preserved.constBegin(); it != preserved.constEnd(); ++it) {
            QByteArray current;
            if (!readBounded(it.key(), 8 * 1024 * 1024, &current) || current != it.value())
                return fail(QStringLiteral("Read-only export detected changed fixture bytes: ") + it.key());
        }
        if (!preserved.contains(vault + QStringLiteral("/identity.json.bak")) && QFileInfo::exists(vault + QStringLiteral("/identity.json.bak")))
            return fail(QStringLiteral("Read-only export detected a new identity backup."));
    }
    QSet<QString> uniqueTracks(trackIds.begin(), trackIds.end());
    QTextStream(stderr) << "vinyl_seed: " << entries.size() << " real local albums, "
        << uniqueTracks.size() << " admitted tracks, " << artists.size() << " artists; full JSON snapshots, read-only routes and owner reloads verified"
        << (readOnlyExport ? "; read-only export preserved input bytes.\n" : ".\n");
    // Deliberately the exact input schema of collections_seed: redirect stdout
    // to its --fixture argument. No canonical collection IDs are fabricated.
    const QJsonObject result{{QStringLiteral("collections"), QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("Local music fixtures")}, {QStringLiteral("references"), references}}}}};
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Indented);
    return 0;
}
