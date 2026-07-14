// ExtensionsPage — the extension store: one page where Stremio-protocol addons
// are discovered, browsed, installed, toggled, ordered and removed.
// Ratified design: agents/colosseum-extensions-mock.html (2026-07-05, "we can go
// for it"), spec: docs/superpowers/specs/2026-07-05-colosseum-extensions-store-design.md.
// Serves all three worlds — Theatre live in v1; Tankoban and Biblio get honest
// designed empty states, never a blank. Data = `Extensions` (the C++ registry)
// + ExtensionsCatalog.js (curated rails, community registry, adult wall).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "ExtensionsCatalog.js" as Catalog

Item {
    id: root
    property Item backdrop: null
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()

    Theme { id: theme }

    // ---- registry bindings ----
    property var installedList: []
    property var installedKeys: ({})          // id AND transportUrl → true
    property var pendingUrls: ({})            // url → true while an install is in flight
    property string notice: ""                // one quiet line for install results

    // ---- page state ----
    property string world: "theatre"          // "theatre" | "tankoban" | "biblio"
    property string pane: "discover"          // "discover" | "browse" | "installed"
    property string query: ""
    property string sort: "top"               // "top" | "new" | "rising"
    property var communityRows: []
    property bool communityLoading: false
    property bool communityLoaded: false

    function refresh() {
        if (typeof Extensions === "undefined") return;
        installedList = Extensions.installed();
        var keys = {};
        for (var i = 0; i < installedList.length; i++) {
            keys[installedList[i].id] = true;
            keys[installedList[i].transportUrl] = true;
        }
        installedKeys = keys;
    }
    function carried(item) {
        return installedKeys[item.id] === true || installedKeys[item.url] === true;
    }
    function hit(name) {
        return !query.length || name.toLowerCase().indexOf(query.toLowerCase()) !== -1;
    }
    function installFromCard(item) {
        if (typeof Extensions === "undefined" || carried(item)) return;
        var p = {};
        for (var k in pendingUrls) p[k] = true;
        p[item.url] = true;
        pendingUrls = p;
        Extensions.install(item.url);
    }
    function loadCommunity() {
        communityLoading = true;
        var mySort = sort, myQuery = query;
        Catalog.browse(mySort, myQuery, function(rows) {
            if (mySort !== root.sort || myQuery !== root.query) return; // stale answer
            root.communityRows = rows || [];
            root.communityLoading = false;
            root.communityLoaded = true;
        });
    }

    Component.onCompleted: refresh()
    Connections {
        target: typeof Extensions !== "undefined" ? Extensions : null
        function onChanged() { root.refresh() }
        function onInstallFinished(id, name) {
            root.notice = name + " installed — it answers from the next ask on.";
            var p = {};
            for (var k in root.pendingUrls) p[k] = true;   // clear all; refresh covers state
            root.pendingUrls = {};
            noticeTimer.restart();
        }
        function onInstallFailed(url, reason) {
            root.notice = reason;
            root.pendingUrls = {};
            noticeTimer.restart();
        }
    }
    Timer { id: noticeTimer; interval: 6000; onTriggered: root.notice = "" }

    // community loads when Browse first opens, and reloads on sort/search change
    onPaneChanged: if (pane === "browse" && !communityLoaded && !communityLoading) loadCommunity()
    onSortChanged: if (pane === "browse") loadCommunity()
    Timer {
        id: queryDebounce
        interval: 450
        onTriggered: if (root.pane === "browse") root.loadCommunity()
    }
    onQueryChanged: queryDebounce.restart()

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
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03, 0.04, 0.07, 0.86) }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 150
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }

        Column {
            id: col
            x: theme.margin
            width: root.width - theme.margin * 2
            topPadding: 14
            spacing: 0

            // ---- header ----
            Text { text: "COLOSSEUM · STORE"; color: theme.inkDimmer
                   font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6; font.weight: Font.DemiBold }
            Text { text: "Extensions"; color: theme.ink; topPadding: 8
                   font.family: theme.display; font.pixelSize: 56; font.letterSpacing: -1 }
            Text {
                topPadding: 14
                font.family: theme.display; font.italic: true; font.pixelSize: 18
                color: theme.inkDim
                text: "New rows, new sources, new subtitles — the house grows by invitation."
            }
            Item { width: 1; height: 20 }
            Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }

            Text {
                topPadding: 16
                textFormat: Text.StyledText
                font.family: theme.ui; font.pixelSize: 13
                color: theme.inkDimmer
                text: {
                    var n = root.installedList.length;
                    var on = 0;
                    for (var i = 0; i < root.installedList.length; i++)
                        if (root.installedList[i].enabled) on++;
                    return "<b><font color='#f7f7f5'>" + n + "</font></b> installed"
                         + "  ·  " + (on === n ? "all carrying" : on + " carrying")
                         + "  ·  Theatre <font color='#c9c8d0'>" + n + "</font>"
                         + "  ·  Tankoban <font color='#c9c8d0'>—</font>"
                         + "  ·  Biblio <font color='#c9c8d0'>—</font>";
                }
            }

            // ---- worlds row: the three houses this store serves ----
            Row {
                topPadding: 34
                spacing: 34
                Repeater {
                    model: [
                        { key: "theatre", title: "Theatre", live: true },
                        { key: "tankoban", title: "Tankoban", live: false },
                        { key: "biblio", title: "Biblio", live: false }
                    ]
                    delegate: Item {
                        id: worldTab
                        required property var modelData
                        implicitWidth: worldRow.implicitWidth
                        implicitHeight: worldRow.implicitHeight + 12
                        Row {
                            id: worldRow
                            spacing: 7
                            Text {
                                text: worldTab.modelData.title
                                color: root.world === worldTab.modelData.key ? theme.ink
                                     : worldMa.containsMouse ? theme.inkDim : theme.inkDimmer
                                font.family: theme.display; font.pixelSize: 24
                            }
                            Text {
                                visible: !worldTab.modelData.live
                                anchors.top: parent.top
                                text: "arrives later"
                                color: theme.inkDimmer
                                font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 0.4
                            }
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: worldRow.implicitWidth; height: 3; radius: 2
                            color: theme.gold
                            visible: root.world === worldTab.modelData.key
                        }
                        MouseArea {
                            id: worldMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.world = worldTab.modelData.key
                        }
                    }
                }
            }

            // =================== THEATRE — the live store ===================
            Column {
                width: col.width
                visible: root.world === "theatre"
                spacing: 0

                // ---- pane tabs + ONE global search + install-from-link ----
                Item {
                    width: col.width
                    height: 46
                    Row {
                        id: paneTabs
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 26
                        Repeater {
                            model: [
                                { key: "discover", label: "Discover" },
                                { key: "browse", label: "Browse everything" },
                                { key: "installed", label: "Installed · " }
                            ]
                            delegate: Item {
                                id: paneTab
                                required property var modelData
                                implicitWidth: paneLabel.implicitWidth
                                implicitHeight: paneLabel.implicitHeight + 9
                                Text {
                                    id: paneLabel
                                    text: paneTab.modelData.key === "installed"
                                          ? paneTab.modelData.label + root.installedList.length
                                          : paneTab.modelData.label
                                    color: root.pane === paneTab.modelData.key ? theme.ink
                                         : paneMa.containsMouse ? theme.inkDim : theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 14
                                    font.weight: root.pane === paneTab.modelData.key ? Font.DemiBold : Font.Normal
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: paneLabel.implicitWidth; height: 2; radius: 1
                                    color: theme.gold
                                    visible: root.pane === paneTab.modelData.key
                                }
                                MouseArea {
                                    id: paneMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.pane = paneTab.modelData.key
                                }
                            }
                        }
                    }

                    // one search bar for the whole store — filters whichever room you're in
                    Rectangle {
                        id: searchBox
                        anchors.right: addLink.left
                        anchors.rightMargin: 24
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(280, col.width * 0.28)
                        height: 38
                        radius: 12
                        color: theme.glassTint
                        border.width: 1
                        border.color: searchInput.activeFocus ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 13
                            anchors.rightMargin: 13
                            spacing: 9
                            Text { anchors.verticalCenter: parent.verticalCenter
                                   text: "⌕"; color: theme.inkDimmer; font.pixelSize: 15 }
                            TextInput {
                                id: searchInput
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 30
                                color: theme.ink
                                font.family: theme.ui; font.pixelSize: 14
                                clip: true
                                onTextChanged: root.query = text
                                Text {
                                    visible: !searchInput.text.length && !searchInput.activeFocus
                                    text: "Search extensions…"
                                    color: theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 14
                                }
                            }
                        }
                    }
                    Text {
                        id: addLink
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Install from a link ›"
                        color: addMa.containsMouse ? "#ffd968" : theme.gold
                        font.family: theme.ui; font.pixelSize: 14
                        MouseArea {
                            id: addMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sheet.openSheet()
                        }
                    }
                }

                Item { width: 1; height: 26 }

                // ============ DISCOVER ============
                Column {
                    width: col.width
                    visible: root.pane === "discover"
                    spacing: 0

                    // featured slab — steps aside while a search is on
                    Rectangle {
                        width: col.width
                        height: 168
                        visible: !root.query.length
                        radius: 20
                        border.width: 1; border.color: theme.edge
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: "#1a1c2c" }
                            GradientStop { position: 0.55; color: "#10111c" }
                            GradientStop { position: 1.0; color: "#0b0c14" }
                        }
                        Row {
                            anchors.fill: parent
                            anchors.margins: 30
                            spacing: 30
                            Item {
                                width: 96; height: 96
                                anchors.verticalCenter: parent.verticalCenter
                                AddonLogo {
                                    anchors.centerIn: parent
                                    addonId: Catalog.featured().id
                                    addonName: Catalog.featured().name
                                    size: 96; radius: 22
                                    tone1: "#2d2a1c"; tone2: "#181405"
                                }
                                Rectangle {   // gold ring — the featured accent
                                    anchors.fill: parent; color: "transparent"; radius: 22
                                    border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.4)
                                }
                            }
                            Column {
                                width: parent.width - 96 - 180 - 60
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6
                                Text { text: "FEATURED EXTENSION"; color: theme.inkDimmer
                                       font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 2.2 }
                                Text { text: Catalog.featured().name; color: theme.ink
                                       font.family: theme.display; font.pixelSize: 32 }
                                Text {
                                    width: parent.width
                                    text: Catalog.featured().line
                                    color: theme.inkDim
                                    font.family: theme.display; font.italic: true; font.pixelSize: 16
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    width: parent.width
                                    text: Catalog.featured().facts
                                    color: theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }
                            Column {
                                width: 180
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 5
                                Text {
                                    anchors.right: parent.right
                                    text: "Installed"
                                    color: theme.gold
                                    font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                }
                                Text {
                                    anchors.right: parent.right
                                    text: "built-in"
                                    color: theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 12
                                }
                            }
                        }
                    }

                    // curated rails
                    Repeater {
                        model: Catalog.rails()
                        delegate: Column {
                            id: rail
                            required property var modelData
                            width: col.width
                            spacing: 0
                            visible: cardsRow.visibleCount > 0

                            Item { width: 1; height: 44 }
                            Row {
                                spacing: 14
                                Text { text: rail.modelData.title; color: theme.ink
                                       font.family: theme.display; font.pixelSize: 26 }
                                Text {
                                    anchors.baseline: parent.children[0].baseline
                                    text: rail.modelData.count
                                          + (rail.modelData.hint ? "  —  " + rail.modelData.hint : "")
                                    color: theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 13
                                }
                            }
                            Item { width: 1; height: 16 }

                            Flickable {
                                width: col.width
                                height: 196
                                contentWidth: cardsRow.implicitWidth
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                flickableDirection: Flickable.HorizontalFlick
                                Row {
                                    id: cardsRow
                                    property int visibleCount: {
                                        var n = 0;
                                        for (var i = 0; i < rail.modelData.items.length; i++)
                                            if (root.hit(rail.modelData.items[i].name)) n++;
                                        return n;
                                    }
                                    spacing: 14
                                    Repeater {
                                        model: rail.modelData.items
                                        delegate: Rectangle {
                                            id: card
                                            required property var modelData
                                            visible: root.hit(card.modelData.name)
                                            width: 236; height: 188
                                            radius: 16
                                            color: cardMa.containsMouse ? Qt.rgba(0.06, 0.065, 0.09, 0.65)
                                                                        : Qt.rgba(0.04, 0.045, 0.065, 0.55)
                                            border.width: 1
                                            border.color: cardMa.containsMouse ? Qt.rgba(1, 1, 1, 0.28) : theme.edge

                                            Column {
                                                anchors.fill: parent
                                                anchors.margins: 18
                                                spacing: 0
                                                AddonLogo {
                                                    addonId: card.modelData.id
                                                    addonName: card.modelData.name
                                                    size: 42; radius: 11
                                                    tone1: card.modelData.tone1
                                                    tone2: card.modelData.tone2
                                                }
                                                Item { width: 1; height: 12 }
                                                Text { text: card.modelData.name; color: theme.ink
                                                       font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold }
                                                Item { width: 1; height: 5 }
                                                Text {
                                                    width: parent.width
                                                    text: card.modelData.desc
                                                    color: theme.inkDimmer
                                                    font.family: theme.ui; font.pixelSize: 12
                                                    wrapMode: Text.WordWrap
                                                    maximumLineCount: 2
                                                    elide: Text.ElideRight
                                                }
                                                Item { width: 1; height: 9 }
                                                Text { text: card.modelData.kind; color: theme.inkDim
                                                       font.family: theme.ui; font.pixelSize: 12 }
                                            }
                                            MouseArea {
                                                id: cardMa
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                acceptedButtons: Qt.NoButton
                                            }
                                            // status tag — pinned to the tile's bottom-right corner
                                            Text {
                                                anchors.right: parent.right
                                                anchors.bottom: parent.bottom
                                                anchors.rightMargin: 18
                                                anchors.bottomMargin: 16
                                                text: card.modelData.core ? "Built-in"
                                                    : root.carried(card.modelData) ? "Installed"
                                                    : root.pendingUrls[card.modelData.url] ? "Installing…"
                                                    : "Install"
                                                color: card.modelData.core || root.carried(card.modelData)
                                                       || root.pendingUrls[card.modelData.url]
                                                       ? theme.inkDimmer
                                                       : verbMa.containsMouse ? "#ffd968" : theme.gold
                                                font.family: theme.ui; font.pixelSize: 13
                                                font.weight: root.carried(card.modelData) ? Font.Normal : Font.DemiBold
                                                MouseArea {
                                                    id: verbMa
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    enabled: !card.modelData.core && !root.carried(card.modelData)
                                                    onClicked: root.installFromCard(card.modelData)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ============ BROWSE ============
                Column {
                    width: col.width
                    visible: root.pane === "browse"
                    spacing: 0

                    Row {
                        spacing: 22
                        Repeater {
                            model: [
                                { key: "top", label: "Top" },
                                { key: "new", label: "New" },
                                { key: "rising", label: "Rising" }
                            ]
                            delegate: Item {
                                id: sortTab
                                required property var modelData
                                implicitWidth: sortLabel.implicitWidth
                                implicitHeight: sortLabel.implicitHeight + 7
                                Text {
                                    id: sortLabel
                                    text: sortTab.modelData.label
                                    color: root.sort === sortTab.modelData.key ? theme.ink
                                         : sortMa.containsMouse ? theme.inkDim : theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 14
                                    font.weight: root.sort === sortTab.modelData.key ? Font.DemiBold : Font.Normal
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: sortLabel.implicitWidth; height: 2; radius: 1
                                    color: theme.gold
                                    visible: root.sort === sortTab.modelData.key
                                }
                                MouseArea {
                                    id: sortMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.sort = sortTab.modelData.key
                                }
                            }
                        }
                        Text {
                            leftPadding: 12
                            text: "a thousand community extensions · streams, catalogs, subtitles, live tv, tools"
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 13
                        }
                    }

                    Item { width: 1; height: 18 }

                    Rectangle {
                        width: col.width
                        radius: 18
                        color: Qt.rgba(0.04, 0.045, 0.065, 0.48)
                        border.width: 1; border.color: theme.edge
                        height: communityCol.implicitHeight + 20

                        Column {
                            id: communityCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10
                            anchors.leftMargin: 28
                            anchors.rightMargin: 28

                            Text {
                                visible: root.communityLoading
                                topPadding: 24; bottomPadding: 24
                                text: "Asking the registry…"
                                color: theme.inkDim
                                font.family: theme.display; font.italic: true; font.pixelSize: 16
                            }
                            Text {
                                visible: !root.communityLoading && root.communityRows.length === 0
                                topPadding: 24; bottomPadding: 24
                                text: root.query.length
                                      ? "Nothing in the registry matches “" + root.query + "”."
                                      : "The registry didn’t answer. Try again in a moment."
                                color: theme.inkDim
                                font.family: theme.display; font.italic: true; font.pixelSize: 16
                            }

                            Repeater {
                                model: root.communityRows
                                delegate: Item {
                                    id: crow
                                    required property var modelData
                                    required property int index
                                    width: communityCol.width
                                    height: 72
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width; height: 1
                                        color: Qt.rgba(1, 1, 1, 0.06)
                                        visible: crow.index < root.communityRows.length - 1
                                    }
                                    Row {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width
                                        spacing: 18
                                        Text {
                                            width: 24
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: crow.index + 1
                                            color: theme.inkDimmer
                                            font.family: theme.display; font.pixelSize: 16
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        AddonLogo {
                                            anchors.verticalCenter: parent.verticalCenter
                                            addonId: crow.modelData.id
                                            addonName: crow.modelData.name
                                            manifestLogo: crow.modelData.logo || ""
                                            size: 40; radius: 10
                                            tone1: crow.modelData.tone1
                                            tone2: crow.modelData.tone2
                                        }
                                        Column {
                                            width: parent.width - 24 - 40 - 120 - 110 - 18 * 4
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 3
                                            Text { text: crow.modelData.name; color: theme.ink
                                                   font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                                   elide: Text.ElideRight; width: parent.width }
                                            Text {
                                                width: parent.width
                                                text: crow.modelData.kind
                                                      + (crow.modelData.desc ? " · " + crow.modelData.desc : "")
                                                color: theme.inkDimmer
                                                font.family: theme.ui; font.pixelSize: 12
                                                elide: Text.ElideRight
                                            }
                                        }
                                        Text {
                                            width: 120
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: crow.modelData.stars > 0
                                            text: "★ " + crow.modelData.stars
                                            color: theme.inkDim
                                            font.family: theme.ui; font.pixelSize: 12
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Text {
                                            width: 110
                                            anchors.verticalCenter: parent.verticalCenter
                                            horizontalAlignment: Text.AlignRight
                                            text: root.carried(crow.modelData) ? "Installed"
                                                : root.pendingUrls[crow.modelData.url] ? "Installing…"
                                                : "Install"
                                            color: root.carried(crow.modelData) || root.pendingUrls[crow.modelData.url]
                                                   ? theme.inkDimmer
                                                   : crowVerbMa.containsMouse ? "#ffd968" : theme.gold
                                            font.family: theme.ui; font.pixelSize: 13
                                            font.weight: root.carried(crow.modelData) ? Font.Normal : Font.DemiBold
                                            MouseArea {
                                                id: crowVerbMa
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                enabled: !root.carried(crow.modelData)
                                                onClicked: root.installFromCard(crow.modelData)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        topPadding: 14
                        text: "Adult extensions are not carried in this store — by the house’s rule, not a toggle."
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 12
                    }
                }

                // ============ INSTALLED ============
                Column {
                    width: col.width
                    visible: root.pane === "installed"
                    spacing: 0

                    Rectangle {
                        width: col.width
                        radius: 18
                        color: Qt.rgba(0.04, 0.045, 0.065, 0.48)
                        border.width: 1; border.color: theme.edge
                        height: installedCol.implicitHeight + 20

                        Column {
                            id: installedCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10
                            anchors.leftMargin: 28
                            anchors.rightMargin: 28

                            Repeater {
                                model: root.installedList
                                delegate: Item {
                                    id: irow
                                    required property var modelData
                                    required property int index
                                    property var manifest: irow.modelData.manifest || ({})
                                    property bool isCore: irow.modelData.core === true
                                    property bool isOn: irow.modelData.enabled === true
                                    property bool configurable: (irow.manifest.behaviorHints || {}).configurable === true
                                    visible: root.hit(irow.manifest.name || irow.modelData.id)
                                    width: installedCol.width
                                    height: 82

                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width; height: 1
                                        color: Qt.rgba(1, 1, 1, 0.06)
                                        visible: irow.index < root.installedList.length - 1
                                    }
                                    MouseArea {
                                        id: irowMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.NoButton
                                    }

                                    Row {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width
                                        spacing: 18

                                        // move up / down — the ask-order controls
                                        Column {
                                            width: 18
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 4
                                            opacity: irowMa.containsMouse ? 1 : 0.25
                                            Text {
                                                text: "▲"; font.pixelSize: 10
                                                color: upMa.containsMouse ? theme.ink : theme.inkDimmer
                                                MouseArea { id: upMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: Extensions.move(irow.modelData.id, -1) }
                                            }
                                            Text {
                                                text: "▼"; font.pixelSize: 10
                                                color: downMa.containsMouse ? theme.ink : theme.inkDimmer
                                                MouseArea { id: downMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: Extensions.move(irow.modelData.id, 1) }
        }
    }

    ScrollGlide { flick: page }

    AddonLogo {
                                            anchors.verticalCenter: parent.verticalCenter
                                            opacity: irow.isOn ? 1 : 0.45
                                            addonId: irow.manifest.id || irow.modelData.id
                                            addonName: irow.manifest.name || irow.modelData.id
                                            manifestLogo: irow.manifest.logo || ""
                                            size: 44; radius: 11
                                        }

                                        Column {
                                            width: parent.width - 18 - 44 - 300 - 18 * 3
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 4
                                            opacity: irow.isOn ? 1 : 0.45
                                            Row {
                                                spacing: 10
                                                Text { text: irow.manifest.name || irow.modelData.id
                                                       color: theme.ink; font.family: theme.ui
                                                       font.pixelSize: 15; font.weight: Font.DemiBold }
                                                Text {
                                                    visible: irow.isCore
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: "THE HOUSE CATALOG"
                                                    color: theme.inkDimmer
                                                    font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 0.6
                                                }
                                            }
                                            Text {
                                                width: parent.width
                                                text: irow.manifest.description || irow.modelData.transportUrl
                                                color: theme.inkDimmer
                                                font.family: theme.ui; font.pixelSize: 12
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Row {
                                            width: 300
                                            anchors.verticalCenter: parent.verticalCenter
                                            layoutDirection: Qt.RightToLeft
                                            spacing: 22

                                            // the on/off switch: gold when carrying
                                            Rectangle {
                                                width: 40; height: 22; radius: 11
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: irow.isOn ? Qt.rgba(0.94, 0.77, 0.29, 0.85)
                                                                 : Qt.rgba(1, 1, 1, 0.12)
                                                border.width: 1
                                                border.color: irow.isOn ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge
                                                opacity: irow.isCore ? 0.5 : 1
                                                Rectangle {
                                                    width: 16; height: 16; radius: 8
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    x: irow.isOn ? 20 : 2
                                                    color: irow.isOn ? "#141207" : theme.inkDim
                                                    Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: irow.isCore ? Qt.ArrowCursor : Qt.PointingHandCursor
                                                    onClicked: if (!irow.isCore)
                                                                   Extensions.setEnabled(irow.modelData.id, !irow.isOn)
                                                }
                                            }
                                            Text {
                                                visible: !irow.isCore
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: "Remove"
                                                color: rmMa.containsMouse ? theme.ink : theme.inkDimmer
                                                font.family: theme.ui; font.pixelSize: 13
                                                MouseArea { id: rmMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: Extensions.remove(irow.modelData.id) }
                                            }
                                            Text {
                                                visible: irow.configurable
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: "Configure"
                                                color: cfgMa.containsMouse ? theme.ink : theme.inkDim
                                                font.family: theme.ui; font.pixelSize: 13
                                                MouseArea {
                                                    id: cfgMa; anchors.fill: parent; hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: Qt.openUrlExternally(
                                                        irow.modelData.transportUrl.replace(/manifest\.json$/i, "configure"))
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        topPadding: 14
                        text: "Order matters: when you press play, sources are asked in this order, top first."
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 12
                    }
                }
            }

            // =================== TANKOBAN / BIBLIO — honest empty ===================
            Rectangle {
                width: col.width
                visible: root.world !== "theatre"
                radius: 18
                color: Qt.rgba(0.04, 0.045, 0.065, 0.48)
                border.width: 1; border.color: theme.edge
                height: emptyCol.implicitHeight + 76
                Column {
                    id: emptyCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 38
                    spacing: 12
                    Text {
                        text: root.world === "tankoban"
                              ? "No extensions live in Tankoban yet."
                              : "No extensions live in Biblio yet."
                        color: theme.inkDim
                        font.family: theme.display; font.italic: true; font.pixelSize: 20
                    }
                    Text {
                        width: parent.width * 0.7
                        text: root.world === "tankoban"
                              ? "The store opens with Theatre first. When the comics lane is ready for guests, its sources — catalogs, download wells, metadata — will install from this same page, the same way."
                              : "Books keep their one trusted source for now. When Biblio is ready to take recommendations, new shelves and download wells will arrive here."
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 14
                        wrapMode: Text.WordWrap
                        lineHeight: 1.35
                    }
                    Text {
                        text: "See what Theatre’s store looks like ›"
                        color: goMa.containsMouse ? "#ffd968" : theme.gold
                        font.family: theme.ui; font.pixelSize: 14
                        MouseArea { id: goMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.world = "theatre" }
                    }
                }
            }
        }
    }

    // ---- top chrome: back · minimize · power (fullscreen-only vocabulary) ----
    Item {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 24
        anchors.rightMargin: theme.margin
        width: chromeRow.implicitWidth
        height: 30
        Row {
            id: chromeRow
            spacing: 22
            Text { text: "⌕"; color: sMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: sMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.searchClicked() } }
            Text { text: "—"; color: mMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: mMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Text { text: "⏻"; color: pMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: pMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() } }
        }
    }
    BackAction {
        variant: "capsule"; tip: "Back"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 21
        anchors.leftMargin: theme.margin - 10
        onTriggered: root.backRequested()
    }

    // ---- one quiet notice line (install results) ----
    Rectangle {
        visible: root.notice.length > 0
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 92
        width: noticeT.implicitWidth + 44
        height: 42
        radius: 12
        color: Qt.rgba(0.05, 0.055, 0.08, 0.92)
        border.width: 1; border.color: theme.edge
        Text { id: noticeT; anchors.centerIn: parent
               text: root.notice; color: theme.inkDim
               font.family: theme.ui; font.pixelSize: 13 }
    }

    // ---- install-from-link sheet ----
    Rectangle {
        id: sheet
        anchors.fill: parent
        color: Qt.rgba(0.015, 0.02, 0.035, 0.62)
        visible: false

        property string previewUrl: ""
        property var previewManifest: null
        property string error: ""
        property bool checking: false

        function openSheet() {
            previewUrl = ""; previewManifest = null; error = ""; checking = false;
            urlInput.text = "";
            visible = true;
            urlInput.forceActiveFocus();
        }
        function closeSheet() { visible = false }
        function check() {
            if (!urlInput.text.trim().length) return;
            error = ""; previewManifest = null; checking = true;
            previewUrl = Extensions.normalizeUrl(urlInput.text);
            Extensions.preview(urlInput.text);
        }

        Connections {
            target: typeof Extensions !== "undefined" ? Extensions : null
            function onPreviewReady(url, manifest) {
                if (!sheet.visible || url !== sheet.previewUrl) return;
                sheet.previewManifest = manifest;
                sheet.checking = false;
            }
            function onPreviewFailed(url, reason) {
                if (!sheet.visible || url !== sheet.previewUrl) return;
                sheet.error = reason;
                sheet.checking = false;
            }
            function onInstallFinished(id, name) {
                if (sheet.visible) sheet.closeSheet();
            }
        }

        MouseArea { anchors.fill: parent; onClicked: sheet.closeSheet() }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(560, root.width * 0.9)
            height: sheetCol.implicitHeight + 64
            radius: 20
            color: Qt.rgba(0.05, 0.055, 0.08, 0.94)
            border.width: 1; border.color: theme.edge
            MouseArea { anchors.fill: parent }   // swallow the dismiss click

            Column {
                id: sheetCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 32
                spacing: 0

                Text { text: "Install from a link"; color: theme.ink
                       font.family: theme.display; font.pixelSize: 26 }
                Text {
                    topPadding: 8
                    width: parent.width
                    text: "Paste an extension’s address. The house reads what it offers and shows you before anything is added."
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Item { width: 1; height: 20 }
                Rectangle {
                    width: parent.width
                    height: 44
                    radius: 12
                    color: theme.glassTint
                    border.width: 1
                    border.color: urlInput.activeFocus ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge
                    TextInput {
                        id: urlInput
                        anchors.fill: parent
                        anchors.leftMargin: 15
                        anchors.rightMargin: 15
                        verticalAlignment: TextInput.AlignVCenter
                        color: theme.ink
                        font.family: theme.ui; font.pixelSize: 14
                        clip: true
                        onAccepted: sheet.check()
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !urlInput.text.length && !urlInput.activeFocus
                            text: "https://…/manifest.json   or   stremio://…"
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 13
                        }
                    }
                }

                Text {
                    visible: sheet.checking
                    topPadding: 16
                    text: "Reading the manifest…"
                    color: theme.inkDim
                    font.family: theme.display; font.italic: true; font.pixelSize: 15
                }
                Text {
                    visible: sheet.error.length > 0
                    topPadding: 16
                    width: parent.width
                    text: sheet.error
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    visible: sheet.previewManifest !== null
                    width: parent.width
                    height: 74
                    radius: 14
                    color: Qt.rgba(0.94, 0.77, 0.29, 0.06)
                    border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.35)
                    Row {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 15
                        AddonLogo {
                            anchors.verticalCenter: parent.verticalCenter
                            addonId: sheet.previewManifest ? (sheet.previewManifest.id || "") : ""
                            addonName: sheet.previewManifest ? (sheet.previewManifest.name || "") : ""
                            manifestLogo: sheet.previewManifest ? (sheet.previewManifest.logo || "") : ""
                            size: 44; radius: 11
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 60
                            spacing: 3
                            Text { text: sheet.previewManifest ? sheet.previewManifest.name : ""
                                   color: theme.ink; font.family: theme.ui
                                   font.pixelSize: 14; font.weight: Font.DemiBold }
                            Text {
                                width: parent.width
                                text: {
                                    if (!sheet.previewManifest) return "";
                                    var m = sheet.previewManifest;
                                    var res = m.resources || [];
                                    var names = [];
                                    for (var i = 0; i < res.length; i++)
                                        names.push(typeof res[i] === "string" ? res[i] : res[i].name);
                                    var host = sheet.previewUrl.replace(/^https?:\/\//, "").split("/")[0];
                                    return "gives " + (names.join(", ") || "resources") + " · from " + host;
                                }
                                color: theme.inkDimmer
                                font.family: theme.ui; font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Item { width: 1; height: 24 }
                Row {
                    anchors.right: parent.right
                    spacing: 26
                    Text {
                        text: "Cancel"
                        color: cancelMa.containsMouse ? theme.ink : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 14
                        MouseArea { id: cancelMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor; onClicked: sheet.closeSheet() }
                    }
                    Text {
                        text: sheet.previewManifest
                              ? "Install " + sheet.previewManifest.name
                              : "Read it first"
                        color: readMa.containsMouse ? "#ffd968" : theme.gold
                        font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                        MouseArea {
                            id: readMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sheet.previewManifest
                                       ? Extensions.install(sheet.previewUrl)
                                       : sheet.check()
                        }
                    }
                }
            }
        }
    }
}
