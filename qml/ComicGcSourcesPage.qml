// ComicGcSourcesPage — the "Also on GetComics" sources page (spec 2026-07-17).
// The ledger's rail became a doorway banner; THIS page is what it opens: every
// attached post for one series, grouped packs -> collected editions -> singles,
// size DESC within each group (fullest grab first), cover thumbnails enriched by
// one ComicsApi.postsById call. Rows paint instantly from the baked catalog;
// covers/sizes fade in when the single response lands (one reflow, accepted).
// Download machinery is the rail's, byte-for-byte: same "gcpost-<id>" chId
// namespace, so anything downloaded from the old rail shows Downloaded here.
import QtQuick
import QtQuick.Controls
import "ComicsApi.js" as Api
import "ComicResolve.js" as Resolve
import "ComicGcSources.js" as Gc

Item {
    id: page
    property Item backdrop
    property string seriesTitle: ""
    property string publisher: ""
    property string cover: ""              // series art: hero thumb + backdrop wash
    property string gcTag: ""              // "gc:<tag>" download/reader namespace
    property var    sources: []            // baked [{id,title,link,date,kind,fan_made}]
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal readRequested(string chId, string label)

    Theme { id: theme }

    // ---- enrichment: one call, generation-guarded (page is reused across opens) ----
    property var enrich: ({})
    property int _gen: 0
    onSourcesChanged: fetchEnrich()
    Component.onCompleted: fetchEnrich()
    function fetchEnrich() {
        var gen = ++page._gen
        page.enrich = ({})
        var ids = []
        for (var i = 0; i < (sources || []).length; i++) ids.push(sources[i].id)
        if (!ids.length) return
        Api.postsById(ids, function(map) {
            if (gen !== page._gen) return          // stale: a newer series is on screen
            page.enrich = map || ({})
        })
    }
    readonly property var groups: Gc.groupSources(page.sources, page.enrich)

    // ---- pitch-black stack: opaque base, world art, heavy wash (house shell) ----
    Rectangle { anchors.fill: parent; color: "#000000" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: page.backdrop
        live: true; hideSource: false
        visible: page.backdrop !== null
        opacity: 0.5
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0;  color: Qt.rgba(0, 0, 0, 0.5) }
            GradientStop { position: 0.42; color: Qt.rgba(0, 0, 0, 0.78) }
            GradientStop { position: 1.0;  color: Qt.rgba(0, 0, 0, 0.95) }
        }
    }
    ChromeScrim { z: 16 }
    BackAction { x: theme.margin; y: 28; z: 20; onTriggered: page.backRequested() }
    Row {
        z: 30
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 34
        spacing: 20
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor; onClicked: page.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: clMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: clMa; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor; onClicked: page.closeRequested() }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol.height + 96
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: flick }
        ScrollGlide { flick: flick }

        Column {
            id: pageCol
            x: theme.margin; y: 96
            width: parent.width - theme.margin * 2
            spacing: 0

            // ================= HERO =================
            Column {
                width: parent.width
                spacing: 7
                bottomPadding: 30
                Text { text: "ALSO ON GETCOMICS"; color: theme.gold
                       font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3 }
                Text { width: parent.width; text: page.seriesTitle
                       color: theme.ink; font.family: theme.display; font.pixelSize: 34
                       elide: Text.ElideRight }
                Text {
                    text: page.sources.length
                          + (page.sources.length === 1 ? " download" : " downloads")
                          + (page.publisher.length ? "   ·   " + page.publisher : "")
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                }
            }

            // ================= GROUPS =================
            Repeater {
                model: page.groups
                delegate: Column {
                    id: grp
                    required property var modelData
                    width: pageCol.width
                    topPadding: 18
                    spacing: 4

                    Row {                                 // gold section line (ledger language)
                        width: parent.width; spacing: 14
                        Text { text: grp.modelData.label; color: theme.gold
                            font.family: theme.ui; font.pixelSize: 12
                            font.letterSpacing: 2.8; font.capitalization: Font.AllUppercase
                            anchors.verticalCenter: parent.verticalCenter }
                        Text { text: grp.modelData.rows.length; color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 12
                            anchors.verticalCenter: parent.verticalCenter }
                        Rectangle { width: parent.width - x; height: 1
                            color: Qt.rgba(0.94, 0.77, 0.29, 0.34)
                            anchors.verticalCenter: parent.verticalCenter }
                    }

                    Repeater {
                        model: grp.modelData.rows
                        delegate: Item {
                            id: src
                            required property var modelData
                            width: pageCol.width
                            height: 72

                            property string chId: "gcpost-" + String(src.modelData.id)
                            property string dlState: "none" // none|resolving|queued|downloading|extracting|choosing|done|error|dead
                            property real   dlDone: 0
                            property real   dlTotal: 0
                            readonly property bool inFlight: dlState === "downloading" || dlState === "queued"
                                                          || dlState === "resolving"   || dlState === "extracting"
                                                          || dlState === "choosing"

                            function refreshDl() {
                                if (typeof Comics === "undefined" || !chId.length) return
                                var st = Comics.statusOf(chId)
                                src.dlState = st.state; src.dlDone = st.done; src.dlTotal = st.total
                            }
                            function primary() {
                                if (typeof Comics === "undefined" || !chId.length) return
                                if (dlState === "dead") return
                                if (dlState === "done") { page.readRequested(chId, String(src.modelData.title || "")); return }
                                if (inFlight) return
                                src.dlState = "queued"
                                Comics.downloadIssue(chId, src.modelData.link, page.gcTag,
                                                     page.seriesTitle, String(src.modelData.title), 0)
                            }
                            function statusLine() {
                                if (dlState === "done") return "● Downloaded"
                                if (dlState === "resolving") return "Resolving…"
                                if (dlState === "queued") return "Queued…"
                                if (dlState === "extracting") return "Extracting…"
                                if (dlState === "choosing") return "Choosing a source…"
                                if (dlState === "downloading")
                                    return dlTotal > 0 ? ("Downloading " + Math.round(dlDone / dlTotal * 100) + "%") : "Downloading…"
                                if (dlState === "dead") return "Not available from this source"
                                if (dlState === "error") return "⚠ Failed — tap to retry"
                                return ""
                            }
                            // kind is implicit in the section header, so sub-bits are only:
                            // Fan-made flag + live download status (inline metadata, no pills)
                            function subBits() {
                                var bits = []
                                if (src.modelData.fan_made) bits.push({ text: "Fan-made", color: theme.inkDimmer })
                                var st = src.statusLine()
                                if (st.length) bits.push({ text: st, color: src.dlState === "done" ? theme.gold
                                                                            : (src.dlState === "error" ? "#e6a3a3" : theme.inkDim) })
                                return bits
                            }
                            Component.onCompleted: refreshDl()
                            Connections {
                                target: typeof Comics !== "undefined" ? Comics : null
                                function onProgress(cid, done, total) {
                                    if (cid !== src.chId) return
                                    src.dlState = "downloading"; src.dlDone = done; src.dlTotal = total
                                }
                                function onFinished(cid) { if (cid === src.chId) src.dlState = "done" }
                                function onFailed(cid, reason) {
                                    if (cid === src.chId) src.dlState = Resolve.failureIsTerminal(reason) ? "dead" : "error"
                                }
                                function onRemoved(cid) { if (cid === src.chId) src.dlState = "none" }
                            }

                            Rectangle { anchors.fill: parent; radius: 8
                                color: src.dlState !== "dead" && srcMa.containsMouse ? Qt.rgba(1,1,1,0.045) : "transparent" }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                                color: Qt.rgba(1, 1, 1, 0.055) }

                            // ---- cover thumb: quiet dark card until art lands; wp.com
                            //      429-throttles bursts -> staggered re-request (shelf pattern) ----
                            Rectangle {
                                id: thumb
                                anchors.left: parent.left; anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: 40; height: 58; radius: 4
                                color: "#15171f"; border.width: 1; border.color: theme.edge
                                clip: true
                                Image {
                                    id: thumbImg
                                    anchors.fill: parent; anchors.margins: 1
                                    source: src.modelData.cover || ""
                                    visible: status === Image.Ready
                                    asynchronous: true; cache: true
                                    fillMode: Image.PreserveAspectCrop
                                    sourceSize.width: 80
                                    property int retries: 0
                                    opacity: status === Image.Ready ? 1.0 : 0.0
                                    Behavior on opacity { NumberAnimation { duration: 220 } }
                                    onStatusChanged: if (status === Image.Error && retries < 2) coverRetry.restart()
                                    Timer { id: coverRetry; interval: 1200 + Math.random() * 2400
                                        onTriggered: {
                                            thumbImg.retries += 1
                                            var s = src.modelData.cover || ""
                                            thumbImg.source = ""; thumbImg.source = s
                                        } }
                                }
                            }

                            Column {                          // title · fan-made/status bits
                                anchors.left: thumb.right; anchors.leftMargin: 14
                                anchors.right: srcRight.left; anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6
                                Text { width: parent.width; text: src.modelData.title || ""
                                    color: srcMa.containsMouse && src.dlState !== "dead" ? theme.gold : theme.ink
                                    font.family: theme.display; font.pixelSize: 17; font.weight: Font.Medium
                                    elide: Text.ElideRight }
                                Row {
                                    spacing: 10; visible: subRep.count > 0
                                    Repeater { id: subRep; model: src.subBits()
                                        delegate: Row { spacing: 10
                                            required property var modelData
                                            required property int index
                                            Text { visible: index > 0; text: "·"; color: theme.inkDimmer; opacity: 0.5
                                                font.family: theme.ui; font.pixelSize: 13 }
                                            Text { text: modelData.text; color: modelData.color
                                                font.family: theme.ui; font.pixelSize: 13 }
                                        }
                                    }
                                }
                            }

                            // right side: size · year · state glyph (rail's glyph rules verbatim)
                            Row {
                                id: srcRight
                                anchors.right: parent.right; anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 18
                                Text { text: Gc.sizeText(src.modelData.sizeMB)
                                    visible: text.length > 0
                                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                    anchors.verticalCenter: parent.verticalCenter }
                                Text { text: src.modelData.year > 0 ? String(src.modelData.year) : ""
                                    visible: text.length > 0
                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                                    anchors.verticalCenter: parent.verticalCenter }
                                Item {
                                    width: 36; height: 36
                                    anchors.verticalCenter: parent.verticalCenter
                                    Image {                      // read (downloaded)
                                        anchors.centerIn: parent; visible: src.dlState === "done"
                                        source: "../assets/icons/books.svg"; width: 22; height: 22 }
                                    Text {                       // download progress %
                                        anchors.centerIn: parent; visible: src.inFlight
                                        text: src.dlTotal > 0 ? Math.round(src.dlDone / src.dlTotal * 100) + "%" : "…"
                                        color: theme.gold; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold }
                                    Image {                      // GetComics download
                                        anchors.centerIn: parent
                                        visible: src.dlState !== "done" && src.dlState !== "dead" && !src.inFlight
                                        source: "../assets/icons/download.svg"; width: 21; height: 21
                                        opacity: 0.92 }
                                }
                            }

                            MouseArea {
                                id: srcMa; anchors.fill: parent
                                enabled: src.dlState !== "dead"; hoverEnabled: src.dlState !== "dead"
                                cursorShape: src.dlState !== "dead" ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: src.primary()
                                Accessible.role: Accessible.Button
                                Accessible.name: (src.dlState === "done" ? "Read " : "Download ") + (src.modelData.title || "")
                            }
                        }
                    }
                }
            }
        }
    }
}
