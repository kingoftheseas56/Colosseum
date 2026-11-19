pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes

Popup {
    id: picker

    objectName: "vaultFolderSelection"
    required property var selection
    required property var library

    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    padding: 22

    // Keep stable delegates and scroll position when one checkbox changes.
    function syncEntries() {
        const rows = selection.entries || []
        let samePage = entryModel.count === rows.length
        for (let i = 0; samePage && i < rows.length; ++i)
            samePage = entryModel.get(i).entryPath === rows[i].path
        if (!samePage) {
            entryModel.clear()
            for (const row of rows) {
                entryModel.append({ entryPath: row.path, entryName: row.name,
                    directory: row.directory, included: row.included, inherited: row.inherited })
            }
            entries.currentIndex = -1
            entries.forceLayout()
            entries.positionViewAtBeginning()
        } else {
            for (let i = 0; i < rows.length; ++i) {
                entryModel.setProperty(i, "included", rows[i].included)
                entryModel.setProperty(i, "inherited", rows[i].inherited)
            }
        }
    }
    onSelectionChanged: if (entryModel && entries) syncEntries()
    Component.onCompleted: syncEntries()

    ListModel { id: entryModel }
    Theme { id: theme }

    background: Rectangle {
        color: theme.vaultSurface
        radius: theme.vaultRadius
        border.color: theme.edge
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.lighter(theme.vaultSurfaceHi, 1.2) }
            GradientStop { position: 1; color: theme.vaultSurface }
        }
    }

    contentItem: ColumnLayout {
        spacing: 12
        Keys.onEscapePressed: event => {
            event.accepted = true
            picker.library.cancelFolderSelection()
        }
        Text {
            Layout.fillWidth: true
            text: picker.selection.editing ? qsTr("Choose files for Vault") : qsTr("Add a folder to Vault")
            color: theme.ink
            font.family: theme.display
            font.pixelSize: theme.vaultSectionTitle
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: picker.selection.personalView
                  ? qsTr("Select an item to include it in Vault.")
                  : qsTr("Uncheck files or folders to keep them Personal.")
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: theme.vaultBody
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            VaultAction {
                objectName: "vaultFolderSelectionUp"
                visible: !picker.selection.personalView && picker.selection.directory !== picker.selection.root
                enabled: !picker.selection.busy
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                quiet: true
                accessibleName: qsTr("Up one folder")
                tooltipText: accessibleName
                Image {
                    anchors.centerIn: parent
                    width: 16; height: 16
                    source: "../assets/icons/lucide/arrow-left.svg"
                }
                onTriggered: picker.library.folderSelectionUp()
            }
            Image {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                source: "../assets/icons/lucide/folder-open.svg"
                opacity: 0.75
            }
            Text {
                Layout.fillWidth: true
                text: picker.selection.personalView ? picker.selection.root || "" : picker.selection.directory || ""
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: theme.vaultCaption
                elide: Text.ElideMiddle
            }
            Rectangle {
                implicitWidth: views.implicitWidth + 6
                implicitHeight: views.implicitHeight + 6
                radius: theme.vaultControlRadius
                color: theme.vaultSurface
                Row {
                    id: views
                    anchors.centerIn: parent
                    spacing: 2
                    VaultLibraryButton {
                        objectName: "vaultFolderBrowseFiles"
                        implicitHeight: 32
                        text: qsTr("Browse files")
                        quiet: true
                        selected: !picker.selection.personalView
                        onClicked: if (!selected) picker.library.showPersonalFolderChoices(false)
                    }
                    VaultLibraryButton {
                        objectName: "vaultFolderPersonalChoices"
                        implicitHeight: 32
                        text: qsTr("Personal")
                        quiet: true
                        selected: !!picker.selection.personalView
                        onClicked: if (!selected) picker.library.showPersonalFolderChoices(true)
                    }
                }
            }
        }
        ListView {
            id: entries
            objectName: "vaultFolderSelectionEntries"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            model: entryModel
            ScrollBar.vertical: HouseScrollBar { id: entryBar; flick: entries; glassStyle: true }
            Rectangle {
                parent: entries
                anchors.fill: parent
                z: -1
                radius: theme.vaultControlRadius
                color: theme.vaultSurface
                border.color: theme.edge
            }
            delegate: Rectangle {
                id: entry
                required property string entryPath
                required property string entryName
                required property bool directory
                required property bool included
                required property bool inherited
                width: entries.width - (entryBar.policy === ScrollBar.AlwaysOn ? entryBar.width : 4)
                height: theme.vaultRowHeight
                radius: theme.vaultControlRadius
                color: include.hovered || disclosure.keyboardAction.hovered ? theme.glassTint : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    spacing: 6
                    CheckBox {
                        id: include
                        objectName: "vaultFolderIncludeEntry"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        leftPadding: 0
                        spacing: 10
                        text: entry.entryName
                        checked: entry.included
                        enabled: !picker.selection.busy && !entry.inherited
                        Accessible.name: entry.entryName
                        Accessible.description: entry.included ? qsTr("Included in Vault") : qsTr("Personal")
                        // Native selection state owns the check, including rejected toggles.
                        nextCheckState: function() { return checkState }
                        onClicked: picker.library.toggleFolderSelectionEntry(entry.entryPath, entry.directory)
                        indicator: Rectangle {
                            width: 17; height: 17
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 4
                            color: include.checked ? theme.gold : "transparent"
                            border.color: include.checked || include.visualFocus ? theme.gold : theme.inkDimmer
                            Shape {
                                anchors.centerIn: parent
                                width: 12; height: 12
                                visible: include.checked
                                ShapePath {
                                    fillColor: "transparent"
                                    strokeColor: theme.vaultGoldInk
                                    strokeWidth: 1.7
                                    capStyle: ShapePath.RoundCap
                                    joinStyle: ShapePath.RoundJoin
                                    startX: 2; startY: 6
                                    PathLine { x: 5; y: 9 }
                                    PathLine { x: 10; y: 3 }
                                }
                            }
                        }
                        contentItem: Item {
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: include.indicator.width + include.spacing
                                spacing: 7
                                Image {
                                    visible: entry.directory
                                    Layout.preferredWidth: 16; Layout.preferredHeight: 16
                                    source: "../assets/icons/lucide/folder-open.svg"
                                    opacity: 0.75
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: include.text
                                    color: entry.included ? theme.ink : theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: theme.vaultBody
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                    VaultAction {
                        id: disclosure
                        objectName: "vaultBrowseFolderEntry"
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        visible: entry.directory && entry.included && !picker.selection.personalView
                        enabled: !picker.selection.busy
                        quiet: true
                        accessibleName: qsTr("Open %1").arg(entry.entryName)
                        tooltipText: accessibleName
                        Image {
                            anchors.centerIn: parent
                            width: 16; height: 16
                            source: "../assets/icons/lucide/chevron-right.svg"
                        }
                        onTriggered: picker.library.browseFolderSelection(entry.entryPath)
                    }
                    Text {
                        Layout.rightMargin: 8
                        visible: !entry.included
                        text: qsTr("Personal")
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: theme.vaultCaption
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                width: Math.max(1, parent.width - 18)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: entries.count === 0
                text: picker.selection.error || (picker.selection.busy ? qsTr("Opening folder…")
                    : picker.selection.personalView ? qsTr("No Personal choices yet.")
                    : picker.selection.more ? qsTr("More entries are available.") : qsTr("This folder is empty."))
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: theme.vaultBody
            }
        }
        Text {
            Layout.fillWidth: true
            visible: entries.count > 0 && text.length > 0
            text: picker.selection.error || ""
            wrapMode: Text.Wrap
            color: theme.gold
            font.family: theme.ui
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !!picker.selection.previous || !!picker.selection.more
            VaultLibraryButton {
                objectName: "vaultFolderSelectionPreviousPage"
                text: qsTr("Previous")
                enabled: !picker.selection.busy && !!picker.selection.previous
                onClicked: picker.library.previousFolderSelectionPage()
            }
            Text {
                text: qsTr("Page %1").arg(picker.selection.page || 1)
                color: theme.inkDim
                font.family: theme.ui
            }
            VaultLibraryButton {
                objectName: "vaultFolderSelectionNextPage"
                text: qsTr("Next")
                enabled: !picker.selection.busy && !!picker.selection.more
                onClicked: picker.library.nextFolderSelectionPage()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            VaultLibraryButton {
                objectName: "vaultCancelFolderSelection"
                text: qsTr("Cancel")
                onClicked: picker.library.cancelFolderSelection()
            }
            VaultLibraryButton {
                objectName: "vaultConfirmFolderSelection"
                text: picker.selection.editing ? qsTr("Save and rescan") : qsTr("Add and index")
                primary: true
                enabled: !picker.selection.busy && !!picker.selection.canSave
                onClicked: picker.library.confirmFolderSelection()
            }
        }
    }
}
