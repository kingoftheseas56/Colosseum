// DownloadsFeed.cpp.draft — page.downloads feed + actions for the web Downloads page (W2-4).
// Source of truth: qml/DownloadsPage.qml + qml/BackgroundActivitySection.qml (main checkout);
// every moved block cites its QML lines. Naming per BRIEF-W2-4: stays a .draft until it compiles.
//
// Sections (all layout:"custom", SCHEMA.md in surfaces/downloads/):
//   downloads.header / .now / .activity / .remote / .shelf.{tankoban,biblio,theatre} / .audiobooks
// Params: { ledgerWorld, ledgerKey } — the open series ledger (QML toggleLedger :200-211).
//
// Routing actions delegate through the bridge's `open` dispatcher (Main.qml:4621 onActionRequested)
// or open sessions directly via the Sessions service, exactly as Main.qml's own routers do —
// no Main.qml edit is needed (this brief forbids it).

#include "ActionRegistry.h"
#include "FeedRegistry.h"
#include "../ColosseumWebBridge.h"

#include "../../ProgressStore.h"
#include "../../SessionStore.h"
#include "../../engine/AudiobookDownloader.h"
#include "../../engine/LocalDownloads.h"
#include "../../engine/MangaDownloader.h"
#include "../../work/BackgroundActivityRegistry.h"

#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QDateTime>
#include <QHash>
#include <algorithm>

namespace {

// ---- services (context property names from native/main.cpp:988,1299,1538,1627,1811) ----
LocalDownloads *downloads(ColosseumWebBridge &bridge)
{
    return qobject_cast<LocalDownloads *>(bridge.service(QStringLiteral("LocalDownloads")));
}
AudiobookDownloader *audiobooks(ColosseumWebBridge &bridge)
{
    return qobject_cast<AudiobookDownloader *>(bridge.service(QStringLiteral("Audiobooks")));
}
MangaDownloader *manga(ColosseumWebBridge &bridge)
{
    return qobject_cast<MangaDownloader *>(bridge.service(QStringLiteral("Downloads")));
}
work::BackgroundActivityRegistry *activity(ColosseumWebBridge &bridge)
{
    return qobject_cast<work::BackgroundActivityRegistry *>(
        bridge.service(QStringLiteral("BackgroundActivity")));
}
SessionStore *sessions(ColosseumWebBridge &bridge)
{
    return qobject_cast<SessionStore *>(bridge.service(QStringLiteral("Sessions")));
}
ProgressStore *progress(ColosseumWebBridge &bridge)
{
    return qobject_cast<ProgressStore *>(bridge.service(QStringLiteral("Progress")));
}

// ---- source-cooldown facts (DownloadsPage.qml:92-98, 400-413) ----
// MangaDownloader::paused(chapterId, resumeInMs) parks a soft-blocked page download; any
// progress/finish/fail/remove for that chapter lifts the wall. Held here (epoch ms).
QHash<QString, qint64> g_coolResumeAt;

bool isLiveState(const QString &state)                        // DownloadsPage.qml:83-90
{
    return state == QLatin1String("queued") || state == QLatin1String("resolving")
        || state == QLatin1String("downloading") || state == QLatin1String("paused")
        || state == QLatin1String("extracting") || state == QLatin1String("packing")
        || state == QLatin1String("ingesting");
}

// EpisodeBrowser.seriesRootId derivation (Main.qml:2059 uses it; DownloadsPage.qml:229-236
// documents the same rule): series base = stream id minus its trailing :season:episode.
QString seriesRootId(const QString &id)
{
    const QStringList parts = id.split(QLatin1Char(':'));
    if (parts.size() >= 3)
        return parts.mid(0, parts.size() - 2).join(QLatin1Char(':'));
    return id;
}

QVariantMap section(const QString &id, int index, const QString &state,
                    QVariantMap data = {})
{
    QVariantMap out{{QStringLiteral("id"), id},
                    {QStringLiteral("index"), index},
                    {QStringLiteral("title"), QString()},
                    {QStringLiteral("layout"), QStringLiteral("custom")},
                    {QStringLiteral("state"), state},
                    {QStringLiteral("items"), QVariantList{}}};
    if (!data.isEmpty()) {
        data.insert(QStringLiteral("schema"), QStringLiteral("downloads.") + id.section(QLatin1Char('.'), 1));
        out.insert(QStringLiteral("data"), data);
    }
    return out;
}

// Web-safe job row: the QML row minus native-only routing fields (url/headers/partPath stay
// native; playArriving resolves them by {world,id}).
QVariantMap publicJobRow(const QVariantMap &j)
{
    QVariantMap r;
    for (const char *k : {"id", "title", "world", "state", "detail", "error", "groupKey",
                          "groupUnit", "badge", "seriesTitle"}) {
        const QString key = QString::fromLatin1(k);
        if (j.contains(key)) r.insert(key, j.value(key));
    }
    for (const char *k : {"received", "total", "speed", "etaSec", "ratio", "episode", "season"}) {
        const QString key = QString::fromLatin1(k);
        if (j.contains(key)) r.insert(key, j.value(key));
    }
    for (const char *k : {"canPlay", "canRetry", "canPause", "canResume", "canCancel", "canDismiss"}) {
        const QString key = QString::fromLatin1(k);
        r.insert(key, j.value(key, false));
    }
    return r;
}

// Web-safe ledger row (path/art stay native; openItem re-resolves by {world,id}).
QVariantMap publicLedgerRow(const QVariantMap &m)
{
    QVariantMap r;
    for (const char *k : {"id", "title", "subtitle", "seriesTitle", "world", "kind",
                          "packRole", "author", "bookId"}) {
        const QString key = QString::fromLatin1(k);
        if (m.contains(key)) r.insert(key, m.value(key));
    }
    for (const char *k : {"bytes", "addedAt"}) {
        const QString key = QString::fromLatin1(k);
        if (m.contains(key)) r.insert(key, m.value(key));
    }
    r.insert(QStringLiteral("missing"), m.value(QStringLiteral("missing"), false));
    return r;
}

// groupJobs (DownloadsPage.qml:329-381) — moved verbatim in behavior.
QVariantList groupJobs(const QVariantList &jobs)
{
    struct Group {
        QString key, world, title, seriesTitle, groupUnit;
        QVariantList rows;
        int doneCount = 0, liveCount = 0, season = 0;
        double received = 0, total = 0, speed = 0, ratio = 0;
        qint64 eta = -1;
        bool single = false, hasKnownTotal = false;
        QVariantMap firstRow;
    };
    QList<Group> groups;
    QHash<QString, int> byKey;
    for (const QVariant &v : jobs) {
        const QVariantMap j = v.toMap();
        const QString key = j.value(QStringLiteral("world")).toString() + QLatin1Char('|')
            + j.value(QStringLiteral("groupKey"), j.value(QStringLiteral("id"))).toString();
        if (!byKey.contains(key)) {
            Group g;
            g.key = key;
            g.world = j.value(QStringLiteral("world"), QStringLiteral("theatre")).toString();
            byKey.insert(key, groups.size());
            groups.append(g);
        }
        Group &g = groups[byKey.value(key)];
        g.rows.append(j);
        const QString state = j.value(QStringLiteral("state")).toString();
        if (state == QLatin1String("done")) g.doneCount++;
        else if (isLiveState(state)) g.liveCount++;
        if ((j.value(QStringLiteral("total"), 0).toDouble()) > 0) {
            g.received += j.value(QStringLiteral("received"), 0).toDouble();
            g.total += j.value(QStringLiteral("total"), 0).toDouble();
        }
        if (state == QLatin1String("downloading"))
            g.speed += j.value(QStringLiteral("speed"), 0).toDouble();
        const QVariant etaV = j.value(QStringLiteral("etaSec"));
        bool etaOk = false;
        const qint64 eta = etaV.toLongLong(&etaOk);
        if (etaOk && eta >= 0) g.eta = qMax(g.eta, eta);
        if (g.rows.size() == 1) g.firstRow = j;
    }
    QVariantList out;
    for (Group &g : groups) {
        // m_acq iteration order is unspecified (QML :355-358) — sort rows by id so an
        // expanded fold never reshuffles between refresh ticks.
        std::sort(g.rows.begin(), g.rows.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("id")).toString()
                 < b.toMap().value(QStringLiteral("id")).toString();
        });
        g.single = g.rows.size() == 1;
        g.hasKnownTotal = g.total > 0;
        g.ratio = g.hasKnownTotal
            ? qBound(0.0, g.received / g.total, 1.0) : 0.0;
        const QVariantMap first = g.rows.first().toMap();
        g.season = first.value(QStringLiteral("season"), 0).toInt();
        g.seriesTitle = first.value(QStringLiteral("seriesTitle")).toString();
        g.groupUnit = first.value(QStringLiteral("groupUnit")).toString();
        const QString base = !g.seriesTitle.isEmpty() ? g.seriesTitle
            : (!first.value(QStringLiteral("title")).toString().isEmpty()
               ? first.value(QStringLiteral("title")).toString() : QStringLiteral("Download"));
        if (g.single)
            g.title = first.value(QStringLiteral("title")).toString().isEmpty()
                ? QStringLiteral("Download") : first.value(QStringLiteral("title")).toString();
        else if (!g.groupUnit.isEmpty())
            g.title = base + QStringLiteral(" — ") + QString::number(g.rows.size())
                      + QLatin1Char(' ') + g.groupUnit;
        else
            g.title = base + QStringLiteral(" — Season ") + QString::number(g.season);

