// ExtensionsStore.cpp — see ExtensionsStore.h for the contract.

#include "ExtensionsStore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {
constexpr int kManifestTimeoutMs = 12000;
constexpr int kDescriptionCap = 400;
// Generation of the house roster. 1 = the original four Theatre rows. 2 added the
// Tankoban and Biblio catalogues and wells, so those two worlds stop being empty
// tabs. 3 retired the WeebCentral/GetComics catalogue rows and emptied the house
// descriptions. 4 renamed the vault row to "Colosseum Grand Database".
// 7 gave the universes their real names and artwork.
// 8 seeds the keyless VidKing hosted-player extension into Theatre (enabled, removable).
// 9 seeds NoTorrent as Theatre's direct-HTTP source (measured 18/18 coverage, House HTTP slice 3).
// 10 retires VidKing (House HTTP slice 4, Hemanth 2026-08-07): the hosted-player surface is
//    removed from the app, so the row is pulled from every installed profile.
// Bump this whenever a house row is added, retired, OR its manifest copy changes —
// the migration re-runs once and now refreshes existing rows as well as adding new ones.
constexpr int kHouseDefaultsVersion = 10;
}

ExtensionsStore::ExtensionsStore(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam)
{
    loadIndex();
    if (m_items.isEmpty())
        seed();
    else if (m_defaultsVersion < kHouseDefaultsVersion)
        migrateDefaults();
}

// ---------------------------------------------------------------- persistence

QString ExtensionsStore::indexPath() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/extensions/installed.json");
}

