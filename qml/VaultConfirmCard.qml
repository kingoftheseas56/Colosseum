// VaultConfirmCard — the founding ceremony's ONE card (Slice 11), built to the locked mock
// agents/colosseum-vault-confirm-card-mock.html. After a new folder's first census it shows
// show-your-work sorting: one row per discovered subtree slice (path · what · sample) with a
// reassignable gold kind chip, the honest leftover line, and consent as one gold button.
//
// A SEEDABLE component (like OpenRecentPanel): it owns no backend. It takes the candidate
// `model` (VaultLibrary.candidate slices) + `rootPath`, and emits shelveRequested(kindOverrides)
// / dismissRequested — VaultPage wires those to VaultLibrary.confirmRoot / dismissCard. So a Qt
// Quick Test drives it with a seeded model, no app.
//
// Slice model row shape (VaultScanner census): { subtreePath, groupTitle, kind, count, mixed,
// loose, leftoverCount, [seriesCount], [sample], [sizeBytes] }. The bracketed fields are the
// scanner-model enrichment that fills the "· N series", sample line, and size count to full mock
// parity; the card renders what is present and stays honest when they are absent.
import QtQuick
import QtQuick.Layouts

Item {
    id: card
    anchors.fill: parent

    // ── inputs ──
    property var model: []
    property string rootPath: ""
    // ── outputs ──
    signal shelveRequested(var kindOverrides)
    signal dismissRequested()

    // chip reassignments the user made, keyed by subtreePath → kind ("comic"|"book"|"video").
    property var kindOverrides: ({})
    property int openChipRow: -1   // which row's kind picker is open (-1 none)

    // Lanista/plan objectName contract (vaultCard.sliceCount / .leftoverCount).
    readonly property int sliceCount: model.length
    readonly property int leftoverCount: totalLeftover

    Theme { id: theme }

    function kindLabel(k) {
        return k === "comic" ? "Comics" : k === "book" ? "Books" : k === "video" ? "Video" : k
    }
    function kindOf(row, i) {
        var ov = card.kindOverrides[row.subtreePath]
        return ov ? ov : row.kind
    }
    function leafName(p) { return ("" + p).split(/[\\/]/).pop() }

    // ── derived header counts ──
    readonly property int totalItems: {
        var n = 0
        for (var i = 0; i < model.length; i++) n += (model[i].count || 0)
        return n
    }
    readonly property int totalShelves: model.length
    readonly property int totalLeftover: {
        var n = 0
        for (var i = 0; i < model.length; i++) n += (model[i].leftoverCount || 0)
        return n
    }
    readonly property double totalBytes: {
        var b = 0
        for (var i = 0; i < model.length; i++) b += (model[i].sizeBytes || 0)
        return b
    }
    function humanSize(bytes) {
        if (bytes <= 0) return ""
        var u = ["B", "KB", "MB", "GB", "TB"], i = 0, v = bytes
        while (v >= 1024 && i < u.length - 1) { v /= 1024; i++ }
        return (v >= 10 ? Math.round(v) : Math.round(v * 10) / 10) + " " + u[i]
    }

    // ── dimmed Vault behind + veil (the card is a modal over the Vault page) ──
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.02, 0.024, 0.035, 0.6)
        MouseArea { anchors.fill: parent } // swallow; consent is explicit
    }

    // ── the card ──
    Rectangle {
        id: sheet
        width: Math.min(760, parent.width - 80)
        height: Math.min(bodyCol.implicitHeight + 72, parent.height - 120)
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(40, parent.height * 0.09)
        radius: 20
        color: Qt.rgba(0.071, 0.082, 0.110, 0.99)
        border.width: 1
        border.color: theme.edge

        Flickable {
            id: flick
            anchors.fill: parent
            anchors.margins: 4
            contentWidth: width
            contentHeight: bodyCol.implicitHeight + 68
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: bodyCol
                x: 40
                width: sheet.width - 80
                topPadding: 36
                spacing: 0

                Text {
                    text: "VAULT · NEW FOLDER"
                    color: theme.gold
                    font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3.5; font.weight: Font.DemiBold
                }
                Text {
                    topPadding: 8
                    width: parent.width
                    elide: Text.ElideMiddle
                    text: card.rootPath
                    color: theme.ink
                    font.family: theme.display; font.pixelSize: 40
                }

                // ── counts strip: items · shelves · size ──
                Row {
                    topPadding: 18
                    bottomPadding: 26
                    spacing: 26
                    Row {
                        spacing: 6
                        Text { text: card.totalItems; color: theme.ink; font.family: theme.display; font.pixelSize: 22 }
                        Text { anchors.bottom: parent.bottom; anchors.bottomMargin: 3
                               text: "items"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
                    }
                    Row {
                        spacing: 6
                        Text { text: card.totalShelves; color: theme.ink; font.family: theme.display; font.pixelSize: 22 }
                        Text { anchors.bottom: parent.bottom; anchors.bottomMargin: 3
                               text: card.totalShelves === 1 ? "shelf" : "shelves"; color: theme.inkDim
                               font.family: theme.ui; font.pixelSize: 13 }
                    }
                    Text {
                        visible: card.totalBytes > 0
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 3
                        text: card.humanSize(card.totalBytes); color: theme.ink
                        font.family: theme.display; font.pixelSize: 22
                    }
                }

                // ── one slice per discovered subtree ──
                Repeater {
                    model: card.model
                    delegate: Rectangle {
                        id: sliceRow
                        required property var modelData
                        required property int index
                        objectName: "vaultCardRow_" + index
                        // Lanista/tests read the row's live (possibly reassigned) kind here.
                        property string kind: card.kindOf(modelData, index)

                        width: bodyCol.width
                        height: rowGrid.implicitHeight + 32
                        radius: 14
                        color: Qt.rgba(0.094, 0.110, 0.145, 0.96)
                        border.width: 1
                        border.color: theme.edge
                        anchors.horizontalCenter: undefined

                        RowLayout {
                            id: rowGrid
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 18; anchors.rightMargin: 18
                            spacing: 16

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    text: modelData.subtreePath || ""
                                    color: theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 0.3
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: {
                                        var k = sliceRow.kind
                                        var noun = (modelData.count === 1)
                                            ? (k === "comic" ? "comic" : k === "book" ? "book" : "video")
                                            : (k === "comic" ? "comics" : k === "book" ? "books" : "videos")
                                        var s = (modelData.count || 0) + " " + noun
                                        if (modelData.seriesCount > 0)
                                            s += "  ·  " + modelData.seriesCount + (modelData.seriesCount === 1 ? " series" : " series")
                                        return s
                                    }
                                    color: theme.ink
                                    font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: !!modelData.sample
                                    wrapMode: Text.WordWrap
                                    text: modelData.sample || ""
                                    color: theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 12
                                }
                            }

                            // reassignable kind chip
                            Rectangle {
                                objectName: "vaultCardRow_" + sliceRow.index + "_chip"
                                Layout.alignment: Qt.AlignVCenter
                                implicitWidth: chipRow.implicitWidth + 32
                                implicitHeight: 38
                                radius: 19
                                color: "transparent"
                                border.width: 1
                                border.color: modelData.loose ? theme.edge : Qt.rgba(0.94, 0.77, 0.29, 0.5)
                                Row {
                                    id: chipRow
                                    anchors.centerIn: parent
                                    spacing: 8
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: card.kindLabel(sliceRow.kind)
                                        color: modelData.loose ? theme.inkDim : theme.gold
                                        font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "▾"; color: theme.inkDimmer; font.pixelSize: 10
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: card.openChipRow = (card.openChipRow === sliceRow.index) ? -1 : sliceRow.index
                                }

                                // kind picker (Comics / Books / Video)
                                Rectangle {
                                    visible: card.openChipRow === sliceRow.index
                                    z: 50
                                    anchors.top: parent.bottom; anchors.topMargin: 6
                                    anchors.right: parent.right
                                    width: 140
                                    height: pickCol.implicitHeight + 10
                                    radius: 12
                                    color: Qt.rgba(0.071, 0.082, 0.110, 0.99)
                                    border.width: 1; border.color: theme.edge
                                    Column {
                                        id: pickCol
                                        width: parent.width
                                        padding: 5
                                        Repeater {
                                            model: [ { k: "comic", label: "Comics" },
                                                     { k: "book", label: "Books" },
                                                     { k: "video", label: "Video" } ]
                                            delegate: Rectangle {
                                                required property var modelData
                                                objectName: "vaultCardRow_" + sliceRow.index + "_pick_" + modelData.k
                                                width: parent.width - 10
                                                height: 34
                                                radius: 8
                                                color: pickMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    anchors.left: parent.left; anchors.leftMargin: 12
                                                    text: modelData.label
                                                    color: (sliceRow.kind === modelData.k) ? theme.gold : theme.ink
                                                    font.family: theme.ui; font.pixelSize: 13
                                                }
                                                MouseArea {
                                                    id: pickMa
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        var ov = Object.assign({}, card.kindOverrides)
                                                        ov[sliceRow.modelData.subtreePath] = modelData.k
                                                        card.kindOverrides = ov
                                                        card.openChipRow = -1
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

                // ── the honest leftover line ──
                Text {
                    visible: card.totalLeftover > 0
                    topPadding: 16
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: card.totalLeftover + (card.totalLeftover === 1 ? " file" : " files")
                          + " Colosseum doesn't read — left where they are."
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 12
                }

                // ── consent ──
                Row {
                    topPadding: 26
                    width: parent.width
                    layoutDirection: Qt.RightToLeft
                    spacing: 12

                    Rectangle {
                        objectName: "vaultCardShelveAll"
                        width: shelveT.implicitWidth + 56; height: 46; radius: 12
                        color: shelveMa.containsMouse ? Qt.rgba(0.98, 0.82, 0.36, 1) : theme.gold
                        Text { id: shelveT; anchors.centerIn: parent; text: "Shelve it all"
                               color: "#151310"; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                        MouseArea {
                            id: shelveMa
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: card.shelveRequested(card.kindOverrides)
                        }
                    }
                    Rectangle {
                        objectName: "vaultCardNotNow"
                        width: notNowT.implicitWidth + 44; height: 46; radius: 12
                        color: notNowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                        border.width: 1; border.color: theme.edge
                        Text { id: notNowT; anchors.centerIn: parent; text: "Not now"
                               color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                        MouseArea {
                            id: notNowMa
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: card.dismissRequested()
                        }
                    }
                }

                Item { width: 1; height: 8 }
            }
        }
    }
}