        QVariantList rows;
        for (const QVariant &r : g.rows) rows.append(publicJobRow(r.toMap()));
        // cooldown fact for a single-row group (QML :612-616 shows it on the subtitle)
        qint64 cool = 0;
        if (g.single) {
            const QString id = first.value(QStringLiteral("id")).toString();
            const qint64 at = g_coolResumeAt.value(id, 0);
            if (at > QDateTime::currentMSecsSinceEpoch()) cool = at;
        }
        QVariantMap gm{{QStringLiteral("key"), g.key},
                       {QStringLiteral("world"), g.world},
                       {QStringLiteral("single"), g.single},
                       {QStringLiteral("title"), g.title},
                       {QStringLiteral("groupUnit"), g.groupUnit},
                       {QStringLiteral("count"), g.rows.size()},
                       {QStringLiteral("doneCount"), g.doneCount},
                       {QStringLiteral("liveCount"), g.liveCount},
                       {QStringLiteral("received"), g.received},
                       {QStringLiteral("total"), g.total},
                       {QStringLiteral("hasKnownTotal"), g.hasKnownTotal},
                       {QStringLiteral("ratio"), g.ratio},
                       {QStringLiteral("speed"), g.speed},
                       {QStringLiteral("eta"), static_cast<double>(g.eta)},
                       {QStringLiteral("season"), g.season},
                       {QStringLiteral("seriesTitle"), g.seriesTitle},
                       {QStringLiteral("coolResumeAt"), static_cast<double>(cool)},
                       {QStringLiteral("rows"), rows}};
        out.append(gm);
    }
    return out;
}

