// SourcesSheet - Torrentio source picker in the Colosseum house language (approved mock, 2026-06-27).
// The full source-picker content is the JOB, not clutter: provider, the quality TOP BAR + quick-filter
// pills, release, seeders/size/source, audio + language, format chips, stream type, and an always-on
// action button. House craft on top: a GLASS table floating over the wallpaper, warm Colosseum ink
// (never cold blue), real glyphs, and gold kept to the active pill / quality line / play button.
// Public API (show/hide/properties) is unchanged.
import QtQuick
import "Torrentio.js" as Torrentio

Item {
    id: sheet
    anchors.fill: parent

    property Item backdrop
    property string label: ""
    property string title: ""
    property string metaLine: ""
    property string backdropUrl: ""
    property string subType: ""   // "movie" | "series" — carried to the player for online subtitle fetch
    property string subId: ""     // "tt..." (movie) or "tt...:s:e" (episode)
    property bool open: false
    property bool loading: false
    property bool timedOut: false
    property var rows: []
    property int gen: 0
    property string qualityFilter: "all"
    property var visibleRows: filteredRows()

    // a source row was chosen → play it (handled up at Main, which opens the player)
    signal playRequested(string infoHash, int fileIdx, string title, string backdropUrl, string subType, string subId)

    visible: sheet.open || sheet.opacity > 0.01
    opacity: sheet.open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 180 } }

    Theme { id: theme }

    function show(type, id, lbl, context) {
        sheet.subType = type ? type : "";
        sheet.subId = id ? id : "";
        sheet.label = lbl ? lbl : "";
        sheet.title = (context && context.title) ? context.title : sheet.label;
        sheet.metaLine = (context && context.metaLine) ? context.metaLine : "";
        sheet.backdropUrl = (context && context.backdrop) ? context.backdrop : "";
        sheet.rows = [];
        sheet.qualityFilter = "all";
        sheet.timedOut = false;
        sheet.loading = true;
        sheet.open = true;
        sheet.gen += 1;
        var myGen = sheet.gen;
        timeout.restart();
        Torrentio.loadStreams(type, id, function(list) {
            if (myGen !== sheet.gen) return;
            sheet.rows = list;
            sheet.loading = false;
            timeout.stop();
        });
    }

    function hide() {
        sheet.gen += 1;
        sheet.open = false;
        timeout.stop();
    }

    function filteredRows() {
        if (sheet.qualityFilter === "all") return sheet.rows;
        var out = [];
        for (var i = 0; i < sheet.rows.length; ++i)
            if (sheet.rows[i].quality === sheet.qualityFilter) out.push(sheet.rows[i]);
        return out;
    }

    function countFor(q) {
        if (q === "all") return sheet.rows.length;
        var n = 0;
        for (var i = 0; i < sheet.rows.length; ++i)
            if (sheet.rows[i].quality === q) ++n;
        return n;
    }

    function chipText(q) { return q === "all" ? "All" : q; }

    // the one inline meta line: seeders, size, source group
    function metaText(m) {
        var p = [];
        if (m.seeders >= 0) p.push("\u{1F464} " + m.seeders);
        if (m.size) p.push("\u{1F4BE} " + m.size);
        if (m.sourceName && m.sourceName !== "P2P") p.push("⚙ " + m.sourceName);
        return p.join("   ·   ");
    }

    Timer {
        id: timeout
        interval: 22000
        repeat: false
        onTriggered: if (sheet.loading) { sheet.loading = false; sheet.timedOut = true }
    }

    // ===================== base: float over the wallpaper, not a flat void =====================
    Rectangle { anchors.fill: parent; color: "#000000" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: sheet.backdrop
        live: true; hideSource: false
        visible: sheet.backdrop !== null
        opacity: 0.5
    }
    MouseArea { anchors.fill: parent }                                    // absorb clicks from below

    // ---- banner hero: the title's key-art across the top, washing down ----
    Item {
        id: bannerStrip
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 300
        Image {
            anchors.fill: parent
            source: sheet.backdropUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true; cache: true
            visible: sheet.backdropUrl.length > 0
            opacity: status === Image.Ready ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.25) }
                GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.5) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.92) }
            }
        }
    }
    Rectangle {                                                          // scrim over the rest
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: bannerStrip.bottom; anchors.bottom: parent.bottom
        color: Qt.rgba(0, 0, 0, 0.9)
    }

    // ---- back ----
    Item {
        x: theme.margin; y: 30; width: backRow.implicitWidth + 16; height: 34; z: 20
        Row {
            id: backRow; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            Text { text: "‹"; color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.display; font.pixelSize: 28; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Back"; color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.ui; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 120 } } }
        }
        MouseArea { id: backMa; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: sheet.hide() }
    }

    // ---- title block, pinned to the bottom of the banner ----
    Column {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        anchors.top: parent.top; anchors.topMargin: bannerStrip.height - height - 26
        spacing: 12
        Text {
            visible: sheet.metaLine.length > 0
            width: parent.width
            text: sheet.metaLine.toUpperCase()
            color: theme.gold; font.family: theme.ui; font.pixelSize: 12
            font.letterSpacing: 4; elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: sheet.title.length ? sheet.title : "Sources"
            color: theme.ink; font.family: theme.display
            font.pixelSize: 56; font.weight: Font.DemiBold
            maximumLineCount: 1; elide: Text.ElideRight
            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.35)
        }
    }

    // ===================== filters: the quality TOP BAR + quick-filter pills =====================
    // the full-width selector bar (the "top bar that separates the quality")
    Rectangle {
        id: topBar
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        anchors.top: bannerStrip.bottom; anchors.topMargin: 20
        height: 64; radius: 16
        color: tbMa.containsMouse ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.06)
        border.width: 1
        border.color: tbMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.45) : theme.edge
        Behavior on border.color { ColorAnimation { duration: 140 } }
        visible: sheet.rows.length > 0

        Rectangle {                                                      // grid glyph
            id: gridBadge
            anchors.left: parent.left; anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            width: 44; height: 44; radius: 12
            color: Qt.rgba(1, 1, 1, 0.05); border.width: 1; border.color: theme.edge
            Grid {
                anchors.centerIn: parent; rows: 2; columns: 2; rowSpacing: 5; columnSpacing: 5
                Repeater { model: 4
                    delegate: Rectangle { width: 7; height: 7; radius: 2; color: theme.inkDim } }
            }
        }
        Text {
            anchors.left: gridBadge.right; anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: "All"; color: theme.ink; font.family: theme.ui; font.pixelSize: 22
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: "▾"; color: theme.inkDim; font.family: theme.display; font.pixelSize: 18
        }
        MouseArea { id: tbMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }
    }

    // quality quick-filter pills (active = gold fill)
    Row {
        id: pills
        anchors.left: parent.left; anchors.leftMargin: theme.margin
        anchors.top: topBar.bottom; anchors.topMargin: 14
        spacing: 12
        visible: sheet.rows.length > 0
        Repeater {
            model: ["all", "4K", "1080p", "720p", "SD"]
            delegate: Rectangle {
                id: pill
                required property string modelData
                property bool on: sheet.qualityFilter === pill.modelData
                property int n: sheet.countFor(pill.modelData)
                visible: n > 0 || pill.modelData === "all"
                width: pillRow.implicitWidth + 36; height: 40; radius: 20
                color: pill.on ? theme.gold : (pMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05))
                border.width: pill.on ? 0 : 1
                border.color: theme.edge
                Row {
                    id: pillRow; anchors.centerIn: parent; spacing: 8
                    Text {
                        text: sheet.chipText(pill.modelData)
                        color: pill.on ? "#1a1306" : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 15; font.weight: pill.on ? Font.Bold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: pill.n
                        color: pill.on ? Qt.rgba(0.10, 0.075, 0.02, 0.7) : theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea { id: pMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: sheet.qualityFilter = pill.modelData }
            }
        }
    }

    // ===================== the glass source table =====================
    Glass {
        id: table
        backdrop: sheet.backdrop
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        anchors.top: pills.visible ? pills.bottom : bannerStrip.bottom
        anchors.topMargin: 16
        anchors.bottom: parent.bottom; anchors.bottomMargin: 26
        radius: 18
        track: 0

        Item {
            id: tableHead
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: 52
            visible: !sheet.loading && !sheet.timedOut && sheet.visibleRows.length > 0
            Text {
                anchors.left: parent.left; anchors.leftMargin: 26; anchors.verticalCenter: parent.verticalCenter
                text: sheet.visibleRows.length + (sheet.visibleRows.length === 1 ? " source" : " sources")
                color: theme.ink; font.family: theme.display; font.pixelSize: 16; font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 26; anchors.verticalCenter: parent.verticalCenter
                text: "via Torrentio"
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 1
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
        }

        Text {
            anchors.centerIn: parent
            visible: sheet.loading || sheet.timedOut || (!sheet.loading && sheet.rows.length === 0)
            text: sheet.loading ? "Finding sources…"
                  : (sheet.timedOut ? "Sources timed out. Try again." : "No sources found.")
            color: sheet.timedOut ? "#e6a3a3" : theme.inkDim
            font.family: theme.ui; font.pixelSize: 16
        }

        ListView {
            id: list
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: tableHead.bottom; anchors.bottom: parent.bottom
            anchors.topMargin: 4; anchors.bottomMargin: 8
            clip: true
            visible: !sheet.loading && !sheet.timedOut && sheet.visibleRows.length > 0
            model: sheet.visibleRows
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                id: row
                required property var modelData
                width: ListView.view.width
                height: 150

                Rectangle { anchors.fill: parent; color: rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent" }

                // provider logo
                Rectangle {
                    id: logo
                    anchors.left: parent.left; anchors.leftMargin: 26
                    anchors.verticalCenter: parent.verticalCenter
                    width: 54; height: 54; radius: 12
                    color: Qt.rgba(1, 1, 1, 0.05); border.width: 1; border.color: theme.edge
                    Text { anchors.centerIn: parent; text: "T"; color: theme.ink
                        font.family: theme.display; font.pixelSize: 24; font.weight: Font.DemiBold }
                }

                // copy column — every element, clean hierarchy
                Column {
                    anchors.left: logo.right; anchors.leftMargin: 24
                    anchors.right: play.left; anchors.rightMargin: 24
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7

                    Row {
                        spacing: 12
                        Text { text: row.modelData.addonName; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter }
                        Text { text: row.modelData.qualityLine || row.modelData.quality; color: theme.gold
                            font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 0.5
                            anchors.verticalCenter: parent.verticalCenter }
                    }
                    Text {
                        width: parent.width
                        text: row.modelData.release
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 14; elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: sheet.metaText(row.modelData)
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight
                    }
                    Row {
                        spacing: 8
                        visible: row.modelData.audio !== undefined
                        Text { text: row.modelData.audio; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter }
                        Repeater {
                            model: row.modelData.languages || []
                            delegate: Rectangle {
                                required property string modelData
                                width: lg.implicitWidth + 12; height: 18; radius: 4
                                color: "transparent"; border.width: 1; border.color: theme.edge
                                anchors.verticalCenter: parent.verticalCenter
                                Text { id: lg; anchors.centerIn: parent; text: parent.modelData
                                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1 }
                            }
                        }
                    }
                    Row {
                        spacing: 7
                        Repeater {
                            model: row.modelData.tags || []
                            delegate: Rectangle {
                                required property string modelData
                                width: tg.implicitWidth + 16; height: 20; radius: 6
                                color: Qt.rgba(1, 1, 1, 0.05); border.width: 1; border.color: theme.edge
                                Text { id: tg; anchors.centerIn: parent; text: parent.modelData; color: theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            }
                        }
                    }
                    Text {
                        text: (row.modelData.streamKind || "Torrent") + " · " + (row.modelData.streamLabel || "P2P stream")
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                    }
                }

                // always-visible action button — solid gold, real triangle glyph
                Rectangle {
                    id: play
                    anchors.right: parent.right; anchors.rightMargin: 30
                    anchors.verticalCenter: parent.verticalCenter
                    width: 56; height: 56; radius: 28; color: theme.gold
                    scale: rowMa.containsMouse ? 1.05 : 1.0
                    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    Text { anchors.centerIn: parent; anchors.horizontalCenterOffset: 2
                        text: "▶"; color: "#1a1306"; font.pixelSize: 18 }
                }

                MouseArea {
                    id: rowMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sheet.playRequested(row.modelData.infoHash, row.modelData.fileIdx,
                                                   sheet.title, sheet.backdropUrl, sheet.subType, sheet.subId)
                }
            }
        }
    }
}
