from __future__ import annotations

from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def write(rel: str, text: str) -> None:
    with (ROOT / rel).open("w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def replace_once(rel: str, old: str, new: str) -> None:
    text = read(rel)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{rel}: expected exactly one replacement, found {count}\n--- needle ---\n{old}")
    write(rel, text.replace(old, new, 1))


# Universe payload: replace the legacy WeebCentral shortcut with an edition profile
# that reuses MAL 13's catalogue while keeping storage/discovery identity isolated.
replace_once(
    "assets/universes/one-piece.json",
    '        { "id": "01J76XYAQSGEJPXCSCVPQ3MHZM", "provider": "weebcentral", "title": "One Piece (Color)" },',
    '        { "id": "one-piece-color", "provider": "tankoban", "title": "One Piece (Color)", "malId": "13", "seriesId": "mal:13:color", "sourceSearchTitle": "One Piece Colored", "sourceSearchAliases": ["One Piece Digital Colored Comics"], "sourceRequiredMarkers": ["colored", "full color", "full colour"] },',
)

# Native value type: append optional edition-discovery fields so all existing
# five-field aggregate initializers keep exactly the same field mapping.
replace_once(
    "native/engine/MangaTankobanTypes.h",
    "    QStringList aliases;\n    QList<VolumeRecord> volumes;\n",
    "    QStringList aliases;\n    QList<VolumeRecord> volumes;\n"
    "    QString discoveryTitle;\n"
    "    QStringList discoveryAliases;\n"
    "    QStringList requiredTitleMarkers;\n",
)

# Descriptor -> SeriesSnapshot plumbing.
replace_once(
    "native/engine/MangaTankobanLogic.cpp",
    '    for (const QVariant& a : descriptor.value(QStringLiteral("aliases")).toList())\n'
    '        snap.aliases << a.toString();\n\n'
    '    // Build one canonical record per volume row — none is ever dropped.\n',
    '    for (const QVariant& a : descriptor.value(QStringLiteral("aliases")).toList())\n'
    '        snap.aliases << a.toString();\n'
    '    snap.discoveryTitle = descriptor.value(QStringLiteral("discoveryTitle")).toString();\n'
    '    for (const QVariant& a : descriptor.value(QStringLiteral("discoveryAliases")).toList())\n'
    '        snap.discoveryAliases << a.toString();\n'
    '    for (const QVariant& marker : descriptor.value(QStringLiteral("requiredTitleMarkers")).toList())\n'
    '        snap.requiredTitleMarkers << marker.toString();\n\n'
    '    // Build one canonical record per volume row — none is ever dropped.\n',
)

# Query planner: an explicit edition profile replaces, rather than broadens, the
# ordinary title/alias family. This is what prevents Color from falling back to
# generic "One Piece" Nyaa searches.
replace_once(
    "native/torrent/MangaTorrentDiscovery.cpp",
    '    // Canonical title first, verbatim through the existing query family so the\n'
    '    // no-alias path stays byte-identical to pre-Arc-18 behavior.\n'
    '    const QStringList canonical = queryVariants(series.title, volumeNumber);\n'
    '    for (const QString& q : canonical)\n'
    '        add(q);\n\n'
    '    // Then aliases — discovery inputs now, not just validation needles.\n'
    '    for (const QString& alias : series.aliases) {\n'
    '        if (alias.trimmed().isEmpty())\n'
    '            continue;\n'
    '        const QStringList aliasVariants = queryVariants(alias, volumeNumber);\n'
    '        for (const QString& q : aliasVariants)\n'
    '            add(q);\n'
    '        if (out.size() >= cap)\n'
    '            break;\n'
    '    }\n',
    '    const bool hasDiscoveryProfile = !series.discoveryTitle.trimmed().isEmpty()\n'
    '        || !series.discoveryAliases.isEmpty();\n'
    '    const QString discoveryTitle = hasDiscoveryProfile ? series.discoveryTitle : series.title;\n'
    '    const QStringList discoveryAliases = hasDiscoveryProfile ? series.discoveryAliases : series.aliases;\n\n'
    '    // Canonical discovery title first. With no edition profile this remains\n'
    '    // byte-identical to the pre-profile title family.\n'
    '    const QStringList canonical = queryVariants(discoveryTitle, volumeNumber);\n'
    '    for (const QString& q : canonical)\n'
    '        add(q);\n\n'
    '    // Then only the aliases belonging to the selected discovery profile.\n'
    '    for (const QString& alias : discoveryAliases) {\n'
    '        if (alias.trimmed().isEmpty())\n'
    '            continue;\n'
    '        const QStringList aliasVariants = queryVariants(alias, volumeNumber);\n'
    '        for (const QString& q : aliasVariants)\n'
    '            add(q);\n'
    '        if (out.size() >= cap)\n'
    '            break;\n'
    '    }\n',
)

# Nyaa edition marker gate. Strong series matching intentionally stays based on
# the base catalogue title/aliases; the marker gate is an additional edition filter.
replace_once(
    "native/torrent/MangaNyaaSource.cpp",
    'bool strongSeriesMatch(const QString& title, const SeriesSnapshot& series)\n'
    '{\n'
    '    const QString hay = foldWords(title);\n'
    '    QStringList needles;\n'
    '    if (!series.title.isEmpty())\n'
    '        needles << series.title;\n'
    '    needles << series.aliases;\n'
    '    for (const QString& n : needles) {\n'
    '        const QString fn = foldWords(n);\n'
    '        if (!fn.isEmpty() && hay.contains(fn))\n'
    '            return true;\n'
    '    }\n'
    '    return false;\n'
    '}\n\n'
    '// Does the [lo,hi] coverage include the exact target volume? Rebuilt as a\n',
    'bool strongSeriesMatch(const QString& title, const SeriesSnapshot& series)\n'
    '{\n'
    '    const QString hay = foldWords(title);\n'
    '    QStringList needles;\n'
    '    if (!series.title.isEmpty())\n'
    '        needles << series.title;\n'
    '    needles << series.aliases;\n'
    '    for (const QString& n : needles) {\n'
    '        const QString fn = foldWords(n);\n'
    '        if (!fn.isEmpty() && hay.contains(fn))\n'
    '            return true;\n'
    '    }\n'
    '    return false;\n'
    '}\n\n'
    'bool matchesRequiredTitleMarker(const QString& title, const QStringList& requiredMarkers)\n'
    '{\n'
    '    if (requiredMarkers.isEmpty())\n'
    '        return true;\n'
    '    const QString hay = foldWords(title);\n'
    '    for (const QString& marker : requiredMarkers) {\n'
    '        const QString needle = foldWords(marker);\n'
    '        if (!needle.isEmpty() && hay.contains(needle))\n'
    '            return true;\n'
    '    }\n'
    '    return false;\n'
    '}\n\n'
    '// Does the [lo,hi] coverage include the exact target volume? Rebuilt as a\n',
)
replace_once(
    "native/torrent/MangaNyaaSource.cpp",
    '        if (!strongSeriesMatch(c.title, series))\n'
    '            continue;                       // weak series-title / alias match\n'
    '        // Series mode has no volume to target — every strongly-matched, kept\n',
    '        if (!strongSeriesMatch(c.title, series))\n'
    '            continue;                       // weak series-title / alias match\n'
    '        if (!matchesRequiredTitleMarker(c.title, series.requiredTitleMarkers))\n'
    '            continue;                       // wrong edition (e.g. B&W result for Color)\n'
    '        // Series mode has no volume to target — every strongly-matched, kept\n',
)

# MangaSeries edition-profile seams.
replace_once(
    "qml/MangaSeries.qml",
    '    property string malId: ""    // Slice C: Discover card\'s MAL id, when the series was opened from one\n',
    '    property string malId: ""    // Slice C: Discover card\'s MAL id, when the series was opened from one\n'
    '    property string seriesIdOverride: ""\n'
    '    property string sourceSearchTitle: ""\n'
    '    property var sourceSearchAliases: []\n'
    '    property var sourceRequiredMarkers: []\n',
)
replace_once(
    "qml/MangaSeries.qml",
    '        TankobanVolumes.prepareSeries({\n'
    '            seriesId: page.seriesId, title: page.seriesTitle,\n'
    '            author: page.author, aliases: []\n'
    '        }, vols, [])\n',
    '        TankobanVolumes.prepareSeries({\n'
    '            seriesId: page.seriesId, title: page.seriesTitle,\n'
    '            author: page.author, aliases: [],\n'
    '            discoveryTitle: page.sourceSearchTitle,\n'
    '            discoveryAliases: page.sourceSearchAliases,\n'
    '            requiredTitleMarkers: page.sourceRequiredMarkers\n'
    '        }, vols, [])\n',
)
replace_once(
    "qml/MangaSeries.qml",
    '                page.seriesId = "mal:" + id\n',
    '                page.seriesId = page.seriesIdOverride.length ? page.seriesIdOverride : ("mal:" + id)\n',
)

# Main route/profile lifecycle. Profile state is always applied before seriesTitle,
# because seriesTitleChanged synchronously triggers MangaSeries.resolve().
replace_once(
    "qml/Main.qml",
    '    function openSeries(title, malId) {\n'
    '        seriesLayer.resumeSeriesId = ""\n'
    '        seriesLayer.resumeChapterId = ""\n'
    '        seriesLayer.resumeVolumeId = ""\n'
    '        seriesLayer.title = title\n'
    '        seriesLayer.malId = malId || ""\n'
    '        if (seriesLayer.active && seriesLayer.item) {\n'
    '            seriesLayer.item.openEntryKind = "manga"   // a reused item may still be in a volume read\n'
    '            seriesLayer.item.openChapterId = ""        // leave the reader, show the chapter list\n'
    '            seriesLayer.item.malId = malId || ""       // set BEFORE seriesTitle: that triggers re-resolve\n'
    '            seriesLayer.item.seriesTitle = title\n'
    '        } else seriesLayer.active = true\n'
    '    }\n'
    '    // open a manga series AND jump straight into the reader at a saved chapter (Continue resume).\n'
    '    function openSeriesAt(title, seriesId, chapterId) {\n'
    '        seriesLayer.resumeSeriesId = seriesId || ""\n'
    '        seriesLayer.resumeChapterId = chapterId || ""\n'
    '        seriesLayer.resumeVolumeId = ""\n'
    '        seriesLayer.title = title\n'
    '        if (seriesLayer.active && seriesLayer.item) {\n'
    '            seriesLayer.item.seriesTitle = title\n'
    '            if (seriesId) seriesLayer.item.seriesId = seriesId\n'
    '            seriesLayer.item.openEntryKind = "manga"   // a reused item may still be in a volume read\n'
    '            seriesLayer.item.openChapterId = chapterId || ""\n'
    '        } else seriesLayer.active = true\n'
    '    }\n',
    '    function clearSeriesEditionProfile() {\n'
    '        seriesLayer.malId = ""\n'
    '        seriesLayer.seriesIdOverride = ""\n'
    '        seriesLayer.sourceSearchTitle = ""\n'
    '        seriesLayer.sourceSearchAliases = []\n'
    '        seriesLayer.sourceRequiredMarkers = []\n'
    '        if (seriesLayer.active && seriesLayer.item) {\n'
    '            seriesLayer.item.malId = ""\n'
    '            seriesLayer.item.seriesIdOverride = ""\n'
    '            seriesLayer.item.sourceSearchTitle = ""\n'
    '            seriesLayer.item.sourceSearchAliases = []\n'
    '            seriesLayer.item.sourceRequiredMarkers = []\n'
    '        }\n'
    '    }\n'
    '    function applySeriesEditionProfile(profile, fallbackMalId) {\n'
    '        clearSeriesEditionProfile()\n'
    '        var p = profile || ({})\n'
    '        seriesLayer.malId = String(p.malId || fallbackMalId || "")\n'
    '        seriesLayer.seriesIdOverride = String(p.seriesId || "")\n'
    '        seriesLayer.sourceSearchTitle = String(p.sourceSearchTitle || "")\n'
    '        seriesLayer.sourceSearchAliases = p.sourceSearchAliases || []\n'
    '        seriesLayer.sourceRequiredMarkers = p.sourceRequiredMarkers || []\n'
    '        if (seriesLayer.active && seriesLayer.item) {\n'
    '            seriesLayer.item.malId = seriesLayer.malId\n'
    '            seriesLayer.item.seriesIdOverride = seriesLayer.seriesIdOverride\n'
    '            seriesLayer.item.sourceSearchTitle = seriesLayer.sourceSearchTitle\n'
    '            seriesLayer.item.sourceSearchAliases = seriesLayer.sourceSearchAliases\n'
    '            seriesLayer.item.sourceRequiredMarkers = seriesLayer.sourceRequiredMarkers\n'
    '        }\n'
    '    }\n'
    '    function restoreSeriesEditionProfile(seriesId) {\n'
    '        var sid = String(seriesId || "")\n'
    '        if (sid !== "mal:13:color") { clearSeriesEditionProfile(); return }\n'
    '        applySeriesEditionProfile({\n'
    '            malId: "13", seriesId: sid, sourceSearchTitle: "One Piece Colored",\n'
    '            sourceSearchAliases: ["One Piece Digital Colored Comics"],\n'
    '            sourceRequiredMarkers: ["colored", "full color", "full colour"]\n'
    '        }, "13")\n'
    '    }\n'
    '    function openSeries(title, malId, profile) {\n'
    '        seriesLayer.resumeSeriesId = ""\n'
    '        seriesLayer.resumeChapterId = ""\n'
    '        seriesLayer.resumeVolumeId = ""\n'
    '        applySeriesEditionProfile(profile, malId)\n'
    '        seriesLayer.title = title\n'
    '        if (seriesLayer.active && seriesLayer.item) {\n'
    '            seriesLayer.item.openEntryKind = "manga"   // a reused item may still be in a volume read\n'
    '            seriesLayer.item.openChapterId = ""        // leave the reader, show the chapter list\n'
    '            seriesLayer.item.seriesTitle = title\n'
    '        } else seriesLayer.active = true\n'
    '    }\n'
    '    // open a manga series AND jump straight into the reader at a saved chapter (Continue resume).\n'
    '    function openSeriesAt(title, seriesId, chapterId) {\n'
    '        seriesLayer.resumeSeriesId = seriesId || ""\n'
    '        seriesLayer.resumeChapterId = chapterId || ""\n'
    '        seriesLayer.resumeVolumeId = ""\n'
    '        restoreSeriesEditionProfile(seriesId)\n'
    '        seriesLayer.title = title\n'
    '        if (seriesLayer.active && seriesLayer.item) {\n'
    '            seriesLayer.item.seriesTitle = title\n'
    '            if (seriesId) seriesLayer.item.seriesId = seriesId\n'
    '            seriesLayer.item.openEntryKind = "manga"   // a reused item may still be in a volume read\n'
    '            seriesLayer.item.openChapterId = chapterId || ""\n'
    '        } else seriesLayer.active = true\n'
    '    }\n',
)
replace_once(
    "qml/Main.qml",
    '                seriesLayer.resumeVolumeId = savedComicId\n'
    '                seriesLayer.title = t.title\n',
    '                seriesLayer.resumeVolumeId = savedComicId\n'
    '                restoreSeriesEditionProfile(t.seriesId)\n'
    '                seriesLayer.title = t.title\n',
)
replace_once(
    "qml/Main.qml",
    '            seriesLayer.resumeVolumeId = ""\n'
    '            seriesLayer.title = t.title\n'
    '            if (seriesLayer.active && seriesLayer.item) {\n',
    '            seriesLayer.resumeVolumeId = ""\n'
    '            restoreSeriesEditionProfile(t.seriesId)\n'
    '            seriesLayer.title = t.title\n'
    '            if (seriesLayer.active && seriesLayer.item) {\n',
)
replace_once(
    "qml/Main.qml",
    '        property string malId: ""             // Slice C: Discover card\'s MAL id, threaded to the series page\n'
    '        property string resumeSeriesId: ""    // Continue resume: jump straight to this chapter…\n',
    '        property string malId: ""             // Slice C: Discover card\'s MAL id, threaded to the series page\n'
    '        property string seriesIdOverride: ""\n'
    '        property string sourceSearchTitle: ""\n'
    '        property var sourceSearchAliases: []\n'
    '        property var sourceRequiredMarkers: []\n'
    '        property string resumeSeriesId: ""    // Continue resume: jump straight to this chapter…\n',
)
replace_once(
    "qml/Main.qml",
    '            item.backdrop = wall\n'
    '            item.malId = seriesLayer.malId\n'
    '            item.seriesTitle = seriesLayer.title\n',
    '            item.backdrop = wall\n'
    '            item.malId = seriesLayer.malId\n'
    '            item.seriesIdOverride = seriesLayer.seriesIdOverride\n'
    '            item.sourceSearchTitle = seriesLayer.sourceSearchTitle\n'
    '            item.sourceSearchAliases = seriesLayer.sourceSearchAliases\n'
    '            item.sourceRequiredMarkers = seriesLayer.sourceRequiredMarkers\n'
    '            item.seriesTitle = seriesLayer.title\n',
)
replace_once(
    "qml/Main.qml",
    '            // manga → Tankoban. A weebcentral-sourced entry (One Piece digital-coloured) opens its\n'
    '            // own series by ID; an anilist entry opens by title, as before.\n'
    '            item.seriesRequested.connect(function(e) {\n'
    '                if (e && e.provider === "weebcentral" && e.id) win.openSeriesAt(e.title || "", e.id)\n'
    '                else win.openSeries((e && e.title) || e || "")\n'
    '            })\n',
    '            // manga → Tankoban. Edition-aware entries can carry a discovery/storage\n'
    '            // profile while reusing the same catalogue identity as the base manga.\n'
    '            item.seriesRequested.connect(function(e) {\n'
    '                if (e && e.provider === "tankoban") {\n'
    '                    win.openSeries(e.title || "", e.malId || "", {\n'
    '                        malId: e.malId || "", seriesId: e.seriesId || "",\n'
    '                        sourceSearchTitle: e.sourceSearchTitle || "",\n'
    '                        sourceSearchAliases: e.sourceSearchAliases || [],\n'
    '                        sourceRequiredMarkers: e.sourceRequiredMarkers || []\n'
    '                    })\n'
    '                } else win.openSeries((e && e.title) || e || "")\n'
    '            })\n',
)

# QML catalogue harness: allow a profile to be injected before seriesTitle, then
# pin the Color identity/profile behavior.
replace_once(
    "tests/manga_series_catalogue_harness.qml",
    '    function makePage(malId, seriesTitle) {\n'
    '        if (!pageComp) {\n'
    '            pageComp = Qt.createComponent("../qml/MangaSeries.qml")\n'
    '            if (pageComp.status === Component.Error)\n'
    '                throw new Error("MangaSeries component: " + pageComp.errorString())\n'
    '        }\n'
    '        var p = pageComp.createObject(harness, {\n'
    '            "width": 1320, "height": 860,\n'
    '            "malCatalogRef": malCatalog, "tankobanCatalogRef": tankCatalog,\n'
    '            "tankobanVolumesRef": volService\n'
    '        })\n'
    '        if (!p) throw new Error("MangaSeries createObject returned null")\n'
    '        // Mirror production\'s exact sequencing (Main.qml: malId set BEFORE seriesTitle —\n'
    '        // that is what triggers the one resolve() via onSeriesTitleChanged).\n'
    '        p.malId = malId || ""\n'
    '        p.seriesTitle = seriesTitle || ""\n'
    '        return p\n'
    '    }\n',
    '    function makePage(malId, seriesTitle, profile) {\n'
    '        if (!pageComp) {\n'
    '            pageComp = Qt.createComponent("../qml/MangaSeries.qml")\n'
    '            if (pageComp.status === Component.Error)\n'
    '                throw new Error("MangaSeries component: " + pageComp.errorString())\n'
    '        }\n'
    '        var p = pageComp.createObject(harness, {\n'
    '            "width": 1320, "height": 860,\n'
    '            "malCatalogRef": malCatalog, "tankobanCatalogRef": tankCatalog,\n'
    '            "tankobanVolumesRef": volService\n'
    '        })\n'
    '        if (!p) throw new Error("MangaSeries createObject returned null")\n'
    '        // Mirror production sequencing: the edition profile and malId must be\n'
    '        // present BEFORE seriesTitle triggers resolve().\n'
    '        var pr = profile || ({})\n'
    '        p.seriesIdOverride = pr.seriesIdOverride || ""\n'
    '        p.sourceSearchTitle = pr.sourceSearchTitle || ""\n'
    '        p.sourceSearchAliases = pr.sourceSearchAliases || []\n'
    '        p.sourceRequiredMarkers = pr.sourceRequiredMarkers || []\n'
    '        p.malId = malId || ""\n'
    '        p.seriesTitle = seriesTitle || ""\n'
    '        return p\n'
    '    }\n',
)
replace_once(
    "tests/manga_series_catalogue_harness.qml",
    '            ck(p2.hasShelf === true, "case2: exact-title resolve must still see the catalogue shelf")\n\n'
    '            // ── Case 3: ambiguous / unmatched title -> the honest shelf-less page ──\n',
    '            ck(p2.hasShelf === true, "case2: exact-title resolve must still see the catalogue shelf")\n\n'
    '            // ── Case 2c: One Piece Color reuses MAL 13 but owns a distinct durable id ──\n'
    '            var colorRow = {\n'
    '                "mal_id": 13, "title": "One Piece", "title_english": "One Piece",\n'
    '                "score": 9.2, "status": "Publishing", "year": 1997,\n'
    '                "images": { "jpg": { "large_image_url": "http://cover/13" } },\n'
    '                "synopsis": "Pirates.", "authors": [ { "name": "Eiichiro Oda" } ],\n'
    '                "genres": [ { "name": "Adventure" } ]\n'
    '            }\n'
    '            malCatalog.rows = ({ "1": monsterRow, "13": colorRow })\n'
    '            tankCatalog.infoMap = ({ "13": { "volumeCount": 113, "countBasis": "mal" } })\n'
    '            volService.volMap = ({ "mal:13:color": [] })\n'
    '            var pc = makePage("13", "One Piece (Color)", {\n'
    '                "seriesIdOverride": "mal:13:color",\n'
    '                "sourceSearchTitle": "One Piece Colored",\n'
    '                "sourceSearchAliases": ["One Piece Digital Colored Comics"],\n'
    '                "sourceRequiredMarkers": ["colored", "full color", "full colour"]\n'
    '            })\n'
    '            ck(pc.resolvedMalId === 13, "case2c: Color must resolve through MAL 13")\n'
    '            ck(pc.seriesId === "mal:13:color", "case2c: Color durable id must stay isolated")\n'
    '            ck(pc.hasShelf === true, "case2c: Color must reuse MAL 13\'s 113-volume shelf")\n'
    '            ck(pc.sourceSearchTitle === "One Piece Colored", "case2c: colored discovery title retained")\n'
    '            ck(pc.sourceSearchAliases.length === 1\n'
    '               && pc.sourceSearchAliases[0] === "One Piece Digital Colored Comics",\n'
    '               "case2c: colored discovery alias retained")\n'
    '            ck(pc.sourceRequiredMarkers.length === 3, "case2c: colored marker gate retained")\n\n'
    '            // ── Case 3: ambiguous / unmatched title -> the honest shelf-less page ──\n',
)

# Native harness: descriptor plumbing, edition-only query family, and B&W rejection.
replace_once(
    "tests/manga_tankoban_logic_harness.cpp",
    '    require(snap.seriesId == QStringLiteral("s1") && snap.title == QStringLiteral("Series"),\n'
    '            "series descriptor retained");\n\n'
    '    // ── Volume assembly: chapterStart/chapterEnd range fallback ───────────\n',
    '    require(snap.seriesId == QStringLiteral("s1") && snap.title == QStringLiteral("Series"),\n'
    '            "series descriptor retained");\n\n'
    '    const auto colorSnap = prepareSeries(\n'
    '        QVariantMap{{"seriesId", "mal:13:color"}, {"title", "One Piece"},\n'
    '                    {"discoveryTitle", "One Piece Colored"},\n'
    '                    {"discoveryAliases", QVariantList{QVariant(QStringLiteral("One Piece Digital Colored Comics"))}},\n'
    '                    {"requiredTitleMarkers", QVariantList{QVariant(QStringLiteral("colored")),\n'
    '                                                            QVariant(QStringLiteral("full color")),\n'
    '                                                            QVariant(QStringLiteral("full colour"))}}},\n'
    '        QVariantList{}, QVariantList{});\n'
    '    require(colorSnap.seriesId == QStringLiteral("mal:13:color"),\n'
    '            "edition descriptor keeps its durable series id");\n'
    '    require(colorSnap.discoveryTitle == QStringLiteral("One Piece Colored")\n'
    '                && colorSnap.discoveryAliases == QStringList{QStringLiteral("One Piece Digital Colored Comics")},\n'
    '            "edition descriptor carries discovery title + aliases");\n'
    '    require(colorSnap.requiredTitleMarkers.size() == 3,\n'
    '            "edition descriptor carries required title markers");\n\n'
    '    // ── Volume assembly: chapterStart/chapterEnd range fallback ───────────\n',
)
replace_once(
    "tests/manga_tankoban_logic_harness.cpp",
    '        require(seriesWide.contains(QStringLiteral("Grand Blue Dreaming"))\n'
    '                    && seriesWide.contains(QStringLiteral("Grand Blue")),\n'
    '                "series-wide family searches bare title and bare alias");\n'
    '    }\n\n'
    '    // ── Arc 18 M2: RSS link retention + provenance stamping seam ────────────\n',
    '        require(seriesWide.contains(QStringLiteral("Grand Blue Dreaming"))\n'
    '                    && seriesWide.contains(QStringLiteral("Grand Blue")),\n'
    '                "series-wide family searches bare title and bare alias");\n\n'
    '        SeriesSnapshot color;\n'
    '        color.seriesId = QStringLiteral("mal:13:color");\n'
    '        color.title = QStringLiteral("One Piece");\n'
    '        color.discoveryTitle = QStringLiteral("One Piece Colored");\n'
    '        color.discoveryAliases = QStringList{QStringLiteral("One Piece Digital Colored Comics")};\n'
    '        color.requiredTitleMarkers = QStringList{QStringLiteral("colored"),\n'
    '                                                 QStringLiteral("full color"),\n'
    '                                                 QStringLiteral("full colour")};\n'
    '        const QStringList colorFamily = MangaTorrentDiscovery::queryFamily(color, QStringLiteral("2"));\n'
    '        require(colorFamily.contains(QStringLiteral("One Piece Colored 2")),\n'
    '                "Color queries use the colored discovery title");\n'
    '        require(colorFamily.contains(QStringLiteral("One Piece Digital Colored Comics 2")),\n'
    '                "Color queries include the colored alias");\n'
    '        require(!colorFamily.contains(QStringLiteral("One Piece 2"))\n'
    '                    && !colorFamily.contains(QStringLiteral("One Piece")),\n'
    '                "Color queries never fall back to broad One Piece discovery");\n'
    '    }\n\n'
    '    // ── Arc 18 M2: RSS link retention + provenance stamping seam ────────────\n',
)
replace_once(
    "tests/manga_tankoban_logic_harness.cpp",
    '    // ── I1: an inclusive pack written as "v01-v12" (both bounds v-marked) ─────\n',
    '    // ── Edition marker gate: Color must reject ordinary B&W releases ────────\n'
    '    {\n'
    '        SeriesSnapshot color;\n'
    '        color.seriesId = QStringLiteral("mal:13:color");\n'
    '        color.title = QStringLiteral("One Piece");\n'
    '        color.requiredTitleMarkers = QStringList{QStringLiteral("colored"),\n'
    '                                                 QStringLiteral("full color"),\n'
    '                                                 QStringLiteral("full colour")};\n'
    '        const auto plainRows = MangaNyaaSource::parseRss(\n'
    '            rssItem("One Piece Volume 2 (Digital)", "someone",\n'
    '                    "1010101010101010101010101010101010101010"));\n'
    '        const auto colorRows = MangaNyaaSource::parseRss(\n'
    '            rssItem("One Piece Digital Colored Comics Volume 2 (Digital)", "someone",\n'
    '                    "2020202020202020202020202020202020202020"));\n'
    '        QList<MangaNyaaCandidate> candidates = plainRows;\n'
    '        candidates.append(colorRows);\n'
    '        const auto ranked = MangaNyaaSource::filterAndRank(color, "2", candidates, TrustTable{});\n'
    '        require(ranked.size() == 1, "Color marker gate rejects the ordinary B&W release");\n'
    '        require(ranked.front().title.contains(QStringLiteral("Colored")),\n'
    '                "Color marker gate keeps the colored release");\n'
    '    }\n\n'
    '    // ── I1: an inclusive pack written as "v01-v12" (both bounds v-marked) ─────\n',
)

# Universe API regression checks: arbitrary profile fields survive validate/load.
replace_once(
    "tests/universe_ext_api_test.mjs",
    '  eq(onePieceN, rawOnePieceN, \'one-piece entries survive through the reader seam\');\n'
    '  eq(dcauN, rawDcauN, \'dcau entries survive through the reader seam\');\n'
    '  eq(onePieceN, 54, \'one-piece = 54 entries\');\n'
    '  eq(dcauN, 31, \'dcau = 31 entries\');\n',
    '  eq(onePieceN, rawOnePieceN, \'one-piece entries survive through the reader seam\');\n'
    '  eq(dcauN, rawDcauN, \'dcau entries survive through the reader seam\');\n'
    '  eq(onePieceN, 54, \'one-piece = 54 entries\');\n'
    '  eq(dcauN, 31, \'dcau = 31 entries\');\n'
    '  const color = onePieceResult.sections.find(s => s.id === \'manga\').entries\n'
    '    .find(e => e.id === \'one-piece-color\');\n'
    '  eq(color.provider, \'tankoban\', \'One Piece Color uses Tankoban\');\n'
    '  eq(color.malId, \'13\', \'One Piece Color reuses MAL 13 catalogue identity\');\n'
    '  eq(color.seriesId, \'mal:13:color\', \'One Piece Color durable identity is isolated\');\n'
    '  eq(color.sourceSearchTitle, \'One Piece Colored\', \'One Piece Color discovery title survives\');\n'
    '  eq(JSON.stringify(color.sourceSearchAliases), JSON.stringify([\'One Piece Digital Colored Comics\']),\n'
    '     \'One Piece Color discovery alias survives\');\n'
    '  eq(JSON.stringify(color.sourceRequiredMarkers), JSON.stringify([\'colored\', \'full color\', \'full colour\']),\n'
    '     \'One Piece Color marker gate survives\');\n',
)

# Fail closed on malformed JSON and assert exactly the intended tracked surface changed.
json.loads(read("assets/universes/one-piece.json"))

expected = {
    "assets/universes/one-piece.json",
    "native/engine/MangaTankobanLogic.cpp",
    "native/engine/MangaTankobanTypes.h",
    "native/torrent/MangaNyaaSource.cpp",
    "native/torrent/MangaTorrentDiscovery.cpp",
    "qml/Main.qml",
    "qml/MangaSeries.qml",
    "tests/manga_series_catalogue_harness.qml",
    "tests/manga_tankoban_logic_harness.cpp",
    "tests/universe_ext_api_test.mjs",
}
print("OPCOLOR_PATCH_OK")
print("\n".join(sorted(expected)))