// computeLedgerSeasons (DownloadsPage.qml:237-282) — theatre episode ledgers only.
QVariantList ledgerSeasons(const QVariantList &items, const QVariantList &jobs)
{
    bool any = false;
    for (const QVariant &v : items) {
        const QVariantMap e = v.toMap();
        if (e.value(QStringLiteral("kind")).toString() == QLatin1String("episode")
            && e.value(QStringLiteral("season"), 0).toInt() > 0) any = true;
    }
    if (!any) return {};
    struct Season { int season = 0; QVariantList items; double bytes = 0; qint64 newest = 0; int arriving = 0; };
    QList<Season> out;
    QHash<int, int> bySeason;
    for (const QVariant &v : items) {
        const QVariantMap it = v.toMap();
        const int s = it.value(QStringLiteral("season"), 0).toInt();
        if (!bySeason.contains(s)) {
            Season ss; ss.season = s;
            bySeason.insert(s, out.size());
            out.append(ss);
        }
        Season &ss = out[bySeason.value(s)];
        ss.items.append(publicLedgerRow(it));
        ss.bytes += it.value(QStringLiteral("bytes"), 0).toDouble();
        ss.newest = qMax(ss.newest, it.value(QStringLiteral("addedAt"), 0).toLongLong());
    }
    // live cross-reference (QML :257-269): groupKey join first, title+season fallback.
    for (const QVariant &v : jobs) {
        const QVariantMap jb = v.toMap();
        if (jb.value(QStringLiteral("world")).toString() != QLatin1String("theatre")
            || jb.value(QStringLiteral("state")).toString() == QLatin1String("done")) continue;
        for (Season &ss : out) {
            const QVariantMap first = ss.items.first().toMap();
            const QStringList parts = first.value(QStringLiteral("id")).toString().split(QLatin1Char(':'));
            const QString wantKey = parts.size() >= 3
                ? parts.mid(0, parts.size() - 2).join(QLatin1Char(':'))
                  + QStringLiteral(":s") + QString::number(ss.season) : QString();
            if (!wantKey.isEmpty()
                ? jb.value(QStringLiteral("groupKey")).toString() == wantKey
                : first.value(QStringLiteral("seriesTitle")).toString().toLower()
                      == jb.value(QStringLiteral("seriesTitle")).toString().toLower()
                  && jb.value(QStringLiteral("season"), 0).toInt() == ss.season)
                ss.arriving++;
        }
    }
    std::sort(out.begin(), out.end(), [](const Season &a, const Season &b) { return a.season < b.season; });
    // Default fold: newest season open, ONLY when the ledger was just opened (QML :273-280).
    int newestSeason = out.isEmpty() ? 0 : out.first().season;
    qint64 newestAt = -1;
    for (const Season &ss : out)
        if (ss.newest > newestAt) { newestAt = ss.newest; newestSeason = ss.season; }
    QVariantList result;
    for (const Season &ss : out) {
        QVariantMap sm{{QStringLiteral("season"), ss.season},
                       {QStringLiteral("items"), ss.items},
                       {QStringLiteral("bytes"), ss.bytes},
                       {QStringLiteral("newest"), static_cast<double>(ss.newest)},
                       {QStringLiteral("arriving"), ss.arriving},
                       {QStringLiteral("defaultOpen"), ss.season == newestSeason}};
        result.append(sm);
    }
    return result;
}

QString fmtBytes(double b)                                     // DownloadsPage.qml:284-289
{
    if (b >= 1073741824) return QString::number(b / 1073741824, 'f', 1) + QStringLiteral(" GB");
    if (b >= 1048576) return QString::number(qRound(b / 1048576)) + QStringLiteral(" MB");
    if (b > 0) return QString::number(qMax<qint64>(1, qRound(b / 1024))) + QStringLiteral(" KB");
    return QString();
}

bool valid(const QVariantMap &params)
{
    if (params.isEmpty()) return true;
    if (params.size() > 2) return false;
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (it.key() != QLatin1String("ledgerWorld") && it.key() != QLatin1String("ledgerKey")) return false;
        if (it.value().typeId() != QMetaType::QString) return false;
    }
    if (params.contains(QStringLiteral("ledgerWorld"))
        && !params.value(QStringLiteral("ledgerWorld")).toString().isEmpty()) {
        const QString w = params.value(QStringLiteral("ledgerWorld")).toString();
        if (w != QLatin1String("tankoban") && w != QLatin1String("biblio") && w != QLatin1String("theatre"))
            return false;
    }
    return true;
}

QVariantList initial(const QVariantMap &)
{
    return {section(QStringLiteral("downloads.header"), 0, QStringLiteral("loading")),
            section(QStringLiteral("downloads.now"), 1, QStringLiteral("loading")),
            section(QStringLiteral("downloads.activity"), 2, QStringLiteral("loading")),
            section(QStringLiteral("downloads.remote"), 3, QStringLiteral("loading")),
            section(QStringLiteral("downloads.shelf.tankoban"), 4, QStringLiteral("loading")),
            section(QStringLiteral("downloads.shelf.biblio"), 5, QStringLiteral("loading")),
            section(QStringLiteral("downloads.shelf.theatre"), 6, QStringLiteral("loading")),
            section(QStringLiteral("downloads.audiobooks"), 7, QStringLiteral("loading"))};
}

void capture(ColosseumWebBridge &bridge, FeedContext &context)
{
    // GUI-thread snapshot (CONTRACT §5.5): read-models only, immutable copies to the worker.
    QVariantMap snap;
    if (auto *ld = downloads(bridge)) {
        snap.insert(QStringLiteral("totals"), ld->totals());
        snap.insert(QStringLiteral("jobs"), ld->activeJobs());
        QVariantMap lanes;
        for (const char *w : {"tankoban", "biblio", "theatre"})
            lanes.insert(QString::fromLatin1(w), ld->series(QString::fromLatin1(w)));
        snap.insert(QStringLiteral("lanes"), lanes);
        snap.insert(QStringLiteral("remote"), ld->availableElsewhere());
        const QString lw = context.params.value(QStringLiteral("ledgerWorld")).toString();
        const QString lk = context.params.value(QStringLiteral("ledgerKey")).toString();
        if (!lw.isEmpty() && !lk.isEmpty())
            snap.insert(QStringLiteral("ledgerItems"), ld->items(lw, lk));
    }
    if (auto *ab = audiobooks(bridge)) {
        snap.insert(QStringLiteral("abDone"), ab->downloadedAudiobooks());
        snap.insert(QStringLiteral("abActive"), ab->activeDownloads());
    }
    if (auto *reg = activity(bridge))
        snap.insert(QStringLiteral("activities"), reg->activities());
    context.nativeSnapshot.insert(QStringLiteral("downloads"), snap);
}

