// DownloadsPage — everything the house holds locally, in one full page.
// Ratified design: agents/colosseum-downloads-mock.html (2026-07-04, "go with it").
// Structure IS the information: "Now arriving" (live jobs, cross-world) answers a
// different question than the vault shelves (settled files, world → series → item),
// so they are separate surfaces in that order. Data = LocalDownloads (read-model);
// every action routes back to the owning backend. No sample data — empty lanes
// say so honestly and route to their world.
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    property Item backdrop: null
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal openRequested(var item)           // completed row → host routes by world/kind
    signal openWorldRequested(string world)  // empty-lane CTA → host opens that world

    Theme { id: theme }

    // ---- read-model bindings (revision-driven refresh) ----
    property var jobs: []
    property var laneSeries: ({})      // world -> series list
    property var totalsMap: ({})
    property string openLedgerWorld: ""
    property string openLedgerKey: ""
    property var ledgerItems: []

    readonly property var worlds: [
        { key: "tankoban", title: "Tankoban", unit: "chapters & issues" },
        { key: "biblio",   title: "Biblio",   unit: "books" },
        { key: "theatre",  title: "Theatre",  unit: "files" }
    ]

    function refresh() {
        if (typeof LocalDownloads === "undefined") return;
        jobs = LocalDownloads.activeJobs();
        totalsMap = LocalDownloads.totals;
        var lanes = {};
        for (var i = 0; i < worlds.length; i++)
            lanes[worlds[i].key] = LocalDownloads.series(worlds[i].key);
        laneSeries = lanes;
        if (openLedgerKey.length)
            ledgerItems = LocalDownloads.items(openLedgerWorld, openLedgerKey);
    }

    function toggleLedger(world, key) {
        if (openLedgerWorld === world && openLedgerKey === key) {
            openLedgerWorld = ""; openLedgerKey = ""; ledgerItems = [];
            return;
        }
        openLedgerWorld = world;
        openLedgerKey = key;
        ledgerItems = (typeof LocalDownloads !== "undefined")
                      ? LocalDownloads.items(world, key) : [];
    }

    function fmtBytes(b) {
        if (b >= 1073741824) return (b / 1073741824).toFixed(1) + " GB";
        if (b >= 1048576) return Math.round(b / 1048576) + " MB";
        if (b > 0) return Math.max(1, Math.round(b / 1024)) + " KB";
        return "";
    }
    function fmtWhen(secs) {
        if (!secs) return "";
        var d = new Date(secs * 1000), now = new Date();
        var days = Math.floor((now - d) / 86400000);
        if (days <= 0) return "added today";
        if (days === 1) return "added yesterday";
        return "added " + Qt.formatDate(d, "MMMM d");
    }
    // deterministic quiet cover tones per title (styling, not data)
    function coverTone(title, dark) {
        var h = 0;
        for (var i = 0; i < title.length; i++) h = ((h << 5) - h + title.charCodeAt(i)) | 0;
        var hue = ((h % 360) + 360) % 360;
        return Qt.hsla(hue / 360, 0.22, dark ? 0.10 : 0.22, 1);
    }

    Component.onCompleted: refresh()
    onVisibleChanged: if (visible) refresh()
    Connections {
        target: typeof LocalDownloads !== "undefined" ? LocalDownloads : null
        function onChanged() { root.refresh() }
    }

    MouseArea { anchors.fill: parent }
    Rectangle { anchors.fill: parent; color: "#000000" }

    // ---- live shell wallpaper (the 899a648 pattern) ----
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image { anchors.fill: parent; visible: root.backdrop === null
                source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03,0.04,0.07,0.86) }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 140
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            x: theme.margin
            width: root.width - theme.margin * 2
            topPadding: 14
            spacing: 0

            // ---- header ----
            Text { text: "COLOSSEUM · LOCAL"; color: theme.inkDimmer
                   font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6; font.weight: Font.DemiBold }
            Text { text: "Downloads"; color: theme.ink; topPadding: 8
                   font.family: theme.display; font.pixelSize: 56; font.letterSpacing: -1 }
            Text {
                topPadding: 14
                font.family: theme.display; font.italic: true; font.pixelSize: 18
                color: theme.inkDim
                text: "Everything the house holds — kept locally, ready offline."
            }
            Item { width: 1; height: 20 }
            Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }

            // one quiet inline metrics line — no stat cards, no pills
            Text {
                topPadding: 16
                textFormat: Text.StyledText
                font.family: theme.ui; font.pixelSize: 13
                color: theme.inkDimmer
                text: {
                    var t = root.totalsMap || {};
                    var parts = [
                        "<b><font color='#f7f7f5'>" + (t.items || 0) + "</font></b> items"
                    ];
                    if (t.bytes) parts.push("<b><font color='#f7f7f5'>" + root.fmtBytes(t.bytes) + "</font></b> on disk");
                    parts.push("Tankoban <font color='#c9c8d0'>" + (t.tankoban || 0) + "</font>");
                    parts.push("Biblio <font color='#c9c8d0'>" + (t.biblio || 0) + "</font>");
                    parts.push("Theatre <font color='#c9c8d0'>" + (t.theatre || 0) + "</font>");
                    if (t.active) parts.push("<b><font color='#f0c44a'>" + t.active + " arriving</font></b>");
                    return parts.join("  ·  ");
                }
            }

            // ============ NOW ARRIVING — exists only while jobs run ============
            Column {
                width: col.width
                visible: root.jobs.length > 0
                topPadding: 40
                spacing: 16

                Row {
                    spacing: 14
                    Text { text: "Now arriving"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 28; font.letterSpacing: -0.2 }
                    Text { anchors.baseline: parent.children[0].baseline
                           text: root.jobs.length + (root.jobs.length === 1 ? " live job" : " live jobs")
                                 + "  —  this strip leaves when the last one lands"
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                }

                Flow {
                    width: col.width
                    spacing: 16
                    Repeater {
                        model: root.jobs
                        delegate: Rectangle {
                            id: jobCard
                            required property var modelData
                            width: Math.min(500, (col.width - 32) / Math.min(3, Math.max(1, root.jobs.length)))
                            height: 92
                            radius: 14
                            color: Qt.rgba(0.04, 0.045, 0.065, 0.55)
                            border.width: 1; border.color: theme.edge
                            clip: true

                            Column {
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 16
                                spacing: 5
                                Row {
                                    width: parent.width
                                    Text {
                                        width: parent.width - pctT.width - 10
                                        text: jobCard.modelData.title || "Download"
                                        color: theme.ink; font.family: theme.ui
                                        font.pixelSize: 15; font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        id: pctT
                                        text: jobCard.modelData.state === "queued" ? "queued"
                                            : jobCard.modelData.state === "resolving" ? "resolving"
                                            : jobCard.modelData.state === "extracting" ? "unpacking"
                                            : jobCard.modelData.state === "failed" ? "failed"
                                            : Math.round((jobCard.modelData.ratio || 0) * 100) + "%"
                                        color: jobCard.modelData.state === "downloading" ? theme.gold : theme.inkDimmer
                                        font.family: theme.ui; font.pixelSize: 13
                                    }
                                }
                                Text {
                                    text: {
                                        var world = jobCard.modelData.world || "";
                                        var w = world === "tankoban" ? "Tankoban" : world === "biblio" ? "Biblio" : "Theatre";
                                        if (jobCard.modelData.state === "failed")
                                            return w + " · " + (jobCard.modelData.error || "download failed");
                                        var d = jobCard.modelData.detail || "";
                                        return d.length ? (w + " · " + d) : w;
                                    }
                                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                }
                            }
                            Row {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.margins: 12
                                spacing: 16
                                Text {
                                    visible: jobCard.modelData.canRetry === true
                                    text: "Retry"
                                    color: retryMa.containsMouse ? "#ffd968" : theme.gold
                                    font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                                    MouseArea { id: retryMa; anchors.fill: parent; hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: LocalDownloads.retry(jobCard.modelData.world, jobCard.modelData.id) }
                                }
                                Text {
                                    text: jobCard.modelData.state === "failed" ? "Remove" : "Cancel"
                                    color: cancelMa.containsMouse ? theme.ink : theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 12
                                    MouseArea { id: cancelMa; anchors.fill: parent; hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: LocalDownloads.cancel(jobCard.modelData.world, jobCard.modelData.id) }
                                }
                            }
                            // gold lives on the bottom edge: the fill IS the progress
                            Rectangle {
                                anchors.left: parent.left; anchors.bottom: parent.bottom
                                width: parent.width * (jobCard.modelData.ratio || 0)
                                height: 3
                                color: theme.gold
                                visible: jobCard.modelData.state === "downloading"
                            }
                        }
                    }
                }
            }

            // ============ WORLD SHELVES ============
            Repeater {
                model: root.worlds
                delegate: Column {
                    id: lane
                    required property var modelData
                    readonly property var laneList: root.laneSeries[lane.modelData.key] || []
                    readonly property bool ledgerHere: root.openLedgerWorld === lane.modelData.key
                                                       && root.openLedgerKey.length > 0
                    width: col.width
                    topPadding: 48
                    spacing: 16

                    Row {
                        spacing: 14
                        Text { text: lane.modelData.title; color: theme.ink
                               font.family: theme.display; font.pixelSize: 28; font.letterSpacing: -0.2 }
                        Text {
                            anchors.baseline: parent.children[0].baseline
                            textFormat: Text.StyledText
                            text: {
                                var n = 0, bytes = 0;
                                for (var i = 0; i < lane.laneList.length; i++) {
                                    n += lane.laneList[i].itemCount;
                                    bytes += lane.laneList[i].bytes;
                                }
                                var s = "<b><font color='#f7f7f5'>" + n + "</font></b> " + lane.modelData.unit;
                                if (bytes) s += " · " + root.fmtBytes(bytes);
                                return s;
                            }
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                        }
                    }

                    Rectangle {
                        width: lane.width
                        implicitHeight: shelfCol.implicitHeight + 52
                        radius: 18
                        color: Qt.rgba(0.04, 0.045, 0.065, 0.48)
                        border.width: 1; border.color: theme.edge

                        Column {
                            id: shelfCol
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 26
                            spacing: 0

                            // honest empty lane
                            Column {
                                visible: lane.laneList.length === 0
                                spacing: 12
                                Text {
                                    text: "Nothing from " + lane.modelData.title + " lives here yet."
                                    color: theme.inkDim
                                    font.family: theme.display; font.italic: true; font.pixelSize: 19
                                }
                                Text {
                                    text: "Open " + lane.modelData.title + " and pick something ›"
                                    color: goMa.containsMouse ? "#ffd968" : theme.gold
                                    font.family: theme.ui; font.pixelSize: 14
                                    MouseArea { id: goMa; anchors.fill: parent; hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.openWorldRequested(lane.modelData.key) }
                                }
                            }

                            // series rail
                            Flickable {
                                visible: lane.laneList.length > 0
                                width: shelfCol.width
                                height: 214
                                contentWidth: railRow.width
                                contentHeight: height
                                clip: true
                                flickableDirection: Flickable.HorizontalFlick
                                boundsBehavior: Flickable.StopAtBounds
                                Row {
                                    id: railRow
                                    spacing: 16
                                    Repeater {
                                        model: lane.laneList
                                        delegate: Item {
                                            id: card
                                            required property var modelData
                                            readonly property bool on: root.openLedgerWorld === lane.modelData.key
                                                                       && root.openLedgerKey === card.modelData.key
                                            width: 148; height: 214

                                            Rectangle {
                                                id: cover
                                                width: 148; height: 198
                                                radius: 12
                                                border.width: card.on ? 2 : 1
                                                border.color: card.on ? Qt.rgba(0.94, 0.77, 0.29, 0.65) : theme.edge
                                                gradient: Gradient {
                                                    GradientStop { position: 0; color: root.coverTone(card.modelData.title || "", false) }
                                                    GradientStop { position: 1; color: root.coverTone(card.modelData.title || "", true) }
                                                }
                                                Image {
                                                    anchors.fill: parent
                                                    visible: (card.modelData.art || "").length > 0
                                                    source: card.modelData.art || ""
                                                    fillMode: Image.PreserveAspectCrop
                                                    opacity: status === Image.Ready ? 1 : 0
                                                }
                                                Rectangle { // readability foot
                                                    anchors.left: parent.left; anchors.right: parent.right
                                                    anchors.bottom: parent.bottom
                                                    height: parent.height * 0.55
                                                    radius: 12
                                                    gradient: Gradient {
                                                        GradientStop { position: 0; color: "transparent" }
                                                        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.78) }
                                                    }
                                                }
                                                Text {
                                                    anchors.left: parent.left; anchors.right: parent.right
                                                    anchors.bottom: parent.bottom
                                                    anchors.leftMargin: 12; anchors.rightMargin: 10; anchors.bottomMargin: 30
                                                    text: card.modelData.title || "Untitled"
                                                    color: theme.ink; font.family: theme.ui
                                                    font.pixelSize: 14; font.weight: Font.DemiBold
                                                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                                }
                                                Text {
                                                    anchors.left: parent.left; anchors.right: parent.right
                                                    anchors.bottom: parent.bottom
                                                    anchors.leftMargin: 12; anchors.rightMargin: 10; anchors.bottomMargin: 10
                                                    textFormat: Text.StyledText
                                                    text: "<b><font color='#f7f7f5'>" + card.modelData.itemCount + "</font></b> "
                                                          + (card.modelData.kind === "book" ? "edition"
                                                             + (card.modelData.itemCount === 1 ? "" : "s")
                                                             : card.modelData.kind === "comic" ? "issues · western"
                                                             : card.modelData.kind === "manga" ? "chapters · manga"
                                                             : card.modelData.kind === "episode" ? "episodes"
                                                             : "film")
                                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11
                                                }
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.toggleLedger(lane.modelData.key, card.modelData.key)
                                            }
                                        }
                                    }
                                }
                            }

                            // expanded item ledger
                            Column {
                                visible: lane.ledgerHere
                                width: shelfCol.width
                                topPadding: 18
                                spacing: 0

                                Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.10) }

                                Repeater {
                                    model: lane.ledgerHere ? root.ledgerItems : []
                                    delegate: Item {
                                        id: row
                                        required property var modelData
                                        width: shelfCol.width
                                        height: 58

                                        Rectangle {
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 1; color: Qt.rgba(1, 1, 1, 0.06)
                                        }
                                        Text {
                                            id: markT
                                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                            width: 22
                                            text: row.modelData.missing ? "✕" : "✓"
                                            color: row.modelData.missing ? theme.inkDimmer : theme.inkDim
                                            font.family: theme.ui; font.pixelSize: 13
                                        }
                                        Column {
                                            anchors.left: markT.right; anchors.leftMargin: 14
                                            anchors.right: actRow.left; anchors.rightMargin: 16
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 3
                                            Text {
                                                width: parent.width
                                                text: row.modelData.title || "Untitled"
                                                color: row.modelData.missing ? theme.inkDim : theme.ink
                                                font.family: theme.ui; font.pixelSize: 15; font.weight: Font.Medium
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                width: parent.width
                                                text: {
                                                    if (row.modelData.missing)
                                                        return "the file left the disk outside the app — remove the entry or fetch it again";
                                                    var parts = [];
                                                    if (row.modelData.subtitle) parts.push(row.modelData.subtitle);
                                                    var b = root.fmtBytes(row.modelData.bytes || 0);
                                                    if (b) parts.push(b);
                                                    var w = root.fmtWhen(row.modelData.addedAt || 0);
                                                    if (w) parts.push(w);
                                                    return parts.join(" · ");
                                                }
                                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                                elide: Text.ElideRight
                                            }
                                        }
                                        Row {
                                            id: actRow
                                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                            spacing: 22
                                            Text {
                                                visible: !row.modelData.missing
                                                text: row.modelData.world === "theatre" ? "Play" : "Read"
                                                color: openMa.containsMouse ? "#ffd968" : theme.gold
                                                font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                                                MouseArea { id: openMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: root.openRequested(row.modelData) }
                                            }
                                            Text {
                                                text: "Remove"
                                                color: rmMa.containsMouse ? theme.ink : theme.inkDimmer
                                                font.family: theme.ui; font.pixelSize: 13
                                                MouseArea { id: rmMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: {
                                                                LocalDownloads.remove(row.modelData.world, row.modelData.id)
                                                                root.refresh()
                                                            } }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Item { width: 1; height: 40 }
        }
    }

    // ---- fixed back / system controls (mirrors GenrePage) ----
    Item {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 52; z: 30
        Rectangle {
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 22
            width: 42; height: 34; radius: 17
            color: backMa.hovered ? Qt.rgba(1,1,1,0.18) : Qt.rgba(0,0,0,0.40)
            Text { anchors.centerIn: parent; text: "‹"; color: theme.ink; font.pixelSize: 22 }
            HoverHandler { id: backMa }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.backRequested() }
        }
        Row {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 26
            spacing: 20
            Image { source: "../assets/icons/search.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.searchClicked() } }
            Image { source: "../assets/icons/minimize.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Image { source: "../assets/icons/power.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() } }
        }
    }
}
