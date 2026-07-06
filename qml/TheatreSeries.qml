// TheatreSeries - Theatre detail page for movies and series.
// Mirrors MangaSeries.qml house style: full-bleed banner, inline metadata, reveal gate,
// pitch-black base, and a slide-up SourcesSheet for Torrentio rows.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "TheatreApi.js" as TheatreApi

Item {
    id: page
    property Item backdrop
    property var itemData: ({})
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal playRequested(string infoHash, int fileIdx, string title, string backdropUrl, string subType, string subId, var streamCandidates, var playbackContext)

    property string title: ""
    property string mediaType: "movie"
    property string banner: ""
    property string cover: ""
    property string year: ""
    property string genresLine: ""
    property string rating: ""
    property string runtime: ""
    property string synopsis: ""
    property var videos: []
    property var seasons: []
    property int activeSeason: 0
    property var episodes: filterEpisodes(videos, activeSeason)
    property string episodeLayout: "list"
    property bool episodeJumpOpen: false
    property var seasonQueued: ({})   // season -> queued this visit
    property bool seasonMenuOpen: false
    property string episodeJumpDraft: ""
    property bool loading: true
    property string errorMsg: ""
    // When an anime meta pivots to Cinemeta (kitsu -> imdb id), the meta's own id is
    // the identity everything keys off (episode stream ids, progress, last-season).
    property string resolvedId: ""

    function currentId() {
        if (resolvedId.length) return resolvedId;
        return (itemData && itemData.id) ? itemData.id : "";
    }
    function episodeSeason(v) { return (v.season !== undefined) ? v.season : (v.seasonNumber || 0) }
    function episodeNumber(v) { return (v.episode !== undefined) ? v.episode : (v.number || 0) }

    function filterEpisodes(vids, season) {
        var out = [];
        for (var i = 0; i < vids.length; i++)
            if (episodeSeason(vids[i]) === season) out.push(vids[i]);
        return out;
    }

    function computeSeasons(vids) {
        var seen = {}, out = [];
        for (var i = 0; i < vids.length; i++) {
            var s = episodeSeason(vids[i]);
            if (s >= 0 && !seen[s]) { seen[s] = true; out.push(s); }
        }
        // Numbered seasons ascending; Specials (season 0) pinned to the end of the row.
        out.sort(function(a, b) {
            if (a === 0) return 1;
            if (b === 0) return -1;
            return a - b;
        });
        return out;
    }

    function defaultSeason() {
        if (!seasons.length)
            return 0;
        if (typeof Progress !== "undefined") {
            var saved = Progress.lastSeason(currentId());
            if (seasonExists(saved))
                return saved;
            var resumeSeason = recentProgressSeason();
            if (seasonExists(resumeSeason))
                return resumeSeason;
        }
        // Fresh show: land on the latest NUMBERED season, never on Specials.
        for (var i = seasons.length - 1; i >= 0; i--)
            if (seasons[i] > 0)
                return seasons[i];
        return seasons[seasons.length - 1];
    }

    function seasonExists(season) {
        for (var i = 0; i < seasons.length; i++)
            if (seasons[i] === season)
                return true;
        return false;
    }

    function seasonLabel() {
        return activeSeason === 0 ? "Specials" : "Season " + activeSeason;
    }

    function queueSeasonDownload() {
        if (typeof Download === "undefined")
            return;
        var reqs = [];
        for (var i = 0; i < episodes.length; i++) {
            var v = episodes[i];
            var sid = episodeStreamId(v);
            if (Download.hasVideo(sid))
                continue;   // already on disk
            reqs.push({
                "id": sid,
                "kind": "episode",
                "title": page.title + " - S" + episodeSeason(v) + "E" + episodeNumber(v),
                "subtitle": v.title || v.name || "",
                "seriesTitle": page.title,
                "season": episodeSeason(v),
                "episode": episodeNumber(v),
                "art": page.cover
            });
        }
        if (reqs.length)
            Download.enqueueBatch(reqs);
        var q = seasonQueued;
        q[activeSeason] = true;
        seasonQueued = q;
    }

    // Single-episode download (parity spec 2026-07-06 F4): same request shape as the
    // season checkout, for exactly one episode. The per-row ↓ button calls this.
    function queueEpisodeDownload(v) {
        if (typeof Download === "undefined")
            return;
        var sid = episodeStreamId(v);
        if (Download.hasVideo(sid))
            return;   // already on disk
        Download.enqueueBatch([{
            "id": sid,
            "kind": "episode",
            "title": page.title + " - S" + episodeSeason(v) + "E" + episodeNumber(v),
            "subtitle": v.title || v.name || "",
            "seriesTitle": page.title,
            "season": episodeSeason(v),
            "episode": episodeNumber(v),
            "art": page.cover
        }]);
    }

    // ids currently sitting in the download queue (any state) — recomputed on every queue
    // change so the per-episode button can show "on its way" instead of re-queueing.
    property var queuedDownloadIds: (typeof Download !== "undefined")
        ? (Download.queueRevision, (function() {
              var m = ({});
              var js = Download.jobs();
              for (var i = 0; i < js.length; i++) m[js[i].id] = true;
              return m;
          })())
        : ({})

    function episodeStreamId(v) {
        if (v.id && v.id.length) return v.id;
        return currentId() + ":" + episodeSeason(v) + ":" + episodeNumber(v);
    }

    function sourceBackdrop() {
        return banner.length ? banner : cover;
    }

    function sourceMetaLine() {
        var parts = [];
        if (year.length) parts.push(year);
        if (genresLine.length) parts.push(genresLine);
        return parts.join(" - ");
    }

    function episodeSourceLine(v) {
        var label = "S" + episodeSeason(v) + "E" + episodeNumber(v);
        var epTitle = v.title || v.name || "";
        return epTitle.length ? (label + " - " + epTitle) : label;
    }

    function episodePlaybackTarget(v) {
        if (!v)
            return null;
        var target = shallowEpisodeTarget(v);
        target.context = adjacentEpisodeContext(v);
        return target;
    }

    function shallowEpisodeTarget(v) {
        if (!v)
            return null;
        return {
            "type": "series",
            "id": episodeStreamId(v),
            "title": page.title + " - S" + episodeSeason(v) + "E" + episodeNumber(v),
            "backdrop": sourceBackdrop(),
            "season": episodeSeason(v),
            "episode": episodeNumber(v),
            "metaLine": episodeSourceLine(v)
        };
    }

    function adjacentEpisodeContext(v) {
        var idx = episodeIndex(episodeNumber(v));
        var queue = [];
        for (var i = 0; i < episodes.length; i++)
            queue.push(shallowEpisodeTarget(episodes[i]));
        return {
            "year": page.year,
            "episodeQueue": queue,
            "episodeIndex": idx,
            "adjacentEpisodes": {
                "prev": idx > 0 ? Object.assign(shallowEpisodeTarget(episodes[idx - 1]),
                                                { "context": { "episodeQueue": queue, "episodeIndex": idx - 1 } }) : null,
                "next": (idx >= 0 && idx + 1 < episodes.length)
                        ? Object.assign(shallowEpisodeTarget(episodes[idx + 1]),
                                        { "context": { "episodeQueue": queue, "episodeIndex": idx + 1 } }) : null
            }
        };
    }

    function episodeIndex(number) {
        for (var i = 0; i < episodes.length; i++)
            if (episodeNumber(episodes[i]) === number)
                return i;
        return -1;
    }

    function jumpToEpisodeNumber(number) {
        var index = episodeIndex(number);
        if (index < 0)
            return false;
        episodeList.positionViewAtIndex(index, ListView.Beginning);
        episodeJumpOpen = false;
        episodeJumpDraft = "";
        return true;
    }

    function submitEpisodeJump() {
        var n = parseInt(episodeJumpDraft, 10);
        if (isNaN(n))
            return;
        jumpToEpisodeNumber(n);
    }

    function progressEntry(v) {
        if (typeof Progress === "undefined")
            return ({});
        var rev = Progress.revision;
        var entry = Progress.get("video", episodeStreamId(v));
        return entry || ({});
    }

    function episodeProgressRatio(v) {
        var entry = progressEntry(v);
        var p = Number(entry.progress || 0);
        if (!isFinite(p) || p < 0)
            return 0;
        return Math.max(0, Math.min(1, p));
    }

    function episodeWatched(v) {
        var entry = progressEntry(v);
        return entry.watched === true || episodeProgressRatio(v) >= 0.85;
    }

    function nextUpEpisodeNumber() {
        for (var i = 0; i < episodes.length; i++)
            if (!episodeWatched(episodes[i]))
                return episodeNumber(episodes[i]);
        return episodes.length ? episodeNumber(episodes[episodes.length - 1]) : 0;
    }

    function recentProgressSeason() {
        if (typeof Progress === "undefined")
            return -1;
        var rev = Progress.revision;
        var rows = Progress.recent("video", 80);
        var prefix = currentId() + ":";
        for (var i = 0; i < rows.length; i++) {
            var id = rows[i].id || "";
            if (id.indexOf(prefix) !== 0)
                continue;
            var parts = id.split(":");
            if (parts.length > 2)
                return Number(parts[1]);
        }
        return -1;
    }

    Theme { id: theme }

    onItemDataChanged: resolve()
    onActiveSeasonChanged: {
        // >= 0 so a Specials (season 0) pick is remembered too; resets during
        // resolve() are already guarded by `loading`.
        if (!loading && mediaType === "series" && activeSeason >= 0 && typeof Progress !== "undefined")
            Progress.rememberLastSeason(currentId(), activeSeason)
    }
    Component.onCompleted: if (currentId().length) resolve()

    function resolve() {
        loading = true;
        errorMsg = "";
        title = (itemData && itemData.title) ? itemData.title : "";
        mediaType = (itemData && itemData.type) ? itemData.type : "movie";
        banner = (itemData && itemData.art) ? itemData.art : "";
        cover = (itemData && itemData.cover) ? itemData.cover : "";
        year = "";
        genresLine = "";
        rating = "";
        runtime = "";
        synopsis = "";
        videos = [];
        seasons = [];
        activeSeason = 0;
        resolvedId = "";
        var id = currentId();
        if (!id) { loading = false; errorMsg = "No id for this title."; return; }
        revealGuard.restart();
        TheatreApi.loadMeta(mediaType, id, function(meta) {
            if (!meta) {
                errorMsg = "Couldn't load details.";
                loading = false;
                revealGuard.stop();
                return;
            }
            if (meta.id) resolvedId = String(meta.id);
            if (meta.name) title = meta.name;
            var bg = TheatreApi.normalizeArtUrl(meta.background || "");
            if (bg) banner = bg;
            var po = TheatreApi.normalizeArtUrl(meta.poster || "");
            if (po) cover = po;
            year = meta.year ? String(meta.year) : (meta.releaseInfo || "");
            if (meta.genres && meta.genres.length) genresLine = meta.genres.slice(0, 3).join(" - ");
            rating = meta.imdbRating || "";
            runtime = meta.runtime || "";
            synopsis = meta.description || "";
            videos = meta.videos || [];
            page.onMetaLoaded();
            loading = false;
            revealGuard.stop();
        });
    }

    function onMetaLoaded() {
        if (mediaType === "series") {
            seasons = computeSeasons(videos);
            activeSeason = defaultSeason();
        } else {
            seasons = [];
            activeSeason = 0;
        }
    }

    Timer { id: revealGuard; interval: 12000; repeat: false; onTriggered: page.loading = false }

    MouseArea { anchors.fill: parent }
    Rectangle { anchors.fill: parent; color: "#000000" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: page.backdrop
        live: true
        hideSource: false
        visible: page.backdrop !== null
        opacity: 0.5
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.5) }
            GradientStop { position: 0.42; color: Qt.rgba(0, 0, 0, 0.78) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.95) }
        }
    }

    ChromeScrim { z: 16 }

    BackAction {
        id: backBtn
        x: theme.margin
        y: 28
        z: 20
        onTriggered: page.backRequested()
    }

    Row {
        z: 30
        anchors.right: parent.right
        anchors.rightMargin: theme.margin
        y: 34
        spacing: 20
        Item {
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: "../assets/icons/minimize.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: minMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.minimizeRequested()
            }
        }
        Item {
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: "../assets/icons/power.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: clMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: clMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.closeRequested()
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        opacity: page.loading ? 0.0 : 1.0
        Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        Column {
            id: pageCol
            width: flick.width
            spacing: 0

            Item {
                width: parent.width
                height: 360
                Image {
                    id: bannerImg
                    anchors.fill: parent
                    source: page.banner.length ? page.banner : page.cover
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.15) }
                        GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.5) }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.96) }
                    }
                }
                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: theme.margin
                    anchors.rightMargin: theme.margin
                    anchors.bottomMargin: 30
                    spacing: 12
                    Text {
                        text: page.mediaType === "series" ? "Series - Theatre" : "Movie - Theatre"
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        font.letterSpacing: 3
                        font.capitalization: Font.AllUppercase
                    }
                    Text {
                        width: parent.width
                        text: page.title
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 64
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        style: Text.Raised
                        styleColor: Qt.rgba(0, 0, 0, 0.35)
                    }
                    Row {
                        spacing: 11
                        Text {
                            visible: page.year.length
                            text: page.year
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: page.year.length && page.genresLine.length
                            text: "-"
                            color: theme.inkDimmer
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: page.genresLine.length
                            text: page.genresLine
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: page.rating.length
                            text: "-"
                            color: theme.inkDimmer
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: page.rating.length
                            text: "* " + page.rating
                            color: theme.gold
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: page.runtime.length
                            text: "-"
                            color: theme.inkDimmer
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: page.runtime.length
                            text: page.runtime
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Row {
                        spacing: 12
                        topPadding: 8
                        visible: page.mediaType !== "series"
                        Rectangle {
                            width: watchRow.implicitWidth + 40
                            height: 42
                            radius: 11
                            color: theme.gold
                            Row {
                                id: watchRow
                                anchors.centerIn: parent
                                spacing: 9
                                Text {
                                    text: ">"
                                    color: "#1a1306"
                                    font.pixelSize: 13
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: "Watch"
                                    color: "#1a1306"
                                    font.family: theme.ui
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onEntered: parent.opacity = 0.92
                                onExited: parent.opacity = 1.0
                                onClicked: sources.show("movie", page.currentId(), page.title, {
                                                            "title": page.title,
                                                            "metaLine": page.sourceMetaLine(),
                                                            "backdrop": page.sourceBackdrop()
                                                        })
                            }
                        }
                    }
                }
            }

            Text {
                visible: page.synopsis.length > 0
                x: theme.margin
                width: Math.min(880, parent.width - 2 * theme.margin)
                text: page.synopsis
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 15
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                topPadding: 22
                bottomPadding: 6
            }

            Item {
                id: episodesSection
                width: parent.width
                height: episodesCol.height
                visible: page.mediaType === "series" && page.videos.length > 0

                Column {
                    id: episodesCol
                    width: parent.width
                    spacing: 0

                    Item {
                        // 11+ seasons: a Netflix-style dropdown — the sideways strip has no
                        // sideways browsing affordance in this app, so long shows use this.
                        width: parent.width
                        height: 56
                        z: 40
                        visible: page.seasons.length > 10
                        Rectangle {
                            id: seasonTrigger
                            x: theme.margin
                            anchors.verticalCenter: parent.verticalCenter
                            width: seasonTrigT.implicitWidth + 52
                            height: 38
                            radius: 19
                            color: seasonTrigMa.containsMouse || page.seasonMenuOpen
                                   ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.06)
                            border.width: 1
                            border.color: page.seasonMenuOpen ? theme.gold : theme.edge
                            Text {
                                id: seasonTrigT
                                x: 16
                                anchors.verticalCenter: parent.verticalCenter
                                text: page.seasonLabel()
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                text: "\u25be"
                                color: page.seasonMenuOpen ? theme.gold : theme.inkDim
                                font.pixelSize: 12
                            }
                            MouseArea {
                                id: seasonTrigMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.seasonMenuOpen = !page.seasonMenuOpen
                            }
                        }
                        Item {
                            // zero-height overlay host: the menu floats, never reflows the page
                            x: theme.margin
                            anchors.top: seasonTrigger.bottom
                            anchors.topMargin: 8
                            width: 236
                            height: 0
                            Rectangle {
                                width: parent.width
                                height: Math.min(304, seasonMenuList.contentHeight + 12)
                                visible: page.seasonMenuOpen
                                radius: 14
                                color: Qt.rgba(0.045, 0.05, 0.075, 0.97)
                                border.width: 1
                                border.color: theme.edge
                                ListView {
                                    id: seasonMenuList
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    clip: true
                                    model: page.seasons
                                    boundsBehavior: Flickable.StopAtBounds
                                    delegate: Rectangle {
                                        id: smRow
                                        required property var modelData
                                        width: seasonMenuList.width
                                        height: 36
                                        radius: 9
                                        color: smMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                                        Text {
                                            x: 12
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: smRow.modelData === 0 ? "Specials" : "Season " + smRow.modelData
                                            color: page.activeSeason === smRow.modelData ? theme.gold : theme.inkDim
                                            font.family: theme.ui
                                            font.pixelSize: 13
                                            font.weight: page.activeSeason === smRow.modelData ? Font.DemiBold : Font.Normal
                                        }
                                        MouseArea {
                                            id: smMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                page.activeSeason = smRow.modelData
                                                page.seasonMenuOpen = false
                                                episodeList.positionViewAtBeginning()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Flickable {
                        visible: page.seasons.length <= 10
                        width: parent.width
                        height: page.seasons.length <= 10 ? 44 : 0
                        contentWidth: seasonRow.width
                        contentHeight: height
                        clip: true
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        Row {
                            id: seasonRow
                            x: theme.margin
                            spacing: 22
                            topPadding: 18
                            Repeater {
                                model: page.seasons
                                // Delegate root is an Item, NOT the Column itself: a MouseArea
                                // with anchors.fill inside a positioner is ignored (0x0, dead
                                // clicks) and breaks the Column's layout entirely.
                                delegate: Item {
                                    id: seasonBtn
                                    required property var modelData
                                    width: seasonCol.width
                                    height: seasonCol.height
                                    property bool on: page.activeSeason === seasonBtn.modelData
                                    Column {
                                        id: seasonCol
                                        spacing: 5
                                        Text {
                                            text: seasonBtn.modelData === 0 ? "Specials" : ("Season " + seasonBtn.modelData)
                                            color: seasonBtn.on ? theme.gold : (seasonMa.containsMouse ? theme.ink : theme.inkDim)
                                            font.family: theme.ui
                                            font.pixelSize: 15
                                            font.weight: seasonBtn.on ? Font.DemiBold : Font.Normal
                                        }
                                        Rectangle {
                                            visible: seasonBtn.on
                                            width: 26
                                            height: 2
                                            radius: 2
                                            color: theme.gold
                                        }
                                    }
                                    MouseArea {
                                        id: seasonMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            page.activeSeason = seasonBtn.modelData
                                            episodeList.positionViewAtBeginning()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Row {
                        x: theme.margin
                        width: parent.width - 2 * theme.margin
                        height: 54
                        spacing: 12

                        Text {
                            text: page.episodes.length + " Episodes"
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        // Season checkout: queue every not-yet-downloaded episode of the
                        // ACTIVE season. Each job resolves its stream lazily when promoted.
                        Rectangle {
                            // the ACTION wears the glass tablet...
                            visible: typeof Download !== "undefined" && page.episodes.length > 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: dlSeasonT.implicitWidth + 32
                            height: 30
                            radius: 15
                            color: dlSeasonMa.containsMouse && !page.seasonQueued[page.activeSeason]
                                   ? Qt.rgba(1, 1, 1, 0.11) : Qt.rgba(1, 1, 1, 0.06)
                            border.width: 1
                            border.color: page.seasonQueued[page.activeSeason]
                                          ? theme.edge : Qt.rgba(0.94, 0.77, 0.29, 0.45)
                            Text {
                                id: dlSeasonT
                                anchors.centerIn: parent
                                text: page.seasonQueued[page.activeSeason]
                                      ? (page.seasonLabel() + " queued")
                                      : "Download " + page.seasonLabel()
                                color: page.seasonQueued[page.activeSeason] ? theme.inkDim
                                     : (dlSeasonMa.containsMouse ? "#ffd968" : theme.gold)
                                font.family: theme.ui
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            MouseArea {
                                id: dlSeasonMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                enabled: !page.seasonQueued[page.activeSeason]
                                onClicked: page.queueSeasonDownload()
                            }
                        }

                        Text {
                            // ...the STATUS reads as a quiet tag, not a second button
                            visible: page.nextUpEpisodeNumber() > 0
                            text: "NEXT \u00b7 E" + page.nextUpEpisodeNumber()
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 11
                            font.letterSpacing: 1.2
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Item { width: Math.max(0, parent.width - 408); height: 1 }

                        Rectangle {
                            width: 74
                            height: 34
                            radius: 17
                            color: Qt.rgba(1, 1, 1, 0.07)
                            border.width: 1
                            border.color: theme.edge
                            anchors.verticalCenter: parent.verticalCenter
                            Row {
                                anchors.centerIn: parent
                                spacing: 3
                                Repeater {
                                    model: ["list", "strip"]
                                    delegate: Rectangle {
                                        id: layoutBtn
                                        required property string modelData
                                        width: 31
                                        height: 28
                                        radius: 14
                                        color: page.episodeLayout === modelData ? theme.ink : "transparent"
                                        Text {
                                            anchors.centerIn: parent
                                            text: layoutBtn.modelData === "list" ? "\u2630" : "\u25ad"
                                            color: page.episodeLayout === layoutBtn.modelData ? "#111111" : theme.inkDim
                                            font.family: theme.ui
                                            font.pixelSize: layoutBtn.modelData === "list" ? 15 : 17
                                            font.weight: Font.DemiBold
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                page.episodeLayout = layoutBtn.modelData
                                                episodeList.positionViewAtBeginning()
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Item {
                            id: jumpHost
                            width: 102
                            height: 34
                            anchors.verticalCenter: parent.verticalCenter
                            visible: page.episodes.length >= 12
                            Rectangle {
                                anchors.fill: parent
                                radius: 8
                                color: page.episodeJumpOpen ? Qt.rgba(1, 1, 1, 0.11) : Qt.rgba(1, 1, 1, 0.06)
                                border.width: 1
                                border.color: page.episodeJumpOpen ? theme.gold : theme.edge
                                Text {
                                    anchors.centerIn: parent
                                    text: "#  Jump"
                                    color: page.episodeJumpOpen ? theme.gold : theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        page.episodeJumpOpen = !page.episodeJumpOpen
                                        if (page.episodeJumpOpen)
                                            jumpInput.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }

                    Item { width: parent.width; height: 0; z: 30   // overlay host: the panel floats
                    Rectangle {
                        x: Math.max(theme.margin, parent.width - theme.margin - 292)
                        y: 6
                        width: 292
                        height: jumpRanges.height + 58
                        radius: 12
                        visible: page.episodeJumpOpen
                        color: Qt.rgba(0.045, 0.05, 0.075, 0.97)
                        border.width: 1
                        border.color: theme.edge
                        z: 3

                        Row {
                            id: jumpInputRow
                            x: 12
                            y: 12
                            spacing: 8
                            Rectangle {
                                width: 184
                                height: 34
                                radius: 8
                                color: Qt.rgba(1, 1, 1, 0.06)
                                border.width: 1
                                border.color: jumpInput.activeFocus ? theme.gold : theme.edge
                                TextInput {
                                    id: jumpInput
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    verticalAlignment: TextInput.AlignVCenter
                                    text: page.episodeJumpDraft
                                    color: theme.ink
                                    selectionColor: theme.gold
                                    selectedTextColor: "#111111"
                                    font.family: theme.ui
                                    font.pixelSize: 13
                                    validator: IntValidator { bottom: 1; top: Math.max(1, page.episodes.length) }
                                    onTextChanged: page.episodeJumpDraft = text
                                    Keys.onReturnPressed: page.submitEpisodeJump()
                                    Keys.onEnterPressed: page.submitEpisodeJump()
                                    Keys.onEscapePressed: page.episodeJumpOpen = false
                                }
                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: jumpInput.text.length === 0 && !jumpInput.activeFocus
                                    text: "1 - " + page.episodes.length
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 13
                                }
                            }
                            Rectangle {
                                width: 72
                                height: 34
                                radius: 8
                                color: theme.ink
                                opacity: page.episodeJumpDraft.length ? 1.0 : 0.45
                                Text {
                                    anchors.centerIn: parent
                                    text: "Jump"
                                    color: "#111111"
                                    font.family: theme.ui
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.submitEpisodeJump()
                                }
                            }
                        }

                        Flow {
                            id: jumpRanges
                            x: 12
                            y: 56
                            width: parent.width - 24
                            spacing: 6
                            Repeater {
                                model: Math.ceil(page.episodes.length / 50)
                                delegate: Rectangle {
                                    id: rangeBtn
                                    required property int index
                                    property int start: index * 50 + 1
                                    property int end: Math.min(start + 49, page.episodes.length)
                                    width: label.implicitWidth + 18
                                    height: 25
                                    radius: 6
                                    color: Qt.rgba(1, 1, 1, 0.06)
                                    border.width: 1
                                    border.color: theme.edge
                                    Text {
                                        id: label
                                        anchors.centerIn: parent
                                        text: rangeBtn.start + "-" + rangeBtn.end
                                        color: theme.inkDim
                                        font.family: theme.ui
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: page.jumpToEpisodeNumber(rangeBtn.start)
                                    }
                                }
                            }
                        }
                    }

                    }

                    ListView {
                        id: episodeList
                        width: parent.width
                        height: page.episodeLayout === "strip"
                                ? 210
                                : Math.min(page.episodes.length * (rowHeight + spacing),
                                           Math.max(360, page.height - 210))
                        clip: true
                        model: page.episodes
                        orientation: page.episodeLayout === "strip" ? ListView.Horizontal : ListView.Vertical
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: page.episodeLayout === "strip" ? Flickable.HorizontalFlick : Flickable.VerticalFlick
                        reuseItems: true
                        cacheBuffer: rowHeight * 6
                        spacing: page.episodeLayout === "strip" ? 0 : 8
                        property int rowHeight: 121

                        onModelChanged: positionViewAtBeginning()

                        ScrollBar.vertical: ScrollBar {
                            id: episodeScroll
                            policy: episodeList.contentHeight > episodeList.height
                                    ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                            width: 8
                            anchors.right: parent.right
                            anchors.rightMargin: 18
                            contentItem: Rectangle {
                                implicitWidth: 4
                                radius: 2
                                color: episodeScroll.active ? theme.gold : Qt.rgba(1, 1, 1, 0.32)
                            }
                            background: Rectangle {
                                implicitWidth: 8
                                radius: 4
                                color: Qt.rgba(1, 1, 1, 0.07)
                            }
                        }

                        delegate: Item {
                            id: ep
                            required property var modelData
                            property bool strip: page.episodeLayout === "strip"
                            property real progressRatio: page.episodeProgressRatio(modelData)
                            property bool watched: page.episodeWatched(modelData)
                            property bool nextUp: page.nextUpEpisodeNumber() === page.episodeNumber(modelData)
                            width: strip ? 246 : ListView.view.width
                            height: strip ? 190 : episodeList.rowHeight
                            Rectangle {
                                anchors.fill: parent
                                anchors.leftMargin: ep.strip ? 0 : theme.margin - 10
                                anchors.rightMargin: ep.strip ? 0 : theme.margin - 10
                                color: ep.nextUp ? Qt.rgba(0.94, 0.77, 0.29, 0.06)
                                      : (epMa.containsMouse ? Qt.rgba(1, 1, 1, 0.055) : "transparent")
                                border.width: ep.strip ? 1 : 0
                                border.color: ep.nextUp ? theme.gold : theme.edge
                                radius: ep.strip ? 10 : 14
                            }
                            Rectangle {
                                id: thumb
                                x: ep.strip ? 10 : theme.margin
                                y: ep.strip ? 10 : (parent.height - height) / 2
                                width: ep.strip ? parent.width - 20 : 176
                                height: ep.strip ? 118 : 99
                                radius: ep.strip ? 8 : 10
                                clip: true
                                color: "#15171f"
                                Image {
                                    anchors.fill: parent
                                    source: ep.modelData.thumbnail ? ep.modelData.thumbnail : page.sourceBackdrop()
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: true
                                    visible: status === Image.Ready
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: !ep.modelData.thumbnail
                                    text: "E" + page.episodeNumber(ep.modelData)
                                    color: Qt.rgba(1, 1, 1, 0.5)
                                    font.family: theme.display
                                    font.pixelSize: 22
                                }
                                Rectangle {   // episode-number badge (Electron device)
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.margins: 7
                                    width: numT.implicitWidth + 10
                                    height: 22
                                    radius: 6
                                    color: Qt.rgba(0, 0, 0, 0.7)
                                    Text {
                                        id: numT
                                        anchors.centerIn: parent
                                        text: page.episodeNumber(ep.modelData)
                                        color: theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: 12
                                        font.weight: Font.Bold
                                    }
                                }
                                Rectangle {
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 7
                                    width: 24
                                    height: 24
                                    radius: 12
                                    visible: ep.watched
                                    color: theme.gold
                                    Text {
                                        anchors.centerIn: parent
                                        text: "\u2713"
                                        color: "#14110a"
                                        font.family: theme.ui
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                    }
                                }
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 4
                                    visible: ep.progressRatio > 0.01 && !ep.watched
                                    color: Qt.rgba(0, 0, 0, 0.5)
                                    Rectangle {
                                        width: parent.width * ep.progressRatio
                                        height: parent.height
                                        color: theme.gold
                                    }
                                }
                            }
                            Column {
                                anchors.left: ep.strip ? parent.left : thumb.right
                                anchors.leftMargin: ep.strip ? 12 : 18
                                anchors.verticalCenter: ep.strip ? undefined : parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: ep.strip ? 12 : theme.margin
                                y: ep.strip ? 134 : 0
                                spacing: 5
                                Text {
                                    width: parent.width
                                    text: "E" + page.episodeNumber(ep.modelData) + " - "
                                          + ((ep.modelData.name && ep.modelData.name.length) ? ep.modelData.name
                                             : (ep.modelData.title && ep.modelData.title.length ? ep.modelData.title
                                                : "Episode " + page.episodeNumber(ep.modelData)))
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    // one dim status line: state + date joined, gold ONLY for Next up
                                    visible: text.length > 0
                                    text: {
                                        var parts = [];
                                        if (ep.nextUp) parts.push("Next up");
                                        else if (ep.watched) parts.push("Watched");
                                        else if (ep.progressRatio > 0.01) parts.push(Math.round(ep.progressRatio * 100) + "% watched");
                                        if (ep.modelData.released) {
                                            var d = new Date(ep.modelData.released);
                                            parts.push(d.toLocaleDateString(Qt.locale(), Locale.ShortFormat));
                                        }
                                        return parts.join(" \u00b7 ");
                                    }
                                    color: ep.nextUp ? theme.gold : theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    visible: !ep.strip && !!(ep.modelData.overview || ep.modelData.description)
                                    width: parent.width
                                    text: ep.modelData.overview || ep.modelData.description || ""
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 13
                                    lineHeight: 1.35
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }
                            MouseArea {
                                id: epMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: sources.show("series", page.episodeStreamId(ep.modelData),
                                                        page.title + " - S" + page.episodeSeason(ep.modelData) + "E" + page.episodeNumber(ep.modelData), Object.assign({
                                                            "title": page.title,
                                                            "metaLine": page.episodeSourceLine(ep.modelData),
                                                            "backdrop": page.sourceBackdrop()
                                                        }, page.adjacentEpisodeContext(ep.modelData)))
                            }
                            // Per-episode download (parity spec F4). Declared AFTER epMa so it
                            // stacks above the row's open-sources click. Three states: on disk
                            // (inert ✓), queued (inert ↓ dim), ready to queue (↓).
                            Rectangle {
                                id: epDl
                                property string sid: page.episodeStreamId(ep.modelData)
                                property bool onDisk: (typeof Download !== "undefined")
                                    ? (Download.queueRevision, Download.hasVideo(sid)) : false
                                property bool inQueue: page.queuedDownloadIds[sid] === true
                                visible: typeof Download !== "undefined"
                                x: thumb.x + thumb.width - width - 7
                                y: thumb.y + thumb.height - height - 7
                                width: 30
                                height: 30
                                radius: 15
                                color: Qt.rgba(0, 0, 0, 0.7)
                                border.width: 1
                                border.color: epDl.onDisk ? theme.gold
                                             : (epDlMa.containsMouse && !epDl.inQueue) ? theme.gold : theme.edge
                                Text {
                                    anchors.centerIn: parent
                                    text: epDl.onDisk ? "✓" : "↓"
                                    color: epDl.onDisk ? theme.gold
                                          : epDl.inQueue ? theme.inkDimmer
                                          : (epDlMa.containsMouse ? theme.gold : theme.inkDim)
                                    font.family: theme.ui
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }
                                MouseArea {
                                    id: epDlMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: (epDl.onDisk || epDl.inQueue) ? Qt.ArrowCursor : Qt.PointingHandCursor
                                    onClicked: if (!epDl.onDisk && !epDl.inQueue) page.queueEpisodeDownload(ep.modelData)
                                }
                            }
                        }
                    }
                }
            }

            Text {
                visible: !page.loading && page.errorMsg.length > 0
                x: theme.margin
                text: page.errorMsg
                color: "#e6a3a3"
                font.family: theme.ui
                font.pixelSize: 13
                topPadding: 18
            }

            Item { width: 1; height: 70 }
        }
    }

    Column {
        id: loadingState
        visible: page.loading
        opacity: page.loading ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        anchors.centerIn: parent
        width: parent.width * 0.7
        spacing: 14
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: page.title
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 34
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: page.errorMsg.length ? page.errorMsg : "Loading..."
            color: page.errorMsg.length ? "#e6a3a3" : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 14
        }
    }

    SourcesSheet {
        id: sources
        z: 60
        backdrop: page.backdrop
        onPlayRequested: (infoHash, fileIdx, title, backdropUrl, subType, subId, streamCandidates, playbackContext) => page.playRequested(infoHash, fileIdx, title, backdropUrl, subType, subId, streamCandidates, playbackContext)
    }
}
