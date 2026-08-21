// ExtensionsSources — the world-agnostic Sources pane.
//
// Hemanth's brief (2026-07-26): "a completely new extension page that is world agnostic,
// meaning all the extensions (theatre, biblio, tankoban) are in one page but in different
// rows." Browse and Installed stay beside it, unchanged.
//
// Shape: a chain across the top showing what each world asks and in what order, then one
// section per world. The chain is the page's thesis — the order sources are asked in is the
// single fact that governs what you actually get, and nothing in the app has ever shown it.
//
// SECTIONS ARE DATA, NOT CODE. `sections` below is derived from the installed roster, so a
// world with nothing in it does not render, and UNIVERSES appears the moment a universe
// extension is installed without another line of layout. That matters: Hemanth caught that
// an "ask order" framing breaks for universes, which aggregate an IP and fetch nothing. They
// are their own world here (Catalog.worldsFor returns ["universes"] by role, universes design
// §5.1a), so they get a section with no ranks at all rather than a broken position in a queue.
//
// Vocabulary: CATALOGUE fills the shelves, SOURCES fetch the file, ALSO INSTALLED is neither.
// The group was briefly called "ASK ORDER" — he asked what that meant, which is the label
// failing. The rank numerals carry the order; the words do not have to.
pragma ComponentBehavior: Bound
import QtQuick
import "ExtensionsCatalog.js" as Catalog