QVariantList build(const FeedContext &context)
{
    const QVariantMap snap = context.nativeSnapshot.value(QStringLiteral("downloads")).toMap();
    if (!snap.contains(QStringLiteral("jobs"))) {
        QVariantMap error = section(QStringLiteral("downloads.header"), 0, QStringLiteral("error"));
        error.insert(QStringLiteral("error"),
                     QStringLiteral("Downloads are unavailable for this profile."));
        return {error};
    }
    const QVariantList jobs = snap.value(QStringLiteral("jobs")).toList();
    const QVariantList abActive = snap.value(QStringLiteral("abActive")).toList();
    const QVariantList abDone = snap.value(QStringLiteral("abDone")).toList();
    const QVariantList remote = snap.value(QStringLiteral("remote")).toList();
    QVariantList sections;

    // live / attention classification (DownloadsPage.qml:158-168)
    int live = 0, attention = 0;
    for (const QVariant &v : jobs) {
        const QString s = v.toMap().value(QStringLiteral("state")).toString();
        if (isLiveState(s)) live++;
        else if (s == QLatin1String("failed")) attention++;
    }
    for (const QVariant &v : abActive) {
        const QString s = v.toMap().value(QStringLiteral("state")).toString();
        if (isLiveState(s)) live++;
        else if (s == QLatin1String("failed")) attention++;
    }

    // totals (DownloadsPage.qml:176-188)
    const QVariantMap base = snap.value(QStringLiteral("totals")).toMap();
    double audioBytes = 0;
    for (const QVariant &v : abDone) audioBytes += v.toMap().value(QStringLiteral("bytes"), 0).toDouble();
    QVariantMap totals{{QStringLiteral("items"), base.value(QStringLiteral("items"), 0).toInt() + abDone.size()},
                       {QStringLiteral("bytes"), base.value(QStringLiteral("bytes"), 0).toDouble() + audioBytes},
                       {QStringLiteral("tankoban"), base.value(QStringLiteral("tankoban"), 0).toInt()},
                       {QStringLiteral("biblio"), base.value(QStringLiteral("biblio"), 0).toInt()},
                       {QStringLiteral("theatre"), base.value(QStringLiteral("theatre"), 0).toInt()},
                       {QStringLiteral("audiobook"), abDone.size()},
                       {QStringLiteral("active"), live},
                       {QStringLiteral("attention"), attention}};
    sections.append(section(QStringLiteral("downloads.header"), 0, QStringLiteral("ready"),
                            {{QStringLiteral("totals"), totals}}));

    QVariantList abActivePublic;
    for (const QVariant &v : abActive) {
        const QVariantMap a = v.toMap();
        QVariantMap r;
        for (const char *k : {"id", "state", "title", "author", "error"}) {
            const QString key = QString::fromLatin1(k);
            if (a.contains(key)) r.insert(key, a.value(key));
        }
        r.insert(QStringLiteral("received"), a.value(QStringLiteral("received"), 0));
        r.insert(QStringLiteral("total"), a.value(QStringLiteral("total"), 0));
        abActivePublic.append(r);
    }
    sections.append(section(QStringLiteral("downloads.now"), 1, QStringLiteral("ready"),
                            {{QStringLiteral("liveCount"), live},
                             {QStringLiteral("attentionCount"), attention},
                             {QStringLiteral("groups"), groupJobs(jobs)},
                             {QStringLiteral("audiobookActive"), abActivePublic}}));

    QVariantList actRows;                                       // BackgroundActivitySection.qml:11
    for (const QVariant &v : snap.value(QStringLiteral("activities")).toList()) {
        const QVariantMap r = v.toMap();
        QVariantMap row;
        for (const char *k : {"id", "title", "stage", "progress", "paused", "canPause"}) {
            const QString key = QString::fromLatin1(k);
            if (r.contains(key)) row.insert(key, r.value(key));
        }
        actRows.append(row);
    }
    sections.append(section(QStringLiteral("downloads.activity"), 2, QStringLiteral("ready"),
                            {{QStringLiteral("rows"), actRows}}));

    QVariantList remoteRows;
    for (const QVariant &v : remote) remoteRows.append(v.toMap());
    sections.append(section(QStringLiteral("downloads.remote"), 3, QStringLiteral("ready"),
                            {{QStringLiteral("items"), remoteRows}}));

    // world shelves (DownloadsPage.qml:147-151, 189-192) + the open ledger (:194-197)
    const QVariantMap lanes = snap.value(QStringLiteral("lanes")).toMap();
    const QString ledgerWorld = context.params.value(QStringLiteral("ledgerWorld")).toString();
    const QString ledgerKey = context.params.value(QStringLiteral("ledgerKey")).toString();
    const QVariantList ledgerItems = snap.value(QStringLiteral("ledgerItems")).toList();
    struct WorldDef { const char *key; const char *title; const char *unit; };
    const WorldDef worldDefs[] = {{"tankoban", "Tankoban", "chapters, volumes & issues"},
                                  {"biblio", "Biblio", "books"},
                                  {"theatre", "Theatre", "files"}};
    int shelfIndex = 4;
    for (const WorldDef &wd : worldDefs) {
        QVariantMap data{{QStringLiteral("world"), QString::fromLatin1(wd.key)},
                        {QStringLiteral("title"), QString::fromLatin1(wd.title)},
                        {QStringLiteral("unit"), QString::fromLatin1(wd.unit)},
                        {QStringLiteral("series"), lanes.value(QString::fromLatin1(wd.key)).toList()}};
        if (ledgerWorld == QLatin1String(wd.key) && !ledgerKey.isEmpty()) {
            QVariantMap ledger;
            const QVariantList seasons = ledgerSeasons(ledgerItems, jobs);
            if (!seasons.isEmpty()) {
                ledger.insert(QStringLiteral("seasons"), seasons);
            } else {
                QVariantList flat;
                for (const QVariant &v : ledgerItems) flat.append(publicLedgerRow(v.toMap()));
                ledger.insert(QStringLiteral("flat"), flat);
            }
            ledger.insert(QStringLiteral("key"), ledgerKey);
            data.insert(QStringLiteral("ledger"), ledger);
        }
        sections.append(section(QStringLiteral("downloads.shelf.") + QString::fromLatin1(wd.key),
                                shelfIndex++, QStringLiteral("ready"), data));
    }

    // audiobooks shelf (DownloadsPage.qml:1408-1432, 1449-1470)
    QVariantList abDonePublic;
    for (const QVariant &v : abDone) {
        const QVariantMap a = v.toMap();
        QVariantMap r;
        for (const char *k : {"id", "title", "author", "bookId"}) {
            const QString key = QString::fromLatin1(k);
            if (a.contains(key)) r.insert(key, a.value(key));
        }
        for (const char *k : {"bytes", "addedAt"}) {
            const QString key = QString::fromLatin1(k);
            if (a.contains(key)) r.insert(key, a.value(key));
        }
        r.insert(QStringLiteral("missing"), a.value(QStringLiteral("missing"), false));
        r.insert(QStringLiteral("bookReady"),
                 !a.value(QStringLiteral("bookPath")).toString().isEmpty());
        abDonePublic.append(r);
    }
    sections.append(section(QStringLiteral("downloads.audiobooks"), 7, QStringLiteral("ready"),
                            {{QStringLiteral("done"), abDonePublic},
                             // QML :1412 visible rule: done>0 OR nothing active
                             {QStringLiteral("visibleWhenIdle"),
                              abDone.size() > 0 || abActive.isEmpty()}}));
    return sections;
}

