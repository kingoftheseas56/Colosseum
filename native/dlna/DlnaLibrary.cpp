#include "DlnaLibrary.h"

#include "engine/VaultConfig.h"
#include "engine/VaultKit.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QMetaMethod>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <climits>

namespace {

const QString kRootId = QStringLiteral("0");
const QString kContainerClass = QStringLiteral("object.container.storageFolder");

QString sha16(const QString& key)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
// Opaque, stable ids: only a hash of the owner's id / key ever leaves, never a path.
QString itemIdFor(const QString& sourceKey) { return QLatin1Char('i') + sha16(sourceKey); }
QString containerIdFor(const QString& kind, const QString& key)
{
    return QLatin1Char('c') + sha16(kind + QLatin1Char(':') + key);
}

QString dedupeKey(const QString& canonicalPath)
{
    const QString p = QDir::cleanPath(canonicalPath);
#ifdef Q_OS_WIN
    return p.toCaseFolded();
#else
    return p;
#endif
}

std::shared_ptr<DlnaSnapshot> emptySnapshot()
{
    auto snap = std::make_shared<DlnaSnapshot>();
    DlnaContainer root;
    root.id = kRootId;
    root.parentId = QStringLiteral("-1");
    root.title = QStringLiteral("Colosseum");
    root.upnpClass = kContainerClass;
    snap->containers.insert(kRootId, root);
    return snap;
}

// ── Owner-thread input, copied out so the worker never touches a QObject ────────────────
struct VRow {
    VaultIndex::FileRow row;
    QString kind;
    bool synthetic = false;
};
struct AudiobookIn {
    QString id, title, author;
    QStringList files;
};
struct BuildInput {
    QList<VRow> rows;               // already passed every owner-side (config/index) check
    QList<AudiobookIn> audiobooks;
    DlnaLibrary::CoverResolver cover;
};

// One physical file that passed eligibility. size/mtimeMs come from the fresh stat.
struct Cand {
    VaultIndex::FileRow row;
    qint64 size = 0;
    qint64 mtimeMs = 0;
    bool synthetic = false;
};

QString groupKeyOf(const VaultIndex::FileRow& r)
{
    if (!r.groupKey.isEmpty())
        return r.groupKey;
    if (!r.subtreePath.isEmpty())
        return r.subtreePath;
    return QFileInfo(r.path).absolutePath();
}

QStringList subfolderParts(const VaultIndex::FileRow& r)
{
    return QDir::fromNativeSeparators(r.subfolder).split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

// Samples, trailers, extras, featurettes, behind-the-scenes: never listed (Kodi lists no samples).
// Folder segments are matched whole; file names only on delimited tokens so a real title such as
// "Trailer Park Boys" or "Sample Size" is not swallowed.
bool isJunkVideo(const VaultIndex::FileRow& r)
{
    static const QSet<QString> junkDirs = {
        QStringLiteral("sample"), QStringLiteral("samples"), QStringLiteral("trailer"),
        QStringLiteral("trailers"), QStringLiteral("featurette"), QStringLiteral("featurettes"),
        QStringLiteral("extra"), QStringLiteral("extras"), QStringLiteral("behind the scenes"),
        QStringLiteral("behind-the-scenes"), QStringLiteral("behind_the_scenes")};
    for (const QString& part : subfolderParts(r))
        if (VaultKit::isExtrasDirName(part) || junkDirs.contains(part.trimmed().toLower()))
            return true;
    static const QRegularExpression delimited(
        QStringLiteral("(^|[._\\-\\[\\(])(sample|trailer|featurettes?|behind[ ._\\-]the[ ._\\-]scenes)($|[._\\-\\]\\)])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression spaced(
        QStringLiteral("\\b(featurettes?|behind the scenes)\\b|\\s-\\s(sample|trailer)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const QString base = QFileInfo(r.realName.isEmpty() ? r.path : r.realName).completeBaseName();
    return delimited.match(base).hasMatch() || spaced.match(base).hasMatch();
}

int seasonFolderOrdinal(const VaultIndex::FileRow& r, bool* found)
{
    for (const QString& part : subfolderParts(r)) {
        if (VaultKit::isSeasonLikeDirName(part)) {
            *found = true;
            return VaultKit::seasonOrdinalFromDirName(part);
        }
    }
    *found = false;
    return 0;
}

QString fileTitle(const VaultIndex::FileRow& r)
{
    if (!r.displayTitle.trimmed().isEmpty())
        return r.displayTitle.trimmed();
    const QString base = QFileInfo(r.realName.isEmpty() ? r.path : r.realName).completeBaseName();
    return base.isEmpty() ? r.realName : base;
}

QString groupTitleOf(const QList<const Cand*>& rows)
{
    // rows are pre-sorted; the first certain identity title wins, then the cleaned folder name.
    for (const Cand* c : rows)
        if (!c->row.identityTitle.trimmed().isEmpty())
            return c->row.identityTitle.trimmed();
    for (const Cand* c : rows)
        if (!c->row.groupTitle.trimmed().isEmpty())
            return c->row.groupTitle.trimmed();
    const QString key = groupKeyOf(rows.first()->row);
    const QString leaf = QFileInfo(key).fileName();
    return leaf.isEmpty() ? QStringLiteral("Untitled") : leaf;
}

// Row order inside a group: the index's natural sort key, then real name, then id.
bool rowLess(const Cand* a, const Cand* b)
{
    const QString ka = a->row.sortKey.isEmpty() ? VaultIndex::naturalSortKey(a->row.realName) : a->row.sortKey;
    const QString kb = b->row.sortKey.isEmpty() ? VaultIndex::naturalSortKey(b->row.realName) : b->row.sortKey;
    if (ka != kb)
        return ka < kb;
    if (a->row.realName != b->row.realName)
        return a->row.realName < b->row.realName;
    return a->row.id < b->row.id;
}

QList<const Cand*> sortedPtrs(const QList<Cand>& list)
{
    QList<const Cand*> out;
    out.reserve(list.size());
    for (const Cand& c : list)
        out.append(&c);
    std::sort(out.begin(), out.end(), rowLess);
    return out;
}

// /art serves jpg/jpeg/png thumbnails up to 10 MB; any other cover is not advertised. A path
// that does not exist yet passes (the server re-checks at request time and answers 404).
QString servableCover(const QString& path)
{
    if (path.isEmpty())
        return QString();
    const QFileInfo fi(path);
    const QString suffix = fi.suffix().toLower();
    if (suffix != QLatin1String("jpg") && suffix != QLatin1String("jpeg")
        && suffix != QLatin1String("png"))
        return QString();
    if (fi.exists() && (!fi.isFile() || fi.size() > 10 * 1024 * 1024))
        return QString();
    return path;
}

// The snapshot under construction: children are appended in the order callers add them, so
// callers add already-sorted lists.
struct Builder {
    DlnaSnapshot& snap;
    const DlnaLibrary::CoverResolver& cover;
    QCollator collator;

    Builder(DlnaSnapshot& s, const DlnaLibrary::CoverResolver& c) : snap(s), cover(c)
    {
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
    }

    bool titleLess(const QString& a, const QString& b, const QString& ida, const QString& idb) const
    {
        const int c = collator.compare(a, b);
        return c != 0 ? c < 0 : ida < idb;
    }

    void addContainer(const QString& id, const QString& parentId, const QString& title)
    {
        DlnaContainer c;
        c.id = id;
        c.parentId = parentId;
        c.title = title;
        c.upnpClass = kContainerClass;
        snap.containers.insert(id, c);
        if (snap.containers.contains(parentId))
            snap.containers[parentId].childContainerIds.append(id);
    }

    void addItems(const QString& parentId, const QList<DlnaItem>& items)
    {
        for (const DlnaItem& it : items) {
            snap.items.insert(it.id, it);
            snap.containers[parentId].childItemIds.append(it.id);
        }
    }

    // Fields every vault-sourced item shares. Title / class / season are the caller's call.
    DlnaItem vaultItem(const Cand& c, const QString& parentId, const QString& upnpClass,
                       const QString& title) const
    {
        const VaultIndex::FileRow& r = c.row;
        DlnaItem it;
        it.id = itemIdFor(r.id);
        it.parentId = parentId;
        it.title = title;
        it.upnpClass = upnpClass;
        it.filePath = r.path;
        it.mime = DlnaLibrary::mimeForPath(r.path);
        it.size = c.size;
        it.mtimeMs = c.mtimeMs;
        it.durationSec = r.durationSec > 0 ? r.durationSec : -1;
        it.year = r.identityYear > 0 ? r.identityYear : r.parsedYear;
        it.genres = r.identityGenres;
        it.synopsis = !r.identitySynopsis.isEmpty() ? r.identitySynopsis : r.synopsis;
        it.author = r.author;
        if (cover)
            it.coverPath = servableCover(cover(r));
        return it;
    }
};

// ── Owner thread: read the owners, apply every check that needs a QObject ───────────────
BuildInput gatherInput(VaultIndex* index, VaultConfig* config, QObject* audiobooks,
                       const DlnaLibrary::CoverResolver& cover)
{
    BuildInput in;
    in.cover = cover;
    if (index && config) {
        QHash<QString, bool> rootOk; // config-side only; the folder-exists stat happens on the worker
        auto rootEligible = [&](const QString& root) {
            const auto it = rootOk.constFind(root);
            if (it != rootOk.constEnd())
                return it.value();
            const bool ok = !root.isEmpty() && config->hasRoot(root) && config->isRootConfirmed(root)
                && !config->isRootHidden(root);
            rootOk.insert(root, ok);
            return ok;
        };
        QHash<QString, bool> rootSynthetic;
        for (const QString kind : {QStringLiteral("video"), QStringLiteral("book"), QStringLiteral("comic")}) {
            const QList<VaultIndex::FileRow> rows = index->rowsForKind(kind);
            for (const VaultIndex::FileRow& r : rows) {
                if (r.id.isEmpty() || r.path.isEmpty() || r.away || !r.errorState.isEmpty())
                    continue;
                if (kind == QLatin1String("video") && r.admissionVerdict != QLatin1String("Admitted"))
                    continue;
                if (!rootEligible(r.rootPath) || config->isHidden(r.id))
                    continue;
                VRow v;
                v.row = r;
                v.kind = kind;
                const auto s = rootSynthetic.constFind(r.rootPath);
                if (s != rootSynthetic.constEnd()) {
                    v.synthetic = s.value();
                } else {
                    v.synthetic = config->isSyntheticRoot(r.rootPath);
                    rootSynthetic.insert(r.rootPath, v.synthetic);
                }
                in.rows.append(v);
            }
        }
    }
    if (audiobooks) {
        QVariantList list;
        QMetaObject::invokeMethod(audiobooks, "downloadedAudiobooks", Qt::DirectConnection,
                                  Q_RETURN_ARG(QVariantList, list));
        for (const QVariant& v : std::as_const(list)) {
            const QVariantMap m = v.toMap();
            const QString id = m.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || m.value(QStringLiteral("missing")).toBool())
                continue;
            AudiobookIn a;
            a.id = id;
            a.title = m.value(QStringLiteral("title")).toString().trimmed();
            a.author = m.value(QStringLiteral("author")).toString().trimmed();
            QMetaObject::invokeMethod(audiobooks, "localFiles", Qt::DirectConnection,
                                      Q_RETURN_ARG(QStringList, a.files), Q_ARG(QString, id));
            in.audiobooks.append(a);
        }
    }
    return in;
}

// ── Worker thread: stats + projection. Pure function of its input. ──────────────────────
std::shared_ptr<DlnaSnapshot> buildSnapshot(const BuildInput& in)
{
    auto snap = emptySnapshot();
    Builder b(*snap, in.cover);

    // ── 1. Physical eligibility: folder exists, file exists with the indexed size ────────
    QHash<QString, bool> rootDirExists;
    auto rootThere = [&](const QString& root) {
        const auto it = rootDirExists.constFind(root);
        if (it != rootDirExists.constEnd())
            return it.value();
        const bool ok = QFileInfo(root).isDir();
        rootDirExists.insert(root, ok);
        return ok;
    };

    // Physical-file dedupe across roots (a user root and the downloads root can reach the same
    // file): canonical path -> best candidate. A real user-root row beats the synthetic
    // downloads row; ties break on the smaller row id so the winner is deterministic.
    QHash<QString, Cand> unique;
    QHash<QString, QString> uniqueKind;
    for (const VRow& v : in.rows) {
        const VaultIndex::FileRow& r = v.row;
        if (v.kind == QLatin1String("video") && isJunkVideo(r))
            continue; // cheap, string-only: reject before any stat
        if (!v.synthetic && !rootThere(r.rootPath))
            continue; // a vanished root is skipped without a stat per file
        const QFileInfo fi(r.path);
        // The stat IS necessary: the index keeps rows for deleted/replaced files (the downloads
        // root especially) until the next publish, and `away` only covers a vanished root.
        if (!fi.isFile() || fi.size() != r.size)
            continue;
        const QString canonical = fi.canonicalFilePath();
        if (canonical.isEmpty())
            continue;
        Cand c;
        c.row = r;
        if (v.kind == QLatin1String("video") && VaultKit::isEpisodeVideo(r.realName)
            && (groupKeyOf(r) == r.rootPath || groupKeyOf(r).endsWith(QLatin1String("::LOOSE")))) {
            const QString title = VaultKit::episodeSeriesTitle(r.realName);
            if (!title.isEmpty()) {
                c.row.groupTitle = title;
                c.row.groupKey = VaultKit::episodeShowKey(r.rootPath, title);
                c.row.subtreePath = c.row.groupKey;
            }
        }
        c.size = fi.size();
        c.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
        c.synthetic = v.synthetic;
        const QString key = dedupeKey(canonical);
        const auto prior = unique.constFind(key);
        if (prior != unique.constEnd()) {
            const bool better = (prior->synthetic && !c.synthetic)
                || (prior->synthetic == c.synthetic && c.row.id < prior->row.id);
            if (!better)
                continue;
        }
        unique.insert(key, c);
        uniqueKind.insert(key, v.kind);
    }
    QSet<QString> seenPaths; // also stops an audiobook file from repeating a vault file
    QHash<QString, QList<Cand>> videoGroups, comicGroups;
    QList<Cand> books;
    for (auto it = unique.constBegin(); it != unique.constEnd(); ++it) {
        seenPaths.insert(it.key());
        const QString kind = uniqueKind.value(it.key());
        if (kind == QLatin1String("video"))
            videoGroups[groupKeyOf(it->row)].append(*it);
        else if (kind == QLatin1String("comic"))
            comicGroups[groupKeyOf(it->row)].append(*it);
        else
            books.append(*it);
    }

    // ── 2. Videos: an episode (SxxExx, a season folder, or a fansub run of absolute numbers)
    //      goes to Shows; EVERY other video is a Movie - collections and trilogies included. ──
    struct ShowOut { QString key, title; QMap<int, QList<DlnaItem>> seasons; };
    QList<DlnaItem> movies;
    QList<ShowOut> shows;
    const QString movieClass = QStringLiteral("object.item.videoItem.movie");
    const QString plainClass = QStringLiteral("object.item.videoItem");

    QStringList videoKeys = videoGroups.keys();
    std::sort(videoKeys.begin(), videoKeys.end());
    for (const QString& key : videoKeys) {
        const QList<const Cand*> rows = sortedPtrs(videoGroups[key]);
        if (rows.isEmpty())
            continue;
        // Absolute numbering ("Show - 003") only counts when the folder holds a run of them; one
        // lone "Movie - 300" file must stay a film.
        int absoluteRun = 0;
        for (const Cand* c : rows) {
            const VaultKit::SeasonEpisode se = VaultKit::parseEpisodeNumber(c->row.realName);
            if (se.matched && se.absolute)
                ++absoluteRun;
        }
        QList<const Cand*> episodeRows, movieRows;
        for (const Cand* c : rows) {
            bool folderFound = false;
            seasonFolderOrdinal(c->row, &folderFound);
            const VaultKit::SeasonEpisode se = VaultKit::parseEpisodeNumber(c->row.realName);
            const bool isEpisode = folderFound
                || VaultKit::isEpisodeVideo(c->row.realName, absoluteRun);
            (isEpisode ? episodeRows : movieRows).append(c);
        }

        const bool loose = key.endsWith(QStringLiteral("::LOOSE"));
        for (const Cand* c : movieRows) {
            // A group's adopted identity names the film only when the folder holds just that one
            // film; in a collection each file keeps its own cleaned title.
            const bool useIdentity = !loose && movieRows.size() == 1
                && !c->row.identityTitle.trimmed().isEmpty();
            movies.append(b.vaultItem(*c, QStringLiteral("t-movies"), movieClass,
                                      useIdentity ? c->row.identityTitle.trimmed() : fileTitle(c->row)));
        }

        if (!episodeRows.isEmpty()) {
            ShowOut show;
            show.key = key;
            show.title = groupTitleOf(episodeRows);
            for (const Cand* c : episodeRows) {
                bool folderFound = false;
                const int folderSeason = seasonFolderOrdinal(c->row, &folderFound);
                const VaultKit::SeasonEpisode se = VaultKit::parseEpisodeNumber(c->row.realName);
                int season;
                if (se.matched && !se.absolute)
                    season = se.season;                       // explicit SxxExx wins
                else if (folderFound)
                    season = folderSeason;                    // "Season 02" folder
                else if (!subfolderParts(c->row).isEmpty())
                    season = 0;                               // Bonus/OVA folder -> Specials
                else
                    season = 1;                               // flat, absolute-numbered show
                const QString seasonCid = containerIdFor(QStringLiteral("season"),
                                                         key + QLatin1Char('#') + QString::number(season));
                DlnaItem it = b.vaultItem(*c, seasonCid, plainClass, fileTitle(c->row));
                it.season = season;
                it.episode = se.matched ? se.episode : 0;
                it.seriesTitle = show.title;
                show.seasons[season].append(it);
            }
            shows.append(show);
        }
    }

    std::sort(movies.begin(), movies.end(), [&](const DlnaItem& x, const DlnaItem& y) {
        return b.titleLess(x.title, y.title, x.id, y.id);
    });
    if (!movies.isEmpty()) {
        b.addContainer(QStringLiteral("t-movies"), kRootId, QStringLiteral("Movies"));
        b.addItems(QStringLiteral("t-movies"), movies);
        snap->typeOrder << QStringLiteral("Movies");
    }

    std::sort(shows.begin(), shows.end(), [&](const ShowOut& x, const ShowOut& y) {
        return b.titleLess(x.title, y.title, x.key, y.key);
    });
    if (!shows.isEmpty()) {
        const QString showsId = QStringLiteral("t-shows");
        b.addContainer(showsId, kRootId, QStringLiteral("Shows"));
        for (ShowOut& show : shows) {
            const QString showCid = containerIdFor(QStringLiteral("show"), show.key);
            b.addContainer(showCid, showsId, show.title);
            for (auto sit = show.seasons.begin(); sit != show.seasons.end(); ++sit) { // ascending; 0 first
                const QString seasonCid = containerIdFor(QStringLiteral("season"),
                    show.key + QLatin1Char('#') + QString::number(sit.key()));
                b.addContainer(seasonCid, showCid,
                               sit.key() == 0 ? QStringLiteral("Specials")
                                              : QStringLiteral("Season %1").arg(sit.key()));
                QList<DlnaItem>& eps = sit.value();
                std::sort(eps.begin(), eps.end(), [&](const DlnaItem& x, const DlnaItem& y) {
                    const int ex = x.episode > 0 ? x.episode : INT_MAX;
                    const int ey = y.episode > 0 ? y.episode : INT_MAX;
                    if (ex != ey)
                        return ex < ey;
                    return b.titleLess(x.title, y.title, x.id, y.id);
                });
                b.addItems(seasonCid, eps);
            }
        }
        snap->typeOrder << QStringLiteral("Shows");
    }

    // ── 3. Books: flat by title ───────────────────────────────────────────────────────────
    {
        QList<DlnaItem> items;
        for (const Cand& c : books)
            items.append(b.vaultItem(c, QStringLiteral("t-books"), QStringLiteral("object.item.textItem"),
                !c.row.identityTitle.trimmed().isEmpty() ? c.row.identityTitle.trimmed() : fileTitle(c.row)));
        std::sort(items.begin(), items.end(), [&](const DlnaItem& x, const DlnaItem& y) {
            return b.titleLess(x.title, y.title, x.id, y.id);
        });
        if (!items.isEmpty()) {
            b.addContainer(QStringLiteral("t-books"), kRootId, QStringLiteral("Books"));
            b.addItems(QStringLiteral("t-books"), items);
            snap->typeOrder << QStringLiteral("Books");
        }
    }

    // ── 4. Comics: series (groupTitle) > issues ───────────────────────────────────────────
    {
        struct Series { QString key, title; QList<DlnaItem> items; };
        QList<Series> series;
        for (auto it = comicGroups.constBegin(); it != comicGroups.constEnd(); ++it) {
            const QList<const Cand*> rows = sortedPtrs(it.value());
            Series s;
            s.key = it.key();
            s.title = groupTitleOf(rows);
            const QString cid = containerIdFor(QStringLiteral("comic"), it.key());
            for (const Cand* c : rows)
                s.items.append(b.vaultItem(*c, cid, QStringLiteral("object.item"), fileTitle(c->row)));
            std::sort(s.items.begin(), s.items.end(), [&](const DlnaItem& x, const DlnaItem& y) {
                return b.titleLess(x.title, y.title, x.id, y.id);
            });
            series.append(s);
        }
        std::sort(series.begin(), series.end(), [&](const Series& x, const Series& y) {
            return b.titleLess(x.title, y.title, x.key, y.key);
        });
        if (!series.isEmpty()) {
            b.addContainer(QStringLiteral("t-comics"), kRootId, QStringLiteral("Comics"));
            for (const Series& s : series) {
                const QString cid = containerIdFor(QStringLiteral("comic"), s.key);
                b.addContainer(cid, QStringLiteral("t-comics"), s.title);
                b.addItems(cid, s.items);
            }
            snap->typeOrder << QStringLiteral("Comics");
        }
    }

    // ── 5. Audiobooks: one container per downloaded book > its files in natural order ────
    {
        struct Book { QString id, title; QList<DlnaItem> items; };
        QList<Book> booksOut;
        for (const AudiobookIn& a : in.audiobooks) {
            const QString title = a.title.isEmpty() ? QStringLiteral("Audiobook") : a.title;
            const QString cid = containerIdFor(QStringLiteral("audiobook"), a.id);
            QList<DlnaItem> good;
            for (const QString& file : a.files) {
                const QFileInfo fi(file);
                if (!fi.isFile())
                    continue;
                const QString canonical = fi.canonicalFilePath();
                if (canonical.isEmpty())
                    continue;
                const QString key = dedupeKey(canonical);
                if (seenPaths.contains(key))
                    continue;
                seenPaths.insert(key);
                DlnaItem it;
                it.id = itemIdFor(QStringLiteral("audiobook:") + a.id + QLatin1Char('/') + fi.fileName());
                it.parentId = cid;
                it.title = fi.completeBaseName();
                it.upnpClass = QStringLiteral("object.item.audioItem.audioBook");
                it.filePath = fi.absoluteFilePath();
                it.mime = DlnaLibrary::mimeForPath(file);
                it.size = fi.size();
                it.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
                it.author = a.author;
                good.append(it);
            }
            if (good.isEmpty())
                continue;
            if (good.size() == 1)
                good.first().title = title; // single-file book: the item wears the book's name
            booksOut.append({a.id, title, good}); // provider order = natural order, kept as-is
        }
        std::sort(booksOut.begin(), booksOut.end(), [&](const Book& x, const Book& y) {
            return b.titleLess(x.title, y.title, x.id, y.id);
        });
        if (!booksOut.isEmpty()) {
            b.addContainer(QStringLiteral("t-audiobooks"), kRootId, QStringLiteral("Audiobooks"));
            for (const Book& book : booksOut) {
                const QString cid = containerIdFor(QStringLiteral("audiobook"), book.id);
                b.addContainer(cid, QStringLiteral("t-audiobooks"), book.title);
                b.addItems(cid, book.items);
            }
            snap->typeOrder << QStringLiteral("Audiobooks");
        }
    }

    snap->itemCount = snap->items.size();
    return snap;
}

} // namespace

// ───────────────────────────────────────────────────────────────────────────

DlnaLibrary::DlnaLibrary(VaultIndex* index, VaultConfig* config, QObject* audiobooks,
                         QObject* vaultLibrary, QObject* parent)
    : QObject(parent), m_index(index), m_config(config), m_audiobooks(audiobooks),
      m_vaultLibrary(vaultLibrary)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(300);
    connect(&m_debounce, &QTimer::timeout, this, &DlnaLibrary::rebuildNow);
    connect(&m_watcher, &QFutureWatcher<std::shared_ptr<DlnaSnapshot>>::finished, this,
            &DlnaLibrary::onBuildFinished);
    m_snapshot = emptySnapshot();
}

DlnaLibrary::~DlnaLibrary()
{
    // An in-flight worker owns only copied data; wait so it cannot outlive its result slot.
    if (m_building)
        m_watcher.waitForFinished();
}

void DlnaLibrary::setInitialSystemUpdateId(quint32 value)
{
    if (value <= m_lastUpdateId)
        return; // never go backwards
    m_lastUpdateId = value;
    auto copy = std::make_shared<DlnaSnapshot>(*snapshot());
    copy->systemUpdateId = value;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_snapshot = std::move(copy);
}

quint32 DlnaLibrary::currentSystemUpdateId() const
{
    return snapshot()->systemUpdateId;
}

void DlnaLibrary::start()
{
    if (m_started)
        return;
    m_started = true;
    if (m_index)
        m_connections << connect(m_index, &VaultIndex::changed, this, &DlnaLibrary::scheduleRebuild);
    if (m_config)
        m_connections << connect(m_config, &VaultConfig::changed, this, &DlnaLibrary::scheduleRebuild);
    if (m_vaultLibrary
        && m_vaultLibrary->metaObject()->indexOfSignal(QMetaObject::normalizedSignature("changed()")) >= 0)
        m_connections << connect(m_vaultLibrary, SIGNAL(changed()), this, SLOT(scheduleRebuild()));
    if (m_audiobooks) {
        const QMetaObject* mo = m_audiobooks->metaObject();
        if (mo->indexOfSignal(QMetaObject::normalizedSignature("finished(QString,QString)")) >= 0)
            m_connections << connect(m_audiobooks, SIGNAL(finished(QString,QString)), this,
                                     SLOT(scheduleRebuild()));
        if (mo->indexOfSignal(QMetaObject::normalizedSignature("removed(QString)")) >= 0)
            m_connections << connect(m_audiobooks, SIGNAL(removed(QString)), this,
                                     SLOT(scheduleRebuild()));
    }
    m_debounce.start();
}

void DlnaLibrary::stop()
{
    if (!m_started)
        return;
    m_started = false;
    m_debounce.stop();
    for (const QMetaObject::Connection& c : std::as_const(m_connections))
        disconnect(c);
    m_connections.clear();

    // Any build already running is stale: bump the epoch so its result is discarded.
    ++m_epoch;
    m_rebuildPending = false;
    publish(emptySnapshot(), false);
}

void DlnaLibrary::scheduleRebuild()
{
    if (m_started)
        m_debounce.start(); // restart: coalesce a burst into one rebuild after it settles
}

void DlnaLibrary::rebuildNow()
{
    m_debounce.stop();
    if (m_building) {
        m_rebuildPending = true; // owners may have moved after this build read them
        return;
    }
    // Owner thread: read the owners and copy everything the worker needs.
    BuildInput input = gatherInput(m_index, m_config, m_audiobooks, m_coverResolver);
    qInfo().noquote() << QStringLiteral("[dlna] rebuild: %1 eligible vault rows, %2 audiobooks")
                             .arg(input.rows.size()).arg(input.audiobooks.size());
    m_building = true;
    m_jobEpoch = m_epoch;
    m_watcher.setFuture(QtConcurrent::run([input = std::move(input)]() { return buildSnapshot(input); }));
}

void DlnaLibrary::onBuildFinished()
{
    std::shared_ptr<DlnaSnapshot> next = m_watcher.result();
    const bool stale = m_jobEpoch != m_epoch;
    m_building = false;
    if (!stale && next)
        publish(std::move(next), true);
    if (m_rebuildPending) {
        m_rebuildPending = false;
        rebuildNow();
    }
}

std::shared_ptr<const DlnaSnapshot> DlnaLibrary::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snapshot;
}

void DlnaLibrary::publish(std::shared_ptr<DlnaSnapshot> next, bool notify)
{
    // The owner thread is the only writer, so the (possibly large) content compare runs outside
    // the lock; readers only ever wait for the pointer swap.
    if (snapshot()->sameContent(*next))
        return;
    next->systemUpdateId = ++m_lastUpdateId;
    qInfo().noquote() << QStringLiteral("[dlna] published %1 items (%2), update id %3")
                             .arg(next->itemCount).arg(next->typeOrder.join(QLatin1String(", ")))
                             .arg(next->systemUpdateId);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_snapshot = std::move(next);
    }
    if (notify)
        emit snapshotChanged();
}