void ExtensionsStore::loadIndex()
{
    QFile f(indexPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject root = doc.object();
    // Absent key = written before house defaults were versioned, i.e. generation 1.
    m_defaultsVersion = root.value(QStringLiteral("defaultsVersion")).toInt(1);
    const QJsonArray arr = root.value(QStringLiteral("extensions")).toArray();
    m_items.clear();
    for (const QJsonValue& v : arr) {
        const QVariantMap e = v.toObject().toVariantMap();
        if (!e.value(QStringLiteral("id")).toString().isEmpty())
            m_items.append(e);
    }
}

void ExtensionsStore::saveIndex() const
{
    QDir().mkpath(QFileInfo(indexPath()).absolutePath());
    QJsonArray arr;
    for (const QVariantMap& e : m_items)
        arr.append(QJsonObject::fromVariantMap(e));
    QJsonObject root;
    root.insert(QStringLiteral("v"), 1);
    root.insert(QStringLiteral("defaultsVersion"), kHouseDefaultsVersion);
    root.insert(QStringLiteral("extensions"), arr);

    QSaveFile f(indexPath());
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

void ExtensionsStore::bump()
{
    ++m_revision;
    emit changed();
}

// -------------------------------------------------------------------- seeding

// The four the house already runs on. Manifests are embedded (no network at
// boot); the real, richer manifest replaces the seed copy if the extension is
// ever re-installed by link.
// Returns true if anything was actually added or refreshed — the caller cannot infer
// that from the row COUNT, because a generation can add as many rows as it retires and
// a manifest refresh changes no count at all.

bool ExtensionsStore::appendHouseDefaults(bool onlyMissing)
{
    bool touched = false;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    auto entry = [now](const char* id, const char* url, bool core,
                       const QVariantMap& manifest) {
        QVariantMap e;
        e.insert(QStringLiteral("id"), QString::fromLatin1(id));
        e.insert(QStringLiteral("transportUrl"), QString::fromLatin1(url));
        e.insert(QStringLiteral("installedAt"), now);
        // First-run source consent (locked product rule, the Stremio model): a removable acquisition/
        // playback WELL — a non-core extension that provides `stream` sources — is seeded INSTALLED but
        // DISABLED until the user turns it on. Core catalogues and non-fetching capabilities
        // (catalog/meta/subtitles/universe) stay enabled. Derived from the manifest's resources + core,
        // never from string-matching a name. `add` preserves an existing profile's own enabled choice;
        // this default reaches only rows a profile does not already carry.
        const bool removableWell = !core
            && manifest.value(QStringLiteral("resources")).toStringList().contains(QStringLiteral("stream"));
        e.insert(QStringLiteral("enabled"), !removableWell);
        e.insert(QStringLiteral("core"), core);
        e.insert(QStringLiteral("manifest"), manifest);
        return e;
    };
    auto manifest = [](const char* id, const char* name, const char* desc,
                       const QStringList& resources, const QStringList& types,
                       const QStringList& idPrefixes, bool configurable) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), QString::fromLatin1(id));
        m.insert(QStringLiteral("name"), QString::fromLatin1(name));
        m.insert(QStringLiteral("description"), QString::fromUtf8(desc));
        m.insert(QStringLiteral("resources"), resources);
        m.insert(QStringLiteral("types"), types);
        if (!idPrefixes.isEmpty())
            m.insert(QStringLiteral("idPrefixes"), idPrefixes);
        if (configurable) {
            QVariantMap hints;
            hints.insert(QStringLiteral("configurable"), true);
            m.insert(QStringLiteral("behaviorHints"), hints);
        }
        return m;
    };

    // onlyMissing = the additive migration pass. It must never duplicate a row the
    // profile already carries, and never resurrect one the user deliberately removed
    // at an older defaults version (the version marker is what stops that).
    //
    // A row the profile already has still gets its MANIFEST refreshed, because a house
    // row's name and metadata are ours, not the user's — only its enabled flag and its
    // position are his, and those live outside the manifest. Without this, every change
    // to house copy reaches new installs only and silently skips every existing profile.
    // That bit twice in one day: the false descriptions stayed after they were deleted,
    // and "Colosseum Data" stayed after it was renamed. Remote add-ons are never touched
    // here — their manifest belongs to their author and arrives over the wire.
    auto add = [this, onlyMissing, &entry, &touched](const char* id, const char* url,
                                                     bool core, const QVariantMap& m) {
        const int at = indexOfId(QString::fromLatin1(id));
        if (at >= 0) {
            if (onlyMissing
                && m_items.at(at).value(QStringLiteral("manifest")).toMap() != m) {
                m_items[at].insert(QStringLiteral("manifest"), m);
                touched = true;
            }
            return;
        }
        m_items.append(entry(id, url, core, m));
        touched = true;
    };

    // ---- Theatre (defaults generation 1) -----------------------------------
    add("com.linvo.cinemeta", "https://v3-cinemeta.strem.io/manifest.json", true,
        manifest("com.linvo.cinemeta", "Cinemeta",
                 "The canonical catalog — every movie and show row Theatre wakes up with.",
                 { QStringLiteral("catalog"), QStringLiteral("meta") },
                 { QStringLiteral("movie"), QStringLiteral("series") },
                 { QStringLiteral("tt") }, false));
    add("com.stremio.torrentio.addon", "https://torrentio.strem.fun/manifest.json", false,
        manifest("com.stremio.torrentio.addon", "Torrentio",
                 "Play sources from twelve indexers, sorted by quality and health.",
                 { QStringLiteral("stream") },
                 { QStringLiteral("movie"), QStringLiteral("series"), QStringLiteral("anime") },
                 { QStringLiteral("tt"), QStringLiteral("kitsu") }, true));
    // NoTorrent — direct HTTP streams that play instantly, no torrent, no seeding. Seeded enabled
    // as Theatre's HTTP source ALONGSIDE Torrentio (not instead of it). Measured 2026-08-07 to
    // cover 18/18 Discover-catalog titles (House HTTP Source slice 3); its URLs are self-authed on
    // its own worker, so it needs no proxy headers. Generation 9.
    add("com.notorrent.addon", "https://addon.notorrent2.workers.dev/manifest.json", false,
        manifest("com.notorrent.addon", "NoTorrent",
                 "Direct HTTP streams — movies and shows that play instantly, no torrent, no seeding.",
                 { QStringLiteral("stream") },
                 { QStringLiteral("movie"), QStringLiteral("series") },
                 { QStringLiteral("tt"), QStringLiteral("tmdb_") }, false));
    add("community.anime.kitsu", "https://anime-kitsu.strem.fun/manifest.json", false,
        manifest("community.anime.kitsu", "Anime Kitsu",
                 "The anime shelf's brain — proper seasons, splits and episode orders.",
                 { QStringLiteral("catalog"), QStringLiteral("meta") },
                 { QStringLiteral("series"), QStringLiteral("movie"), QStringLiteral("anime") },
                 { QStringLiteral("kitsu"), QStringLiteral("mal") }, false));
    add("org.stremio.opensubtitlesv3", "https://opensubtitles-v3.strem.io/manifest.json", false,
        manifest("org.stremio.opensubtitlesv3", "OpenSubtitles v3",
                 "Subtitles in every language, matched to the exact episode playing.",
                 { QStringLiteral("subtitles") },
                 { QStringLiteral("movie"), QStringLiteral("series") },
                 {}, false));

    // ---- Catalogues: what fills the shelves. Locked (core), never ranked ----
    // Hemanth's ruling 2026-07-26: WeebCentral and GetComics are NOT our catalogues —
    // they are catalogues of what is available to DOWNLOAD, which is a well's job. Our
    // catalogues are the private data vault and AniList. Ground-truthed against the code:
    // the manga shelves wake on data/mal_catalog.db, comics on data/comics_catalog.db —
    // both shipped from the private Colosseum-Data repo — and AniList serves manga search
    // and series artwork. WeebCentral/GetComics are only consulted after a series opens.
    // Descriptions are gone by the same ruling: they needed no explaining, and the lines
    // we wrote were either obvious or false.
    add("colosseum.catalogue.vault", "colosseum://catalogue/vault", true,
        manifest("colosseum.catalogue.vault", "Colosseum Grand Database", "",
                 { QStringLiteral("catalog"), QStringLiteral("meta") },
                 { QStringLiteral("manga"), QStringLiteral("comic") }, {}, false));
    add("colosseum.catalogue.anilist", "colosseum://catalogue/anilist", true,
        manifest("colosseum.catalogue.anilist", "AniList", "",
                 { QStringLiteral("catalog"), QStringLiteral("meta") },
                 { QStringLiteral("manga") }, {}, false));
    add("colosseum.catalogue.applebooks", "colosseum://catalogue/applebooks", true,
        manifest("colosseum.catalogue.applebooks", "Apple Books", "",
                 { QStringLiteral("catalog"), QStringLiteral("meta") },
                 { QStringLiteral("book") }, {}, false));

    // ---- Wells: what actually fetches. Ranked, removable, asked top-first ----
    // Array order IS ask-order, and each world's rank is that world's filtered index.
    // This order gives Tankoban 1-4 and Biblio 1-3 exactly as the design's roster says,
    // with the one shared Torrent Indexers row landing 4th in Tankoban and 2nd in Biblio.
    add("colosseum.well.nyaa", "colosseum://well/nyaa", false,
        manifest("colosseum.well.nyaa", "Nyaa", "",
                 { QStringLiteral("stream") }, { QStringLiteral("manga") }, {}, false));
    add("colosseum.well.weebcentral.pages", "colosseum://well/weebcentral.pages", false,
        manifest("colosseum.well.weebcentral.pages", "WeebCentral", "",
                 { QStringLiteral("stream") }, { QStringLiteral("manga") }, {}, false));
    add("colosseum.well.getcomics.issues", "colosseum://well/getcomics.issues", false,
        manifest("colosseum.well.getcomics.issues", "GetComics", "",
                 { QStringLiteral("stream") }, { QStringLiteral("comic") }, {}, false));
    add("colosseum.well.libgen", "colosseum://well/libgen", false,
        manifest("colosseum.well.libgen", "LibGen", "",
                 { QStringLiteral("stream") }, { QStringLiteral("book") }, {}, false));
    // One well, two worlds. Hemanth overrode the four-separate-extensions recommendation,
    // so it carries a Configure sheet (stage 4). Named "Tankorent" on his word 2026-07-26 —
    // which is what the engine has called it all along (native/torrent/TankorentSearchService).
    // The id stays `colosseum.well.indexers`: renaming it would orphan the row in his profile.
    // OPEN, awaiting his ruling: the `audiobook` type below is a dead claim — the federated
    // search is only ever asked for "books" and "comics", never audiobooks. Left in place
    // rather than silently changed, because dropping it changes Biblio's roster.
    add("colosseum.well.indexers", "colosseum://well/indexers", false,
        manifest("colosseum.well.indexers", "Tankorent", "",
                 { QStringLiteral("stream") },
                 { QStringLiteral("comic"), QStringLiteral("book"), QStringLiteral("audiobook") },
                 {}, true));
    add("colosseum.well.audiobookbay", "colosseum://well/audiobookbay", false,
        manifest("colosseum.well.audiobookbay", "AudioBookBay", "",
                 { QStringLiteral("stream") }, { QStringLiteral("audiobook") }, {}, false));

    // ---- Universes: an IP gathered across every medium it lives in ---------
    // A universe is classified by ROLE, not content: the `universe` resource is checked
    // BEFORE the type derivation, so One Piece lands in exactly one place instead of
    // appearing four times (manga + anime + film + its own). Universes design §5.1a, and
    // already guarded by tests/extension_worlds_derivation_test.mjs.
    //
    // They fetch nothing and are never ranked — Hemanth caught that an ask-order framing
    // breaks for them, which is why ExtensionsSources gives them a section with no ranks
    // and keeps them out of the chain entirely.
    //
    // Auto-installed for now on his word 2026-07-26 ("the 2 being auto-installed for now").
    // The spec's end state is remote manifests fetched over HTTPS (§5.5); seeding them here
    // is the honest interim — the roster is where every other house row already lives, and
    // it means the fourth row is real without inventing a server we have not built.
    //
    // Artwork is verified in each plan §2. One Piece's logo is a transparent wordmark;
    // DCAU has no metahub entry of its own so its identity art is anchored to BTAS, the
    // origin work. The page renders the NAME as Fraunces text, so `logo` is used only by
    // the Home carousel tile.
    auto universe = [&manifest](const char* id, const char* name,
                                const char* logo, const char* background) {
        QVariantMap m = manifest(id, name, "",
                                 { QStringLiteral("universe") },
                                 { QStringLiteral("universe") }, {}, false);
        m.insert(QStringLiteral("logo"), QString::fromLatin1(logo));
        m.insert(QStringLiteral("background"), QString::fromLatin1(background));
        return m;
    };
    add("com.colosseum.universe.onepiece", "colosseum://universe/onepiece", false,
        universe("com.colosseum.universe.onepiece", "One Piece",
                 "https://images.metahub.space/logo/medium/tt0388629/img",
                 "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg"));
    add("com.colosseum.universe.dcau", "colosseum://universe/dcau", false,
        universe("com.colosseum.universe.dcau", "DC Animated Universe",
                 "https://images.metahub.space/logo/medium/tt0103359/img",
                 "https://images.metahub.space/background/medium/tt0103359/img"));

    return touched;
}