Item {
    id: root
    objectName: "extensionsSources"
    implicitHeight: col.implicitHeight

    property var installedList: []
    property string query: ""

    signal removeRequested(var entry)
    signal configureRequested(var entry)

    Theme { id: theme }

    readonly property var worldTitles: ({
        theatre: "Theatre", tankoban: "Tankoban", biblio: "Biblio", universes: "Universes"
    })
    // Order matters to the reader, not to the machine: the three media worlds as the app
    // presents them everywhere else, then universes, which sit above all three.
    readonly property var worldOrder: ["theatre", "tankoban", "biblio", "universes"]

    function inWorld(entry, world) { return Catalog.inWorld(entry, world) }
    function rowsFor(world) {
        var out = [], cat = [], wells = [], other = [];
        for (var i = 0; i < installedList.length; i++) {
            var e = installedList[i];
            if (!Catalog.inWorld(e, world)) continue;
            if (Catalog.isCatalogue(e)) cat.push(e);
            else if (Catalog.isWell(e)) wells.push(e);
            else other.push(e);
        }
        return cat.concat(wells).concat(other);
    }
    function wellsFor(world) {
        var out = [];
        for (var i = 0; i < installedList.length; i++)
            if (Catalog.inWorld(installedList[i], world) && Catalog.isWell(installedList[i]))
                out.push(installedList[i]);
        return out;
    }
    // Only worlds that actually carry something render. An empty section is a lie about the
    // shape of the app, and it is how the old page ended up with two dead tabs.
    readonly property var sections: {
        var out = [];
        for (var i = 0; i < worldOrder.length; i++) {
            var w = worldOrder[i], rows = rowsFor(w);
            if (rows.length) out.push({ key: w, title: worldTitles[w], rows: rows });
        }
        return out;
    }
    function nameOf(e) { return (e && e.manifest && e.manifest.name) || (e && e.id) || "" }
    function hit(e) {
        return !query.length || nameOf(e).toLowerCase().indexOf(query.toLowerCase()) !== -1;
    }
    // "Also in Tankoban · 4th" — the one sub-line that survives, because it is not a
    // description. It is the fact whose invisibility let a reorder in one world silently
    // rewrite another, and it is read off the live roster rather than stored.
    function tieFor(entry, world) {
        var ws = Catalog.worldsFor(entry);
        if (ws.length < 2 || !Catalog.isWell(entry)) return "";
        for (var i = 0; i < ws.length; i++) {
            if (ws[i] === world) continue;
            var rank = 0, w = wellsFor(ws[i]);
            for (var k = 0; k < w.length; k++) if (w[k].id === entry.id) { rank = k + 1; break; }
            var ord = rank === 1 ? "1st" : rank === 2 ? "2nd" : rank === 3 ? "3rd" : rank + "th";
            return "Also in " + (worldTitles[ws[i]] || ws[i]) + " · " + ord;
        }
        return "";
    }
    function moveWell(id, delta, world) {
        var m = Catalog.moveDestination(installedList, world, id, delta);
        if (m) Extensions.moveTo(m.id, m.index);
    }

    Column {
        id: col
        width: parent.width
        spacing: 0

        // ── the chain ────────────────────────────────────────────────────────
        Rectangle {
            width: parent.width
            height: chainCol.implicitHeight + 48
            radius: 20
            border.width: 1; border.color: theme.edge
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#1a1c2c" }
                GradientStop { position: 0.55; color: "#10111c" }
                GradientStop { position: 1.0; color: "#0b0c14" }
            }
            Column {
                id: chainCol
                anchors.fill: parent
                anchors.margins: 24
                spacing: 4
                Text {
                    text: "ASKED IN THIS ORDER"
                    color: theme.gold
                    font.family: theme.ui; font.pixelSize: 11
                    font.letterSpacing: 2.6; font.bold: true
                    bottomPadding: 12
                }
                Repeater {
                    model: root.sections
                    delegate: Row {
                        id: chainRow
                        required property var modelData
                        spacing: 16
                        // Universes fetch nothing, so they never appear in the chain.
                        visible: chainRow.modelData.key !== "universes"
                        height: visible ? 48 : 0
                        Text {
                            width: 96
                            horizontalAlignment: Text.AlignRight
                            anchors.verticalCenter: parent.verticalCenter
                            text: chainRow.modelData.title
                            color: theme.inkDim
                            font.family: theme.display; font.pixelSize: 17
                        }
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            Repeater {
                                model: chainRow.modelData.rows
                                delegate: Row {
                                    id: chip
                                    required property var modelData
                                    required property int index
                                    spacing: 8
                                    // Only catalogues and sources are in the chain; a
                                    // subtitles or metadata row is asked for neither.
                                    visible: Catalog.isCatalogue(chip.modelData)
                                             || Catalog.isWell(chip.modelData)
                                    readonly property bool isCat: Catalog.isCatalogue(chip.modelData)
                                    readonly property int rank: {
                                        if (chip.isCat) return 0;
                                        var w = root.wellsFor(chainRow.modelData.key);
                                        for (var i = 0; i < w.length; i++)
                                            if (w[i].id === chip.modelData.id) return i + 1;
                                        return 0;
                                    }
                                    // the gate: everything left of it fills the shelves,
                                    // everything right of it fetches
                                    Rectangle {
                                        width: 1; height: 22
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: theme.edge
                                        visible: chip.rank === 1
                                    }
                                    Rectangle {
                                        height: 38
                                        width: chipRow.implicitWidth + 24
                                        anchors.verticalCenter: parent.verticalCenter
                                        radius: 19
                                        color: Catalog.worldsFor(chip.modelData).length > 1
                                               ? Qt.rgba(0.94, 0.77, 0.29, 0.07)
                                               : Qt.rgba(1, 1, 1, 0.035)
                                        border.width: 1
                                        border.color: Catalog.worldsFor(chip.modelData).length > 1
                                                      ? Qt.rgba(0.94, 0.77, 0.29, 0.55) : theme.edge
                                        opacity: chip.modelData.enabled ? 1 : 0.4
                                        Row {
                                            id: chipRow
                                            anchors.centerIn: parent
                                            spacing: 8
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: chip.rank > 0 ? chip.rank : ""
                                                visible: chip.rank > 0
                                                color: theme.gold
                                                font.family: theme.ui; font.pixelSize: 11
                                                font.bold: true
                                            }
                                            AddonLogo {
                                                anchors.verticalCenter: parent.verticalCenter
                                                addonId: chip.modelData.id
                                                addonName: root.nameOf(chip.modelData)
                                                manifestLogo: (chip.modelData.manifest || {}).logo || ""
                                                size: 26; radius: 7
                                            }
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: root.nameOf(chip.modelData)
                                                color: chip.isCat ? theme.inkDimmer : theme.inkDim
                                                font.family: theme.ui; font.pixelSize: 13
                                            }
                                        }
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "→"
                                        color: theme.inkDimmer
                                        opacity: 0.55
                                        font.pixelSize: 11
                                        visible: chip.rank > 0
                                                 && chip.rank < root.wellsFor(chainRow.modelData.key).length
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── one section per world ────────────────────────────────────────────
        Repeater {
            model: root.sections
            delegate: Column {
                id: section
                required property var modelData
                width: col.width
                topPadding: 52
                spacing: 0

                Item {
                    width: parent.width
                    height: 52
                    Text {
                        id: worldName
                        objectName: "extensionSourceSection_" + section.modelData.key
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 12
                        text: section.modelData.title
                        color: theme.ink
                        font.family: theme.display; font.pixelSize: 36
                    }
                    Text {
                        anchors.left: worldName.right
                        anchors.leftMargin: 16
                        anchors.baseline: worldName.baseline
                        // A universe supplies no sources, so it is never counted in them.
                        text: {
                            var n = section.modelData.rows.length;
                            if (section.modelData.key === "universes")
                                return n + (n === 1 ? " universe" : " universes");
                            return n + (n === 1 ? " source" : " sources");
                        }
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 12
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; height: 1
                        color: theme.edge
                    }
                }

                Repeater {
                    model: section.modelData.rows
                    delegate: Item {
                        id: row
                        // Automation identity (Lanista), DiscoverBrowser.qml:727 precedent:
                        // keyed by the row's own extension id, stable across re-sort/re-filter.
                        objectName: "extensionSourceRow_" + String(row.modelData ? row.modelData.id : row.index)
                        required property var modelData
                        required property int index
                        width: section.width
                        visible: root.hit(row.modelData)
                        height: visible ? (70 + (row.startsGroup ? 30 : 0)) : 0

                        readonly property var manifest: row.modelData.manifest || ({})
                        readonly property bool isCore: row.modelData.core === true
                        readonly property bool isOn: row.modelData.enabled === true
                        readonly property bool isCat: Catalog.isCatalogue(row.modelData)
                        readonly property bool isWell: Catalog.isWell(row.modelData)
                        readonly property bool isHouse: String(row.modelData.transportUrl || "")
                                                        .indexOf("colosseum://") === 0
                        readonly property bool configurable:
                            (row.manifest.behaviorHints || {}).configurable === true
                        readonly property bool configurationRequired:
                            (row.manifest.behaviorHints || {}).configurationRequired === true
                        readonly property string group:
                            row.isCat ? "catalogue" : (row.isWell ? "sources" : "rest")
                        readonly property string groupTitle:
                            // The section header already says "Universes"; labelling them
                            // "Also installed" underneath it would be noise.
                            section.modelData.key === "universes" ? ""
                          : row.group === "catalogue" ? "Catalogue"
                          : row.group === "sources" ? "Sources" : "Also installed"
                        readonly property bool startsGroup: {
                            if (row.index <= 0) return true;
                            var p = section.modelData.rows[row.index - 1];
                            if (!p) return true;
                            var pg = Catalog.isCatalogue(p) ? "catalogue"
                                   : (Catalog.isWell(p) ? "sources" : "rest");
                            return pg !== row.group;
                        }
                        readonly property int rank: {
                            if (!row.isWell) return 0;
                            var w = root.wellsFor(section.modelData.key);
                            for (var i = 0; i < w.length; i++)
                                if (w[i].id === row.modelData.id) return i + 1;
                            return 0;
                        }
                        readonly property string tie: root.tieFor(row.modelData, section.modelData.key)
                        readonly property bool canUp:
                            row.isWell && Catalog.moveDestination(
                                root.installedList, section.modelData.key, row.modelData.id, -1) !== null
                        readonly property bool canDown:
                            row.isWell && Catalog.moveDestination(
                                root.installedList, section.modelData.key, row.modelData.id, 1) !== null

                        Text {
                            visible: row.startsGroup && row.groupTitle !== ""
                            anchors.top: parent.top
                            anchors.topMargin: 14
                            anchors.left: parent.left
                            text: row.groupTitle.toUpperCase()
                            color: theme.gold
                            font.family: theme.ui; font.pixelSize: 10
                            font.letterSpacing: 2.4; font.bold: true
                        }

                        MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true
                                    acceptedButtons: Qt.NoButton }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width; height: 1
                            color: Qt.rgba(1, 1, 1, 0.055)
                            visible: row.index < section.modelData.rows.length - 1
                        }

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.verticalCenterOffset: row.startsGroup ? 15 : 0
                            width: parent.width
                            spacing: 16

                            Text {
                                width: 26
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.rank > 0 ? row.rank : ""
                                color: theme.gold
                                font.family: theme.ui; font.pixelSize: 13; font.bold: true
                                opacity: row.isOn ? 1 : 0.45
                            }
                            Column {
                                width: 18
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                opacity: rowMa.containsMouse ? 1 : 0.32
                                visible: row.isWell
                                Text {
                                    text: "▲"; font.pixelSize: 9
                                    color: upMa.containsMouse ? theme.ink : theme.inkDimmer
                                    opacity: row.canUp ? 1 : 0.3
                                    MouseArea {
                                        id: upMa; anchors.fill: parent; anchors.margins: -9
                                        hoverEnabled: true; enabled: row.canUp
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.moveWell(row.modelData.id, -1,
                                                                 section.modelData.key)
                                    }
                                }
                                Text {
                                    text: "▼"; font.pixelSize: 9
                                    color: downMa.containsMouse ? theme.ink : theme.inkDimmer
                                    opacity: row.canDown ? 1 : 0.3
                                    MouseArea {
                                        id: downMa; anchors.fill: parent; anchors.margins: -9
                                        hoverEnabled: true; enabled: row.canDown
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.moveWell(row.modelData.id, 1,
                                                                 section.modelData.key)
                                    }
                                }
                            }
                            AddonLogo {
                                anchors.verticalCenter: parent.verticalCenter
                                opacity: row.isOn ? 1 : 0.45
                                addonId: row.modelData.id
                                addonName: root.nameOf(row.modelData)
                                manifestLogo: row.manifest.logo || ""
                                size: 44; radius: 11
                            }
                            Column {
                                width: parent.width - 26 - 44 - 300 - 16 * 4
                                       - (row.isWell ? 18 + 16 : 0)
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3
                                opacity: row.isOn ? 1 : 0.45
                                Text {
                                    width: parent.width
                                    text: root.nameOf(row.modelData)
                                    color: theme.ink
                                    font.family: theme.ui; font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    visible: row.tie.length > 0
                                    text: row.tie
                                    color: theme.gold
                                    opacity: 0.85
                                    font.family: theme.ui; font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }
                            Row {
                                width: 300
                                anchors.verticalCenter: parent.verticalCenter
                                layoutDirection: Qt.RightToLeft
                                spacing: 22

                                Rectangle {
                                    objectName: "extensionSourceToggle_" + String(row.modelData ? row.modelData.id : row.index)
                                    // Automation surface (additive, no behavior change): a
                                    // click-assertable mirror of row.isOn, matching the same
                                    // amendment on ExtensionsPage.qml's extensionToggle_.
                                    readonly property bool checked: row.isOn
                                    width: 40; height: 22; radius: 11
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: row.isOn ? Qt.rgba(0.94, 0.77, 0.29, 0.85)
                                                    : Qt.rgba(1, 1, 1, 0.12)
                                    border.width: 1
                                    border.color: row.isOn ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge
                                    opacity: row.isCore ? 0.5 : 1
                                    Rectangle {
                                        width: 16; height: 16; radius: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        x: row.isOn ? 20 : 2
                                        color: row.isOn ? "#141207" : theme.inkDim
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -11
                                        enabled: !row.isCore
                                        cursorShape: row.isCore ? Qt.ArrowCursor
                                                                : Qt.PointingHandCursor
                                        onClicked: Extensions.setEnabled(row.modelData.id, !row.isOn)
                                    }
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: row.isCore ? "Locked" : "Remove"
                                    color: row.isCore ? theme.inkDimmer
                                         : removeMa.containsMouse ? theme.ink : theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 13
                                    MouseArea {
                                        id: removeMa
                                        anchors.fill: parent; anchors.margins: -12
                                        hoverEnabled: true; enabled: !row.isCore
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.removeRequested(row.modelData)
                                    }
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: row.configurable || row.configurationRequired
                                    text: row.configurationRequired
                                          ? "Configure required"
                                          : (row.isHouse ? "Settings" : "Configure ↗")
                                    color: cfgMa.containsMouse ? theme.ink : theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 13
                                    MouseArea {
                                        id: cfgMa
                                        anchors.fill: parent; anchors.margins: -12
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.configureRequested(row.modelData)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
