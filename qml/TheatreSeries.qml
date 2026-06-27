// TheatreSeries - Theatre detail page for movies and series.
// Mirrors MangaSeries.qml house style: full-bleed banner, inline metadata, reveal gate,
// pitch-black base, and a slide-up SourcesSheet for Torrentio rows.
import QtQuick
import "TheatreApi.js" as TheatreApi

Item {
    id: page
    property Item backdrop
    property var itemData: ({})
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal playRequested(string infoHash, int fileIdx, string title, string backdropUrl)

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
    property bool loading: true
    property string errorMsg: ""

    function currentId() { return (itemData && itemData.id) ? itemData.id : "" }
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
            if (s > 0 && !seen[s]) { seen[s] = true; out.push(s); }
        }
        out.sort(function(a, b) { return a - b; });
        return out;
    }

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

    Theme { id: theme }

    onItemDataChanged: resolve()
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
            activeSeason = seasons.length ? seasons[0] : 0;
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

    Item {
        id: backBtn
        x: theme.margin
        y: 28
        width: backRow.implicitWidth + 16
        height: 34
        z: 20
        Row {
            id: backRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text {
                text: "<"
                color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.display
                font.pixelSize: 26
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: "Back"
                color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.ui
                font.pixelSize: 15
                anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 120 } }
            }
        }
        MouseArea {
            id: backMa
            anchors.fill: parent
            anchors.margins: -8
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: page.backRequested()
        }
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

                    Flickable {
                        width: parent.width
                        height: 44
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
                                delegate: Column {
                                    id: seasonBtn
                                    required property var modelData
                                    spacing: 5
                                    property bool on: page.activeSeason === seasonBtn.modelData
                                    Text {
                                        text: "Season " + seasonBtn.modelData
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
                                    MouseArea {
                                        id: seasonMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: page.activeSeason = seasonBtn.modelData
                                    }
                                }
                            }
                        }
                    }

                    Repeater {
                        model: page.episodes
                        delegate: Item {
                            id: ep
                            required property var modelData
                            width: episodesCol.width
                            height: 92
                            Rectangle {
                                anchors.fill: parent
                                color: epMa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                            }
                            Rectangle {
                                id: thumb
                                x: theme.margin
                                anchors.verticalCenter: parent.verticalCenter
                                width: 132
                                height: 74
                                radius: 6
                                clip: true
                                color: "#15171f"
                                Image {
                                    anchors.fill: parent
                                    source: ep.modelData.thumbnail ? ep.modelData.thumbnail : ""
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
                            }
                            Column {
                                anchors.left: thumb.right
                                anchors.leftMargin: 18
                                anchors.right: parent.right
                                anchors.rightMargin: theme.margin
                                anchors.verticalCenter: parent.verticalCenter
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
                                    visible: !!ep.modelData.released
                                    text: {
                                        var d = ep.modelData.released ? new Date(ep.modelData.released) : null;
                                        return d ? d.toLocaleDateString(Qt.locale(), Locale.ShortFormat) : "";
                                    }
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 12
                                }
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: Qt.rgba(1, 1, 1, 0.05)
                            }
                            MouseArea {
                                id: epMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: sources.show("series", page.episodeStreamId(ep.modelData),
                                                        page.title + " - S" + page.episodeSeason(ep.modelData) + "E" + page.episodeNumber(ep.modelData), {
                                                            "title": page.title,
                                                            "metaLine": page.episodeSourceLine(ep.modelData),
                                                            "backdrop": page.sourceBackdrop()
                                                        })
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
        onPlayRequested: (infoHash, fileIdx, title, backdropUrl) => page.playRequested(infoHash, fileIdx, title, backdropUrl)
    }
}
