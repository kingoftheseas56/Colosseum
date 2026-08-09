// VaultPage — "On this machine": the local-media Vault as a host-owned full page, entered from
// the taskbar folder door. Slice 10 lands the permanent door + this page's EMPTY state (nothing
// indexed yet): eyebrow, title, and a dashed Add-folder drop surface. It paints from the
// VaultLibrary read-model (itemCount/scanning); the shelves that fill a populated Vault, and the
// folder-scan ingest behind Add folder, land in Slice 11. Same chrome vocabulary as
// Settings/Downloads (back · minimize · fullscreen · power) so it reads as one of the house's pages.
import QtQuick
import QtQuick.Controls

Item {
    id: root
    property Item backdrop: null
    signal backRequested()
    signal addFolderRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()

    Theme { id: theme }

    // ---- read-model: the Vault's published truth (revision-driven refresh) ----
    // Touch revision so every shelf/count re-reads on a committed publish; itemCount/scanning drive
    // the empty vs scanning-empty state (a populated Vault + its shelves arrive in Slice 11).
    readonly property int itemCount:
        (typeof VaultLibrary !== "undefined") ? (VaultLibrary.revision, VaultLibrary.itemCount) : 0
    readonly property bool scanning:
        (typeof VaultLibrary !== "undefined") ? VaultLibrary.scanning : false
    readonly property bool scanningEmpty: itemCount === 0 && scanning

    // swallow clicks so nothing behind this page receives them
    MouseArea { anchors.fill: parent }
    Rectangle { anchors.fill: parent; color: "#000000" }

    // ---- live shell wallpaper (the same backdrop sampling the other full pages use) ----
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
            Text { text: "ON THIS MACHINE"; color: theme.inkDimmer
                   font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6; font.weight: Font.DemiBold }
            Text { text: "Vault"; color: theme.ink; topPadding: 8
                   font.family: theme.display; font.pixelSize: 56; font.letterSpacing: -1 }
            Item { width: 1; height: 20 }
            Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }

            // ---- empty state: the dashed Add-folder drop surface ----
            Item { width: 1; height: 44 }

            Rectangle {
                id: dropSurface
                objectName: "vaultDropSurface"
                width: col.width
                height: 320
                radius: 20
                color: dropHover.containsDrag ? Qt.rgba(0.94, 0.77, 0.29, 0.06)
                                              : Qt.rgba(0.04, 0.045, 0.065, 0.42)

                // QML has no dashed Rectangle border, so paint one — brightens to gold on drag-over.
                Canvas {
                    id: dashes
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d"); ctx.reset()
                        ctx.strokeStyle = dropHover.containsDrag ? "rgba(240,196,74,0.85)" : "rgba(255,255,255,0.22)"
                        ctx.lineWidth = 1.6
                        ctx.setLineDash([9, 7])
                        ctx.strokeRect(1, 1, width - 2, height - 2)
                    }
                }
                Connections { target: dropHover; function onContainsDragChanged() { dashes.requestPaint() } }

                Column {
                    anchors.centerIn: parent
                    width: parent.width - 96
                    spacing: 16

                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 48; height: 48; opacity: 0.7
                        source: "../assets/icons/vault-folder.svg"
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        text: root.scanningEmpty ? "Looking through your folder…"
                                                 : "Add a folder of comics, books, or videos"
                        color: theme.ink
                        font.family: theme.ui; font.pixelSize: 18; font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        width: parent.width
                        wrapMode: Text.WordWrap
                        visible: !root.scanningEmpty
                        text: "The Vault reads what is already on this machine and keeps it here — nothing is downloaded or moved."
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 13; lineHeight: 1.3
                    }

                    // Add folder — opens the native folder picker (Slice 10). The ingest behind it
                    // (canonicalize · add as a Vault root · scan · shelve) lands in Slice 11.
                    Rectangle {
                        objectName: "vaultAddFolderButton"
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: !root.scanningEmpty
                        width: addLabel.implicitWidth + 44; height: 44; radius: 12
                        color: addMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.9) : Qt.rgba(1, 1, 1, 0.08)
                        border.width: 1
                        border.color: addMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.6) : theme.edge
                        Text {
                            id: addLabel
                            anchors.centerIn: parent
                            text: "Add folder"
                            color: addMa.containsMouse ? "#141207" : theme.ink
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: addMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.addFolderRequested()
                        }
                    }
                }

                // A folder dropped on THIS Vault-specific surface is an Add-folder gesture (Slice 10
                // opens the picker path; ingest is Slice 11). This is NOT the app-wide file-open drop.
                DropArea {
                    id: dropHover
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: (drop) => {
                        drop.accepted = true
                        root.addFolderRequested()
                    }
                }
            }
        }
    }

    // ---- scan pill (Slice 11): a folder census is running; cancelable. Shows the folder name;
    //      the live "N of M" count fills in once the scanner emits per-file progress. ----
    Rectangle {
        id: scanPill
        objectName: "vaultScanPill"
        property bool scanning: (typeof VaultLibrary !== "undefined") ? VaultLibrary.scanning : false
        property int doneCount: (typeof VaultLibrary !== "undefined")
                                ? (VaultLibrary.scanProgressChanged, VaultLibrary.scanDone) : 0
        property int totalCount: (typeof VaultLibrary !== "undefined")
                                 ? (VaultLibrary.scanProgressChanged, VaultLibrary.scanTotal) : 0
        property string rootPath: (typeof VaultLibrary !== "undefined")
                                  ? (VaultLibrary.scanProgressChanged, VaultLibrary.scanningRoot) : ""
        visible: scanning
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 44
        width: pillRow.implicitWidth + 40
        height: 52
        radius: 26
        color: Qt.rgba(0.04, 0.045, 0.065, 0.94)
        border.width: 1
        border.color: theme.edge

        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: 14

            Item {
                width: 16; height: 16
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    id: spinner
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: Qt.rgba(1, 1, 1, 0.14)
                    Rectangle {
                        width: 4; height: 4; radius: 2; color: theme.gold
                        anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
                    }
                    RotationAnimator on rotation {
                        running: scanPill.scanning; from: 0; to: 360
                        duration: 900; loops: Animation.Infinite
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    var name = scanPill.rootPath.split(/[\\/]/).pop()
                    var base = "Scanning " + (name || "folder")
                    return (scanPill.totalCount > 0)
                        ? (base + " — " + scanPill.doneCount + " of " + scanPill.totalCount)
                        : (base + "…")
                }
                color: theme.ink
                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
            }

            Rectangle {
                objectName: "vaultScanCancel"
                width: cancelLabel.implicitWidth + 22; height: 30; radius: 15
                anchors.verticalCenter: parent.verticalCenter
                color: cancelMa.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.07)
                Text {
                    id: cancelLabel
                    anchors.centerIn: parent
                    text: "Cancel"
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 13
                }
                MouseArea {
                    id: cancelMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (typeof VaultLibrary !== "undefined") VaultLibrary.cancelScan()
                }
            }
        }
    }

    // ---- top chrome: minimize · fullscreen · power (same vocabulary as Settings/Downloads) ----
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
            Text { text: "—"; color: mMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: mMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Text { text: "⛶"; color: fMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: fMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() } }
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

    // ── the founding-ceremony confirmation card: a modal over the Vault once a census yields a
    //    candidate. Seedable component; VaultPage wires it to the VaultLibrary façade. ──
    VaultConfirmCard {
        objectName: "vaultCard"
        anchors.fill: parent
        z: 30
        visible: (typeof VaultLibrary !== "undefined") ? VaultLibrary.cardVisible : false
        model: (typeof VaultLibrary !== "undefined") ? (VaultLibrary.candidateChanged, VaultLibrary.candidate) : []
        rootPath: (typeof VaultLibrary !== "undefined") ? VaultLibrary.candidateRoot : ""
        onShelveRequested: (ov) => { if (typeof VaultLibrary !== "undefined") VaultLibrary.confirmRoot(rootPath, ov) }
        onDismissRequested: { if (typeof VaultLibrary !== "undefined") VaultLibrary.dismissCard() }
    }
}