// ---- registration ----
QMetaObject::Connection watchDownloads(QObject *object, QObject *receiver,
                                       std::function<void()> changed)
{
    auto *ld = qobject_cast<LocalDownloads *>(object);
    if (!ld) return {};
    return QObject::connect(ld, &LocalDownloads::changed, receiver,
                            [changed = std::move(changed)] { changed(); });
}
QMetaObject::Connection watchAudiobooks(QObject *object, QObject *receiver,
                                        std::function<void()> changed)
{
    auto *ab = qobject_cast<AudiobookDownloader *>(object);
    if (!ab) return {};
    return QObject::connect(ab, &AudiobookDownloader::activeCountChanged, receiver,
                            [changed = std::move(changed)] { changed(); });
}
QMetaObject::Connection watchCool(QObject *object, QObject *receiver,
                                  std::function<void()> changed)
{
    // Only the paused() setter is bound (one connection per owner signal). The lift side
    // (QML :409-412 clears on progress/finish/fail/remove) is approximated safely: entries
    // expire by wall clock, and groupJobs ignores cooldowns for rows not mid-download.
    auto *md = qobject_cast<MangaDownloader *>(object);
    if (!md) return {};
    return QObject::connect(
        md, &MangaDownloader::paused, receiver,
        [changed = std::move(changed)](const QString &chapterId, int resumeInMs) {
            g_coolResumeAt.insert(chapterId, QDateTime::currentMSecsSinceEpoch() + resumeInMs);
            changed();
        });
}
QMetaObject::Connection watchActivity(QObject *object, QObject *receiver,
                                      std::function<void()> changed)
{
    auto *reg = qobject_cast<work::BackgroundActivityRegistry *>(object);
    if (!reg) return {};
    return QObject::connect(reg, &work::BackgroundActivityRegistry::activitiesChanged,
                            receiver, [changed = std::move(changed)] { changed(); });
}

const bool feedRegistered = [] {
    FeedRegistry::Entry entry;
    entry.name = QStringLiteral("page.downloads");
    entry.valid = valid;
    entry.initial = initial;
    entry.build = build;
    entry.capture = capture;
    entry.ownerSignals.append({QStringLiteral("LocalDownloads"), watchDownloads});
    entry.ownerSignals.append({QStringLiteral("Audiobooks"), watchAudiobooks});
    entry.ownerSignals.append({QStringLiteral("Downloads"), watchCool});
    entry.ownerSignals.append({QStringLiteral("BackgroundActivity"), watchActivity});
    return FeedRegistry::add(std::move(entry));
}();

