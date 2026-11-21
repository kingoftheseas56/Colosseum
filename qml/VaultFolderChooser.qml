// Android only: Colosseum's own folder chooser for the Vault. Storage places first
// (Internal storage, SD cards, USB drives), then ordinary folders as you tap in, and one
// "Add this folder" button. Android's own picker is never used: it refuses Download and
// the top of each storage place. Built from the same pieces as VaultFolderSelection.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt.labs.folderlistmodel

FocusScope {
    id: chooser
    objectName: "vaultFolderChooser"
    property Item backdrop: null
    // Storage places: [{label, path, removable}]. The page fills it from PlatformRuntime.
    property var volumes: []
    property string currentPath: ""
    readonly property bool compact: width < 540
    readonly property bool atPlaces: currentPath === ""
    readonly property var volume: volumeFor(currentPath)
    readonly property string volumeLabel: atPlaces ? qsTr("Storage")
        : volume ? volume.label : "/"
    readonly property string pathSeparator: "  ›  "
    readonly property string relativeLocation: {
        if (atPlaces || !volume) return ""
        return currentPath.slice(volume.path.length).split("/").filter(s => s.length > 0).join(pathSeparator)
    }
    readonly property int rowHeight: Math.max(theme.vaultRowHeight, 44)
    signal addRequested(string path)
    signal cancelRequested()

    function volumeFor(path) {
        let best = null
        for (const v of volumes)
            if (path === v.path || path.startsWith(v.path + "/"))
                if (!best || v.path.length > best.path.length) best = v
        return best
    }
    function hiddenFolder(path) { return /\/Android\/(data|obb)$/.test(path) }
    function urlFor(path) {
        return Qt.url("file://" + path.split("/").map(encodeURIComponent).join("/"))
    }
    function open(path) { currentPath = path }
    function up() {
        if (atPlaces) return
        if (volume && currentPath === volume.path) { currentPath = ""; return }
        const cut = currentPath.lastIndexOf("/")
        currentPath = cut > 0 ? currentPath.slice(0, cut) : ""
    }
    // Back steps up a level first; only the places level closes the chooser.
    function handleBack() {
        if (atPlaces) return false
        up()
        return true
    }
    Component.onCompleted: {
        // A single storage place has nothing to choose between: open straight into it.
        if (volumes.length === 1) currentPath = volumes[0].path
    }

    focus: true
    Keys.onEscapePressed: event => { event.accepted = true; chooser.cancelRequested() }

    // One tappable folder row, in the same style as the Vault folder screen's rows.
    component ChooserRow: Rectangle {
        id: row
        property string entryName: ""
        signal activated()
        Theme { id: skin }
        radius: skin.vaultControlRadius
        color: tap.pressed || tap.containsMouse ? skin.glassTint : "transparent"
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 1
            color: skin.edge
            opacity: 0.45
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 10
            Image {
                Layout.preferredWidth: 16; Layout.preferredHeight: 16
                source: "../assets/icons/lucide/folder-open.svg"
                opacity: 0.75
            }
            Text {
                Layout.fillWidth: true
                text: row.entryName
                textFormat: Text.PlainText
                color: skin.ink
                font.family: skin.ui
                font.pixelSize: skin.vaultBody
                elide: Text.ElideRight
            }
            Image {
                Layout.preferredWidth: 16; Layout.preferredHeight: 16
                source: "../assets/icons/lucide/chevron-right.svg"
                opacity: 0.75
            }
        }
        MouseArea {
            id: tap
            objectName: "vaultFolderChooserRow"
            anchors.fill: parent
            hoverEnabled: true
            onClicked: row.activated()
        }
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Open %1").arg(row.entryName)
    }

    Theme { id: theme }
    FolderListModel {
        id: folders
        folder: chooser.atPlaces ? "" : chooser.urlFor(chooser.currentPath)
        showFiles: false
        showDirs: true
        showDotAndDotDot: false
        showHidden: false
        sortField: FolderListModel.Name
    }

    Glass { anchors.fill: parent; backdrop: chooser.backdrop; radius: theme.vaultRadius; scrim: 0.38 }
    Rectangle {
        anchors.fill: parent
        radius: theme.vaultRadius
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(theme.vaultSurfaceHi.r, theme.vaultSurfaceHi.g, theme.vaultSurfaceHi.b, 0.91) }
            GradientStop { position: 1; color: Qt.rgba(theme.vaultSurface.r, theme.vaultSurface.g, theme.vaultSurface.b, 0.94) }
        }
        border.color: theme.edge
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: chooser.compact ? 12 : 22
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    objectName: "vaultFolderChooserTitle"
                    Layout.fillWidth: true
                    text: qsTr("Add a folder")
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: chooser.compact ? theme.vaultSectionTitle : theme.vaultPageTitle
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: chooser.atPlaces ? qsTr("Choose where your files are.")
                                           : qsTr("Open folders until you reach the one to add.")
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: theme.vaultBody
                    wrapMode: Text.Wrap
                }
            }
            VaultLibraryButton {
                objectName: "vaultCloseFolderChooser"
                Layout.preferredWidth: 38
                Layout.alignment: Qt.AlignVCenter
                centerContent: true
                padding: 0
                iconPath: "../assets/icons/lucide/x.svg"
                iconSize: 24
                accessibleName: qsTr("Close folder chooser")
                onClicked: chooser.cancelRequested()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: theme.vaultSurface
            border.color: theme.edge
            radius: theme.vaultControlRadius
            objectName: "vaultFolderChooserBreadcrumb"
            Accessible.role: Accessible.StaticText
            Accessible.name: chooser.relativeLocation
                ? qsTr("%1, %2").arg(chooser.volumeLabel).arg(chooser.relativeLocation) : chooser.volumeLabel
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 9
                VaultAction {
                    objectName: "vaultFolderChooserUp"
                    visible: !chooser.atPlaces
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                    quiet: true
                    accessibleName: qsTr("Up one folder")
                    Image {
                        anchors.centerIn: parent
                        width: 16; height: 16
                        source: "../assets/icons/lucide/arrow-left.svg"
                    }
                    onTriggered: chooser.up()
                }
                Image {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    source: "../assets/icons/lucide/folder-open.svg"
                    opacity: 0.75
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        objectName: "vaultFolderChooserVolumeLabel"
                        Layout.maximumWidth: chooser.relativeLocation ? Math.max(64, chooser.width * 0.39) : chooser.width - 70
                        text: chooser.volumeLabel
                        textFormat: Text.PlainText
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: theme.vaultCaption
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: chooser.relativeLocation.length > 0
                        text: chooser.pathSeparator
                        textFormat: Text.PlainText
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: theme.vaultCaption
                        font.weight: Font.DemiBold
                    }
                    Text {
                        objectName: "vaultFolderChooserLocationLabel"
                        Layout.fillWidth: true
                        text: chooser.relativeLocation
                        textFormat: Text.PlainText
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: theme.vaultCaption
                        font.weight: Font.DemiBold
                        elide: Text.ElideLeft
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.edge }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: chooser.rowHeight

            ListView {
                id: places
                objectName: "vaultFolderChooserPlaces"
                anchors.fill: parent
                visible: chooser.atPlaces
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: chooser.volumes
                ScrollBar.vertical: HouseScrollBar { flick: places; glassStyle: true }
                delegate: ChooserRow {
                    required property var modelData
                    width: places.width - 12
                    height: chooser.rowHeight
                    entryName: modelData.label
                    onActivated: chooser.open(modelData.path)
                }
            }
            ListView {
                id: list
                objectName: "vaultFolderChooserEntries"
                anchors.fill: parent
                visible: !chooser.atPlaces
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: folders
                ScrollBar.vertical: HouseScrollBar { flick: list; glassStyle: true }
                delegate: ChooserRow {
                    required property string filePath
                    required property string fileName
                    // Android/data and Android/obb are never offered.
                    readonly property bool allowed: !chooser.hiddenFolder(filePath)
                    visible: allowed
                    enabled: allowed
                    width: list.width - 12
                    height: allowed ? chooser.rowHeight : 0
                    entryName: fileName
                    onActivated: chooser.open(filePath)
                }
            }
            Text {
                anchors.centerIn: parent
                width: Math.max(1, parent.width - 18)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: chooser.atPlaces ? places.count === 0 : list.count === 0
                text: chooser.atPlaces ? qsTr("No storage places were found.")
                                       : qsTr("This folder has no folders inside it.")
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: theme.vaultBody
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.edge }
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !chooser.compact
                text: qsTr("Colosseum only reads the folders you add.")
                verticalAlignment: Text.AlignVCenter
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: theme.vaultCaption
                wrapMode: Text.Wrap
            }
            Item { Layout.fillWidth: true; visible: chooser.compact }
            VaultLibraryButton {
                objectName: "vaultCancelFolderChooser"
                text: qsTr("Cancel")
                centerContent: true
                onClicked: chooser.cancelRequested()
            }
            VaultLibraryButton {
                objectName: "vaultAddThisFolder"
                text: qsTr("Add this folder")
                primary: true
                centerContent: true
                enabled: !chooser.atPlaces
                onClicked: chooser.addRequested(chooser.currentPath)
            }
        }
    }
}