void ExtensionsStore::seed()
{
    appendHouseDefaults(/*onlyMissing=*/false);
    m_defaultsVersion = kHouseDefaultsVersion;
    saveIndex();
    bump();
}

// Rows the house once shipped and has since disowned. A profile that already carries
// one must have it taken away — additive migration alone would leave it sitting there
// forever. Generation 3 retires the WeebCentral and GetComics *catalogue* rows: both
// sites are where downloads come from, not where our shelves come from (Hemanth's
// ruling 2026-07-26). Their well rows are untouched — that role was always correct.
static const char* const kRetiredIds[] = {
    "colosseum.catalogue.weebcentral",
    "colosseum.catalogue.getcomics",
    // Generation 10: the VidKing hosted player is gone from the app (House HTTP slice 4) —
    // a profile still carrying the row would render nothing for it.
    "net.vidking.player",
};

// An existing profile predates a house row that now ships. Add only what is absent,
// stamp the new generation, and never run again for that generation — so removing a
// well stays removed across restarts.
void ExtensionsStore::migrateDefaults()
{
    // Count is NOT a change signal: generation 3 retired two rows and added two, netting
    // zero, and a manifest refresh changes no count at all. Track the edit explicitly or
    // the UI never refreshes.
    bool changed = false;

    for (const char* id : kRetiredIds) {
        const int at = indexOfId(QString::fromLatin1(id));
        if (at >= 0) {
            m_items.removeAt(at);
            changed = true;
        }
    }

    if (appendHouseDefaults(/*onlyMissing=*/true))
        changed = true;

    m_defaultsVersion = kHouseDefaultsVersion;
    saveIndex();
    if (changed)
        bump();
}