// ---- actions ----
QVariantMap ok() { return {{QStringLiteral("ok"), true}}; }
QVariantMap fail(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

LocalDownloads *ldOwner(ColosseumWebBridge &bridge, const ActionRegistry::Completion &done)
{
    auto *ld = downloads(bridge);
    if (!ld) done(fail(QStringLiteral("Downloads are unavailable for this profile.")));
    return ld;
}

// find the CURRENT job row by id (the QML always acts on current state, never stale rows)
QVariantMap findJob(ColosseumWebBridge &bridge, const QString &world, const QString &id)
{
    auto *ld = downloads(bridge);
    if (!ld) return {};
    for (const QVariant &v : ld->activeJobs()) {
        const QVariantMap j = v.toMap();
        if (j.value(QStringLiteral("id")).toString() == id
            && (world.isEmpty() || j.value(QStringLiteral("world")).toString() == world))
            return j;
    }
    return {};
}

double videoPosition(ColosseumWebBridge &bridge, const QString &id)   // Main.qml:2021-2024
{
    auto *pg = progress(bridge);
    if (!pg) return 0;
    const QVariantMap prog = pg->get(QStringLiteral("video"), id);
    const QVariantMap resume = prog.value(QStringLiteral("resume")).toMap();
    const double pos = resume.value(QStringLiteral("position"), 0).toDouble();
    return pos > 0 ? pos : 0;
}

void handleSimple(ColosseumWebBridge &bridge, const QVariantMap &payload,
                  const ActionRegistry::Completion &done,
                  void (LocalDownloads::*method)(const QString &, const QString &))
{
    auto *ld = ldOwner(bridge, done);
    if (!ld) return;
    (ld->*method)(payload.value(QStringLiteral("world")).toString(),
                  payload.value(QStringLiteral("id")).toString());
    done(ok());
}

const bool pauseRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.pause"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("world")) && p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            handleSimple(b, p, done, &LocalDownloads::pause); }});   // DownloadsPage.qml:714
}();
const bool resumeRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.resume"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("world")) && p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            handleSimple(b, p, done, &LocalDownloads::resume); }});  // :716
}();
const bool retryRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.retry"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("world")) && p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            handleSimple(b, p, done, &LocalDownloads::retry); }});   // :684
}();
const bool cancelRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.cancel"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("world")) && p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            handleSimple(b, p, done, &LocalDownloads::cancel); }});  // :949
}();
const bool dismissRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.dismissFailure"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("world")) && p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            handleSimple(b, p, done, &LocalDownloads::dismissFailure); }});   // :752
}();

// groupToggle (DownloadsPage.qml:706-717): pause every pausable row in reverse order,
// or resume every resumable row in order.
const bool groupToggleRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.groupToggle"),
        [](const QVariantMap &p) {
            return p.contains(QStringLiteral("world")) && p.contains(QStringLiteral("groupKey"))
                && p.contains(QStringLiteral("pause")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            auto *ld = ldOwner(b, done);
            if (!ld) return;
            const QString world = p.value(QStringLiteral("world")).toString();
            const QString groupKey = p.value(QStringLiteral("groupKey")).toString();
            const bool pausing = p.value(QStringLiteral("pause")).toBool();
            QVariantList rows;
            for (const QVariant &v : ld->activeJobs()) {
                const QVariantMap j = v.toMap();
                if (j.value(QStringLiteral("world")).toString() != world) continue;
                const QString key = j.value(QStringLiteral("groupKey"), j.value(QStringLiteral("id"))).toString();
                if (key == groupKey) rows.append(j);
            }
            if (!pausing) {
                // resume walks forward in id order; pause walks the list backwards (:712)
            } else {
                std::reverse(rows.begin(), rows.end());
            }
            for (const QVariant &v : rows) {
                const QVariantMap j = v.toMap();
                if (pausing && j.value(QStringLiteral("canPause")).toBool() == true)
                    ld->pause(world, j.value(QStringLiteral("id")).toString());
                else if (!pausing && j.value(QStringLiteral("canResume")).toBool() == true)
                    ld->resume(world, j.value(QStringLiteral("id")).toString());
            }
            done(ok()); }});   // NOTE reversed: resume in order, pause in reverse — see comment
}();

// cancelGroup (DownloadsPage.qml:764-780): re-resolve the group's CURRENT rows at commit.
const bool cancelGroupRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.cancelGroup"),
        [](const QVariantMap &p) {
            return p.contains(QStringLiteral("world")) && p.contains(QStringLiteral("groupKey")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            auto *ld = ldOwner(b, done);
            if (!ld) return;
            const QString world = p.value(QStringLiteral("world")).toString();
            const QString groupKey = p.value(QStringLiteral("groupKey")).toString();
            QVariantList rows;
            for (const QVariant &v : ld->activeJobs()) {
                const QVariantMap j = v.toMap();
                if (j.value(QStringLiteral("world")).toString() != world) continue;
                const QString key = j.value(QStringLiteral("groupKey"), j.value(QStringLiteral("id"))).toString();
                if (key == groupKey) rows.append(j);
            }
            for (int i = rows.size() - 1; i >= 0; --i) {            // reverse (:775)
                const QVariantMap j = rows[i].toMap();
                if (j.value(QStringLiteral("canCancel")).toBool() == true)
                    ld->cancel(world, j.value(QStringLiteral("id")).toString());
            }
            done(ok()); }});
}();

// remove (DownloadsPage.qml:1804 + Main finishMutation fallback)
const bool removeRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.remove"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("world")) && p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            auto *ld = ldOwner(b, done);
            if (!ld) return;
            const QVariantMap result = ld->remove(p.value(QStringLiteral("world")).toString(),
                                                  p.value(QStringLiteral("id")).toString());
            if (result.value(QStringLiteral("success"), true).toBool() == false)
                done(fail(result.value(QStringLiteral("message")).toString().isEmpty()
                          ? QStringLiteral("The local copy could not be deleted.")
                          : result.value(QStringLiteral("message")).toString()));
            else done(ok()); }});
}();

// redownload (Main.qml:4324-4327): remote row → LocalDownloads.redownload(item)
const bool redownloadRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.redownload"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("world")) && p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            auto *ld = ldOwner(b, done);
            if (!ld) return;
            const QString world = p.value(QStringLiteral("world")).toString();
            const QString id = p.value(QStringLiteral("id")).toString();
            for (const QVariant &v : ld->availableElsewhere()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("world")).toString() == world
                    && m.value(QStringLiteral("id")).toString() == id) {
                    if (!m.value(QStringLiteral("canRedownload"), false).toBool())
                        return done(fail(QStringLiteral("This item needs its source again before it can download here.")));
                    const QVariantMap result = ld->redownload(m);
                    if (result.value(QStringLiteral("success"), true).toBool() == false)
                        done(fail(result.value(QStringLiteral("message")).toString().isEmpty()
                                  ? QStringLiteral("This file could not be queued again.")
                                  : result.value(QStringLiteral("message")).toString()));
                    else done(ok());
                    return;
                }
            }
            done(fail(QStringLiteral("That download is no longer offered by the account."))); }});
}();