QString DlnaLibrary::mimeForPath(const QString& path)
{
    static const QHash<QString, QString> table = {
        {QStringLiteral("mkv"), QStringLiteral("video/x-matroska")},
        {QStringLiteral("mp4"), QStringLiteral("video/mp4")},
        {QStringLiteral("m4v"), QStringLiteral("video/mp4")},
        {QStringLiteral("avi"), QStringLiteral("video/x-msvideo")},
        {QStringLiteral("webm"), QStringLiteral("video/webm")},
        {QStringLiteral("ts"), QStringLiteral("video/mp2t")},
        {QStringLiteral("mov"), QStringLiteral("video/quicktime")},
        {QStringLiteral("wmv"), QStringLiteral("video/x-ms-wmv")},
        {QStringLiteral("flv"), QStringLiteral("video/x-flv")},
        {QStringLiteral("mpg"), QStringLiteral("video/mpeg")},
        {QStringLiteral("mpeg"), QStringLiteral("video/mpeg")},
        {QStringLiteral("ogv"), QStringLiteral("video/ogg")},
        {QStringLiteral("mp3"), QStringLiteral("audio/mpeg")},
        {QStringLiteral("m4a"), QStringLiteral("audio/mp4")},
        {QStringLiteral("m4b"), QStringLiteral("audio/mp4")},
        {QStringLiteral("aac"), QStringLiteral("audio/aac")},
        {QStringLiteral("flac"), QStringLiteral("audio/flac")},
        {QStringLiteral("ogg"), QStringLiteral("audio/ogg")},
        {QStringLiteral("opus"), QStringLiteral("audio/ogg")},
        {QStringLiteral("wav"), QStringLiteral("audio/wav")},
        {QStringLiteral("epub"), QStringLiteral("application/epub+zip")},
        {QStringLiteral("pdf"), QStringLiteral("application/pdf")},
        {QStringLiteral("cbz"), QStringLiteral("application/vnd.comicbook+zip")},
        {QStringLiteral("cbr"), QStringLiteral("application/vnd.comicbook-rar")},
        {QStringLiteral("mobi"), QStringLiteral("application/x-mobipocket-ebook")},
        {QStringLiteral("azw3"), QStringLiteral("application/vnd.amazon.ebook")},
        {QStringLiteral("fb2"), QStringLiteral("application/x-fictionbook+xml")},
        {QStringLiteral("djvu"), QStringLiteral("image/vnd.djvu")},
        {QStringLiteral("jpg"), QStringLiteral("image/jpeg")},
        {QStringLiteral("jpeg"), QStringLiteral("image/jpeg")},
        {QStringLiteral("png"), QStringLiteral("image/png")},
    };
    return table.value(QFileInfo(path).suffix().toLower(), QStringLiteral("application/octet-stream"));
}