// ---------------------------------------------------------------------- reads

QVariantList ExtensionsStore::installed() const
{
    QVariantList out;
    for (const QVariantMap& e : m_items)
        out.append(e);
    return out;
}

int ExtensionsStore::indexOfId(const QString& id) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).value(QStringLiteral("id")).toString() == id)
            return i;
    return -1;
}

bool ExtensionsStore::isInstalled(const QString& urlOrId) const
{
    const QString url = normalizeUrl(urlOrId);
    for (const QVariantMap& e : m_items) {
        if (e.value(QStringLiteral("id")).toString() == urlOrId)
            return true;
        if (!url.isEmpty()
            && e.value(QStringLiteral("transportUrl")).toString() == url)
            return true;
    }
    return false;
}

QString ExtensionsStore::universePayload(const QString& file) const
{
    // `file` is a bare stem (e.g. "one-piece") from UniverseExtApi.fileFor(), never a path
    // and never an extension id. Reject anything else before it touches a path — this is
    // the only thing standing between an installed extension id and path traversal.
    static const QRegularExpression kStem(QStringLiteral("^[a-z0-9-]+$"));
    if (!kStem.match(file).hasMatch())
        return QString();

    // Same resolution order as MalCatalog: the relative path first (true when the
    // argless launch has anchored CWD on the repo root), then applicationDirPath()
    // + "/../../" (true for a dev/test harness launch that left CWD elsewhere — the
    // exe sits at <repo>/native/build-msvc/, so two hops up is the repo root).
    QString path = QStringLiteral("assets/universes/") + file + QStringLiteral(".json");
    if (!QFileInfo::exists(path)) {
        const QString beside = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/../../") + path;
        if (QFileInfo::exists(beside)) path = beside;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// ------------------------------------------------------------------ mutations

void ExtensionsStore::remove(const QString& id)
{
    const int i = indexOfId(id);
    if (i < 0 || m_items.at(i).value(QStringLiteral("core")).toBool())
        return;                                  // core rows cannot leave
    m_items.removeAt(i);
    saveIndex();
    bump();
}

void ExtensionsStore::setEnabled(const QString& id, bool on)
{
    const int i = indexOfId(id);
    if (i < 0 || m_items.at(i).value(QStringLiteral("core")).toBool())
        return;                                  // the house catalog is always on
    if (m_items.at(i).value(QStringLiteral("enabled")).toBool() == on)
        return;
    m_items[i].insert(QStringLiteral("enabled"), on);
    saveIndex();
    bump();
}

// Reorder is a WORLD-relative act performed on a GLOBAL array, and the two disagree
// whenever another world's row sits between two of this world's wells — which is the
// normal case, not the corner case.
//
// This replaces move(id, ±1), which swapped GLOBAL neighbours and was wrong twice over.
// Simulated against the shipped defaults: of Tankoban's 8 possible arrow presses, 4 moved
// a row past a Biblio-only row and changed nothing on screen — and 3 of those silently
// reordered BIBLIO's ask-order. A user curating his manga sources was editing his book
// sources, with no feedback in either world. (A5's audit P0-3, and worse than it read.)
//
// The destination is computed in QML because ExtensionsCatalog.js owns world derivation;
// duplicating that here would be a second source of truth for "the next well in Tankoban".
// This end does one thing: move a row to an absolute index, and refuse to rank a catalogue.
void ExtensionsStore::moveTo(const QString& id, int index)
{
    const int i = indexOfId(id);
    if (i < 0)
        return;
    if (m_items.at(i).value(QStringLiteral("core")).toBool())
        return;                                  // catalogues are never ranked
    const int j = qBound(0, index, int(m_items.size()) - 1);
    if (i == j)
        return;
    m_items.move(i, j);
    saveIndex();
    bump();
}

// ----------------------------------------------------------- manifest fetches

QString ExtensionsStore::normalizeUrl(const QString& raw) const
{
    QString url = raw.trimmed();
    if (url.isEmpty())
        return {};
    // House catalogues and wells live in-app on a local scheme. They have no manifest
    // document to fetch, so pass them through UNTOUCHED — the blind
    // "+= /manifest.json" below is what made directory-page installs 404.
    if (url.startsWith(QStringLiteral("colosseum://"), Qt::CaseInsensitive))
        return url;
    if (url.startsWith(QStringLiteral("stremio://"), Qt::CaseInsensitive))
        url = QStringLiteral("https://") + url.mid(10);
    if (!url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
        && !url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive))
        url = QStringLiteral("https://") + url;
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    if (!url.endsWith(QStringLiteral("manifest.json"), Qt::CaseInsensitive))
        url += QStringLiteral("/manifest.json");
    return url;
}

QVariantMap ExtensionsStore::slimManifest(const QJsonObject& m)
{
    QVariantMap out;
    out.insert(QStringLiteral("id"), m.value(QStringLiteral("id")).toString());
    out.insert(QStringLiteral("name"), m.value(QStringLiteral("name")).toString());
    const QString version = m.value(QStringLiteral("version")).toString();
    if (!version.isEmpty())
        out.insert(QStringLiteral("version"), version);
    QString desc = m.value(QStringLiteral("description")).toString();
    if (desc.size() > kDescriptionCap)
        desc = desc.left(kDescriptionCap);
    if (!desc.isEmpty())
        out.insert(QStringLiteral("description"), desc);
    const QString logo = m.value(QStringLiteral("logo")).toString();
    if (!logo.isEmpty() && !logo.startsWith(QStringLiteral("data:")))
        out.insert(QStringLiteral("logo"), logo);

    // resources kept verbatim (string OR {name,types,idPrefixes}) — AddonClient
    // runs Harbor's matching over exactly this shape.
    if (m.contains(QStringLiteral("resources")))
        out.insert(QStringLiteral("resources"),
                   m.value(QStringLiteral("resources")).toArray().toVariantList());
    if (m.contains(QStringLiteral("types")))
        out.insert(QStringLiteral("types"),
                   m.value(QStringLiteral("types")).toArray().toVariantList());
    if (m.contains(QStringLiteral("idPrefixes")))
        out.insert(QStringLiteral("idPrefixes"),
                   m.value(QStringLiteral("idPrefixes")).toArray().toVariantList());

    // catalogs slimmed to what row-loading needs: id, type, name, extra name+isRequired
    if (m.contains(QStringLiteral("catalogs"))) {
        QVariantList cats;
        const QJsonArray inCats = m.value(QStringLiteral("catalogs")).toArray();
        for (const QJsonValue& cv : inCats) {
            const QJsonObject c = cv.toObject();
            QVariantMap cat;
            cat.insert(QStringLiteral("id"), c.value(QStringLiteral("id")).toString());
            cat.insert(QStringLiteral("type"), c.value(QStringLiteral("type")).toString());
            cat.insert(QStringLiteral("name"), c.value(QStringLiteral("name")).toString());
            QVariantList extras;
            const QJsonArray inExtras = c.value(QStringLiteral("extra")).toArray();
            for (const QJsonValue& ev : inExtras) {
                const QJsonObject ex = ev.toObject();
                QVariantMap e;
                e.insert(QStringLiteral("name"), ex.value(QStringLiteral("name")).toString());
                if (ex.value(QStringLiteral("isRequired")).toBool())
                    e.insert(QStringLiteral("isRequired"), true);
                extras.append(e);
            }
            if (!extras.isEmpty())
                cat.insert(QStringLiteral("extra"), extras);
            cats.append(cat);
        }
        out.insert(QStringLiteral("catalogs"), cats);
    }

    const QJsonObject hintsIn = m.value(QStringLiteral("behaviorHints")).toObject();
    if (!hintsIn.isEmpty()) {
        QVariantMap hints;
        for (const char* key : { "adult", "p2p", "configurable", "configurationRequired" }) {
            const QString k = QString::fromLatin1(key);
            if (hintsIn.value(k).toBool())
                hints.insert(k, true);
        }
        if (!hints.isEmpty())
            out.insert(QStringLiteral("behaviorHints"), hints);
    }
    return out;
}

bool ExtensionsStore::manifestIsAdult(const QVariantMap& slim)
{
    return slim.value(QStringLiteral("behaviorHints")).toMap()
               .value(QStringLiteral("adult")).toBool();
}

void ExtensionsStore::setShowExplicit(bool v)
{
    if (m_showExplicit == v)
        return;
    m_showExplicit = v;
    emit showExplicitChanged();
}

void ExtensionsStore::fetchManifest(const QString& transportUrl, bool thenInstall)
{
    QNetworkRequest req{ QUrl(transportUrl) };
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(kManifestTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, transportUrl, thenInstall]() {
        reply->deleteLater();
        auto fail = [this, transportUrl, thenInstall](const QString& reason) {
            if (thenInstall)
                emit installFailed(transportUrl, reason);
            else
                emit previewFailed(transportUrl, reason);
        };
        if (reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("The address didn't answer (%1).")
                     .arg(reply->errorString()));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            fail(QStringLiteral("That address didn't return an extension manifest."));
            return;
        }
        const QVariantMap slim = slimManifest(doc.object());
        if (slim.value(QStringLiteral("id")).toString().isEmpty()
            || slim.value(QStringLiteral("name")).toString().isEmpty()) {
            fail(QStringLiteral("The manifest is missing its id or name."));
            return;
        }
        // Follows the global Explicit Content preference (ContentPreferences.showExplicit,
        // bound in Main.qml) rather than refusing outright: with the setting on, the user
        // installs whatever they point at. The message names the switch so the refusal is
        // actionable instead of a dead end.
        if (!m_showExplicit && manifestIsAdult(slim)) {
            fail(QStringLiteral("This is an adult extension. Turn on Explicit Content in "
                                "Settings to install it."));
            return;
        }
        m_previewCache.insert(transportUrl, slim);
        if (thenInstall)
            finishInstall(transportUrl, slim);
        else
            emit previewReady(transportUrl, slim);
    });
}