// playArriving (Main.qml:2056-2081): stream the same resolved url the job pulls;
// disk-first .part once >8MB landed.
const bool playArrivingRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.playArriving"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("world")) && p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            auto *ss = sessions(b);
            if (!ss) return done(fail(QStringLiteral("Playback is unavailable right now.")));
            const QVariantMap job = findJob(b, p.value(QStringLiteral("world")).toString(),
                                            p.value(QStringLiteral("id")).toString());
            if (job.isEmpty())
                return done(fail(QStringLiteral("That download is no longer running.")));
            const QString url = job.value(QStringLiteral("url")).toString();
            if (url.isEmpty())
                return done(fail(QStringLiteral("This download is not playing yet — it is still resolving.")));
            const QString id = job.value(QStringLiteral("id")).toString();
            const double received = job.value(QStringLiteral("received"), 0).toDouble();
            const QString part = received > 8 * 1024 * 1024
                ? job.value(QStringLiteral("partPath")).toString() : QString();
            QVariantMap target{{QStringLiteral("showKey"), seriesRootId(id)},
                               {QStringLiteral("streamUrl"), url},
                               {QStringLiteral("partPath"), part},
                               {QStringLiteral("id"), id},
                               {QStringLiteral("title"), job.value(QStringLiteral("title"), QStringLiteral("Video"))},
                               {QStringLiteral("headers"), job.value(QStringLiteral("headers"))},
                               {QStringLiteral("art"), job.value(QStringLiteral("art"))},
                               {QStringLiteral("kind"), job.value(QStringLiteral("kind"))},
                               {QStringLiteral("position"), videoPosition(b, id)}};
            ss->openOrSwitch(QVariantMap{
                {QStringLiteral("appType"), QStringLiteral("theatre")},
                {QStringLiteral("contentKind"), QStringLiteral("movie")},
                {QStringLiteral("title"), job.value(QStringLiteral("title"), QStringLiteral("Video")).toString()},
                {QStringLiteral("target"), target}});
            done(ok()); }});
}();

// openItem (Main.qml routeDownloadItem:2019-2054): ledger row → its destination.
const bool openItemRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.openItem"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("world")) && p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            auto *ld = ldOwner(b, done);
            if (!ld) return;
            const QString world = p.value(QStringLiteral("world")).toString();
            const QString id = p.value(QStringLiteral("id")).toString();
            QVariantMap row;
            // search the open ledger worlds' items by identity
            for (const char *wname : {"tankoban", "biblio", "theatre"}) {
                for (const QVariant &sr : ld->series(QString::fromLatin1(wname)).toList()) {
                    for (const QVariant &iv : ld->items(QString::fromLatin1(wname),
                                                        sr.toMap().value(QStringLiteral("key")).toString()).toList()) {
                        if (iv.toMap().value(QStringLiteral("id")).toString() == id) { row = iv.toMap(); break; }
                    }
                    if (!row.isEmpty()) break;
                }
                if (!row.isEmpty()) break;
            }
            if (row.isEmpty())
                return done(fail(QStringLiteral("That item is no longer on this device.")));
            if (world == QLatin1String("theatre")) {
                auto *ss = sessions(b);
                if (!ss) return done(fail(QStringLiteral("Playback is unavailable right now.")));
                QVariantMap target{                     // Main.qml openLocalVideoSession:2582
                    {QStringLiteral("showKey"), seriesRootId(id)},
                    {QStringLiteral("localPath"), row.value(QStringLiteral("path"))},
                    {QStringLiteral("id"), id},
                    {QStringLiteral("title"), row.value(QStringLiteral("title"))},
                    {QStringLiteral("art"), row.value(QStringLiteral("art"))},
                    {QStringLiteral("kind"), row.value(QStringLiteral("kind"))},
                    {QStringLiteral("position"), videoPosition(b, id)}};
                QVariantMap desc{
                    {QStringLiteral("appType"), QStringLiteral("theatre")},
                    {QStringLiteral("contentKind"), QStringLiteral("movie")},
                    {QStringLiteral("title"), row.value(QStringLiteral("title"), QStringLiteral("Video"))},
                    {QStringLiteral("target"), target}};
                ss->openOrSwitch(desc);
                return done(ok());
            }
            if (world == QLatin1String("biblio")) {
                auto *ss = sessions(b);
                if (!ss) return done(fail(QStringLiteral("The reader is unavailable right now.")));
                QVariantMap book{
                    {QStringLiteral("id"), row.value(QStringLiteral("id"), row.value(QStringLiteral("path")))},
                    {QStringLiteral("title"), row.value(QStringLiteral("title"))},
                    {QStringLiteral("author"), row.value(QStringLiteral("author"))}};
                QVariantMap target{                     // Main.qml openBookSession:2606
                    {QStringLiteral("path"), row.value(QStringLiteral("path"))},
                    {QStringLiteral("book"), book},
                    {QStringLiteral("id"), row.value(QStringLiteral("path"))}};
                QVariantMap desc{
                    {QStringLiteral("appType"), QStringLiteral("biblio")},
                    {QStringLiteral("contentKind"), QStringLiteral("book")},
                    {QStringLiteral("title"), row.value(QStringLiteral("title"), QStringLiteral("Book"))},
                    {QStringLiteral("target"), target}};
                ss->openOrSwitch(desc);
                return done(ok());
            }
            // tankoban: route through the shared open dispatcher by seriesId provenance
            // (Main.qml:2042-2053 ≈ dispatcher Main.qml:4677-4687); pack children answer
            // with the bridge's comic pack route for web to follow (§12.4).
            const QString seriesId = row.value(QStringLiteral("seriesId")).toString();
            const QString packRole = row.value(QStringLiteral("packRole")).toString();
            if (!packRole.isEmpty()) {
                QVariantMap answer = ok();
                answer.insert(QStringLiteral("result"), QVariantMap{{QStringLiteral("route"),
                    b.comicPackRoute(seriesId,
                                     row.value(QStringLiteral("seriesTitle")).toString(),
                                     row.value(QStringLiteral("id")).toString())}});
                return done(answer);
            }
            QVariantMap ref{{QStringLiteral("id"), seriesId}};
            if (seriesId.startsWith(QLatin1String("gcd:")))
                ref.insert(QStringLiteral("gcdId"), seriesId.mid(4));
            if (!row.value(QStringLiteral("id")).toString().isEmpty())
                ref.insert(QStringLiteral("resumeChapterId"), row.value(QStringLiteral("id")));
            b.delegateAction(QStringLiteral("open"),
                {{QStringLiteral("intent"), QStringLiteral("details")},
                 {QStringLiteral("item"), QVariantMap{
                     {QStringLiteral("world"), QStringLiteral("Tankoban")},
                     {QStringLiteral("kind"), row.value(QStringLiteral("kind"), QStringLiteral("comic"))},
                     {QStringLiteral("title"), row.value(QStringLiteral("seriesTitle"), row.value(QStringLiteral("title")))},
                     {QStringLiteral("ref"), ref}}}},
                [done](const QVariantMap &answer) mutable { if (done) done(answer); });
        }});
}();