void ExtensionsStore::preview(const QString& rawUrl)
{
    const QString url = normalizeUrl(rawUrl);
    if (url.isEmpty()) {
        emit previewFailed(rawUrl, QStringLiteral("Paste an extension address first."));
        return;
    }
    fetchManifest(url, /*thenInstall=*/false);
}

void ExtensionsStore::install(const QString& rawUrl)
{
    const QString url = normalizeUrl(rawUrl);
    if (url.isEmpty()) {
        emit installFailed(rawUrl, QStringLiteral("Paste an extension address first."));
        return;
    }
    if (m_previewCache.contains(url)) {
        finishInstall(url, m_previewCache.value(url));
        return;
    }
    fetchManifest(url, /*thenInstall=*/true);
}

void ExtensionsStore::finishInstall(const QString& transportUrl, const QVariantMap& slim)
{
    const QString id = slim.value(QStringLiteral("id")).toString();

    QVariantMap entry;
    entry.insert(QStringLiteral("id"), id);
    entry.insert(QStringLiteral("transportUrl"), transportUrl);
    entry.insert(QStringLiteral("installedAt"), QDateTime::currentSecsSinceEpoch());
    entry.insert(QStringLiteral("enabled"), true);
    entry.insert(QStringLiteral("core"), false);
    entry.insert(QStringLiteral("manifest"), slim);

    const int existing = indexOfId(id);
    if (existing >= 0) {
        // same id = update in place (keeps its position, its core flag and its switch)
        entry.insert(QStringLiteral("core"),
                     m_items.at(existing).value(QStringLiteral("core")));
        entry.insert(QStringLiteral("enabled"),
                     m_items.at(existing).value(QStringLiteral("enabled")));
        m_items[existing] = entry;
    } else {
        m_items.append(entry);
    }
    saveIndex();
    bump();
    emit installFinished(id, slim.value(QStringLiteral("name")).toString());
}