// openAudiobook (Main.qml routeDownloadedAudiobook:2089-2100)
const bool openAudiobookRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.openAudiobook"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            auto *ab = audiobooks(b);
            auto *ss = sessions(b);
            if (!ab || !ss) return done(fail(QStringLiteral("The reader is unavailable right now.")));
            const QString pairKey = p.value(QStringLiteral("id")).toString();
            for (const QVariant &v : ab->downloadedAudiobooks()) {
                const QVariantMap a = v.toMap();
                if (a.value(QStringLiteral("id")).toString() != pairKey) continue;
                const QString bookPath = a.value(QStringLiteral("bookPath")).toString();
                if (bookPath.isEmpty())
                    return done(fail(QStringLiteral("The paired book is not available locally.")));
                ss->openOrSwitch(QVariantMap{
                    {QStringLiteral("appType"), QStringLiteral("biblio")},
                    {QStringLiteral("contentKind"), QStringLiteral("book")},
                    {QStringLiteral("title"), a.value(QStringLiteral("title"), QStringLiteral("Book"))},
                    {QStringLiteral("target"), QVariantMap{
                        {QStringLiteral("path"), bookPath},
                        {QStringLiteral("book"), QVariantMap{
                            {QStringLiteral("id"), a.value(QStringLiteral("bookId"), bookPath)},
                            {QStringLiteral("title"), a.value(QStringLiteral("title"))},
                            {QStringLiteral("author"), a.value(QStringLiteral("author"))},
                            {QStringLiteral("pairKey"), pairKey},
                            {QStringLiteral("openAudio"), true}}},
                        {QStringLiteral("id"), bookPath}}}});
                return done(ok());
            }
            done(fail(QStringLiteral("That audiobook is no longer on this device."))); }});
}();

const bool cancelAudiobookRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.cancelAudiobook"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            auto *ab = audiobooks(b);
            if (!ab) return done(fail(QStringLiteral("Audiobooks are unavailable for this profile.")));
            ab->cancelDownload(p.value(QStringLiteral("id")).toString());
            done(ok()); }});   // DownloadsPage.qml:1039
}();
const bool dismissAudiobookRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.dismissAudiobookFailure"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            auto *ab = audiobooks(b);
            if (!ab) return done(fail(QStringLiteral("Audiobooks are unavailable for this profile.")));
            ab->dismissFailure(p.value(QStringLiteral("id")).toString());
            done(ok()); }});   // DownloadsPage.qml:1024
}();
const bool deleteAudiobookRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.deleteAudiobook"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            auto *ab = audiobooks(b);
            if (!ab) return done(fail(QStringLiteral("Audiobooks are unavailable for this profile.")));
            const QVariantMap result = ab->deleteAudiobook(p.value(QStringLiteral("id")).toString());
            if (result.value(QStringLiteral("success"), true).toBool() == false)
                done(fail(result.value(QStringLiteral("message")).toString().isEmpty()
                          ? QStringLiteral("The audiobook could not be deleted.")
                          : result.value(QStringLiteral("message")).toString()));
            else done(ok()); }});   // DownloadsPage.qml:1556
}();

const bool bgPauseRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.background.pause"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            auto *reg = activity(b);
            if (!reg) return done(fail(QStringLiteral("Background activity is unavailable.")));
            reg->requestPause(p.value(QStringLiteral("id")).toString());
            done(ok()); }});   // BackgroundActivitySection.qml:69
}();
const bool bgResumeRegistered = [] {
    return ActionRegistry::add({QStringLiteral("page.downloads.background.resume"),
        [](const QVariantMap &p) { return p.contains(QStringLiteral("id")); },
        [](ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion done) {
            auto *reg = activity(b);
            if (!reg) return done(fail(QStringLiteral("Background activity is unavailable.")));
            reg->requestResume(p.value(QStringLiteral("id")).toString());
            done(ok()); }});   // BackgroundActivitySection.qml:68
}();

} // namespace
