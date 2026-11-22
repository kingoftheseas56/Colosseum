pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes

FocusScope {
    id: picker

    objectName: "vaultFolderSelection"
    required property var selection
    required property var library
    property Item backdrop: null
    readonly property bool compact: width < 540
    readonly property var location: selection.location || ({})
    readonly property string displayPath: (selection.personalView ? selection.root : selection.directory) || ""
    readonly property string volumeLabel: location.volume
        || (/^[A-Za-z]:/.test(displayPath) ? displayPath.slice(0, 2).toUpperCase() : "/")
    readonly property string pathSeparator: "  ›  "
    readonly property string relativeLocation: location.segments
        ? location.segments.join(pathSeparator)
        : displayPath.replace(/^(?:[A-Za-z]:)?\/+/, "").split("/").join(pathSeparator)
    readonly property int rowInset: 8
    readonly property int scrollGutter: entryBar.width + 4

    component PickerButton: VaultLibraryButton {
        centerContent: true
        padding: 0
    }

    focus: true

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

    Glass {
        anchors.fill: parent
        backdrop: picker.backdrop
        radius: theme.vaultRadius
        scrim: 0.38
    }
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
        anchors.margins: picker.compact ? 12 : 22
        spacing: 12
        Keys.onEscapePressed: event => {
            event.accepted = true
            picker.library.cancelFolderSelection()
        }
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    objectName: "vaultFolderSelectionTitle"
                    Layout.fillWidth: true
                    text: qsTr("Choose what to index")
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: picker.compact ? theme.vaultSectionTitle : theme.vaultPageTitle
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: picker.selection.personalView
                          ? qsTr("Select an item to include it in Vault.")
                          : qsTr("Uncheck any files or folders you want Vault to skip.")
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: theme.vaultBody
                    wrapMode: Text.Wrap
                }
            }
            PickerButton {
                objectName: "vaultCloseFolderSelection"
                Layout.preferredWidth: 38
                Layout.alignment: Qt.AlignVCenter
                iconPath: "../assets/icons/lucide/x.svg"
                iconSize: 24
                accessibleName: qsTr("Close folder picker")
                onClicked: picker.library.cancelFolderSelection()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: theme.vaultSurface
            border.color: theme.edge
            radius: theme.vaultControlRadius
            objectName: "vaultFolderBreadcrumb"
            Accessible.role: Accessible.StaticText
            Accessible.name: picker.relativeLocation
                ? qsTr("%1, %2").arg(picker.volumeLabel).arg(picker.relativeLocation) : picker.volumeLabel
            Accessible.description: picker.location.nativePath || ""
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 9
                VaultAction {
                    objectName: "vaultFolderSelectionUp"
                    visible: !picker.selection.personalView && !!picker.selection.canGoUp
                    enabled: !picker.selection.busy
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                    quiet: true
                    accessibleName: qsTr("Up one folder")
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
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        objectName: "vaultFolderVolumeLabel"
                        Layout.maximumWidth: picker.relativeLocation ? Math.max(64, picker.width * 0.39) : picker.width - 70
                        text: picker.volumeLabel
                        textFormat: Text.PlainText
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: theme.vaultCaption
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: picker.relativeLocation.length > 0
                        text: picker.pathSeparator
                        textFormat: Text.PlainText
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: theme.vaultCaption
                        font.weight: Font.DemiBold
                    }
                    Text {
                        objectName: "vaultFolderLocationLabel"
                        Layout.fillWidth: true
                        text: picker.relativeLocation
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
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Rectangle {
                implicitWidth: views.implicitWidth + 6
                implicitHeight: views.implicitHeight + 6
                radius: theme.vaultControlRadius
                color: theme.vaultSurface
                Row {
                    id: views
                    anchors.centerIn: parent
                    spacing: 2
                    PickerButton {
                        objectName: "vaultFolderBrowseFiles"
                        implicitHeight: 32
                        text: qsTr("Browse files")
                        quiet: true
                        selected: !picker.selection.personalView
                        onClicked: if (!selected) picker.library.showPersonalFolderChoices(false)
                    }
                    PickerButton {
                        objectName: "vaultFolderExcludedChoices"
                        implicitHeight: 32
                        text: picker.selection.excludedCount > 0
                            ? qsTr("Excluded (%L1)").arg(picker.selection.excludedCount) : qsTr("Excluded")
                        quiet: true
                        selected: !!picker.selection.personalView
                        onClicked: if (!selected) picker.library.showPersonalFolderChoices(true)
                    }
                }
            }
            Text {
                objectName: "vaultFolderSelectionCount"
                Layout.fillWidth: true
                Layout.rightMargin: picker.scrollGutter + picker.rowInset
                visible: !picker.compact && !picker.selection.busy && !picker.selection.personalView
                    && entryModel.count > 0
                horizontalAlignment: Text.AlignRight
                text: picker.selection.more || picker.selection.previous
                    ? qsTr("%L1 of %L2 selected on this page").arg(picker.selection.includedCount || 0).arg(entryModel.count)
                    : qsTr("%L1 of %L2 selected").arg(picker.selection.includedCount || 0).arg(entryModel.count)
                elide: Text.ElideRight
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: theme.vaultCaption
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.edge }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: theme.vaultRowHeight

            ListView {
                id: entries
                objectName: "vaultFolderSelectionEntries"
                width: parent.width
                height: Math.floor(parent.height / theme.vaultRowHeight) * theme.vaultRowHeight
                anchors.verticalCenter: parent.verticalCenter
                snapMode: ListView.SnapToItem
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                model: entryModel
                ScrollBar.vertical: HouseScrollBar { id: entryBar; flick: entries; glassStyle: true }
                delegate: Rectangle {
                    id: entry
                    required property string entryPath
                    required property string entryName
                    required property bool directory
                    required property bool included
                    required property bool inherited
                    width: entries.width - picker.scrollGutter
                    height: theme.vaultRowHeight
                    radius: theme.vaultControlRadius
                    color: include.hovered || disclosure.keyboardAction.hovered ? theme.glassTint : "transparent"
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        height: 1
                        color: theme.edge
                        opacity: 0.45
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: picker.rowInset
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
                            Accessible.description: entry.included ? qsTr("Included in Vault")
                                : entry.inherited ? qsTr("Excluded by a parent folder") : qsTr("Excluded")
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
                            Image {
                                anchors.centerIn: parent
                                width: 16; height: 16
                                source: "../assets/icons/lucide/chevron-right.svg"
                            }
                            onTriggered: picker.library.browseFolderSelection(entry.entryPath)
                        }
                        Text {
                            objectName: "vaultFolderEntryStatus"
                            Layout.rightMargin: picker.rowInset
                            visible: !picker.compact || !entry.included
                            text: entry.included ? qsTr("Included") : qsTr("Excluded")
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
                        : picker.selection.personalView ? qsTr("No excluded files or folders.")
                        : picker.selection.more ? qsTr("More entries are available.") : qsTr("This folder is empty."))
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: theme.vaultBody
                }
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
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: pagination.visible
            color: theme.edge
        }
        RowLayout {
            id: pagination
            Layout.fillWidth: true
            visible: !!picker.selection.previous || !!picker.selection.more
            PickerButton {
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
            PickerButton {
                objectName: "vaultFolderSelectionNextPage"
                text: qsTr("Next")
                enabled: !picker.selection.busy && !!picker.selection.more
                onClicked: picker.library.nextFolderSelectionPage()
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.edge }
        RowLayout {
            Layout.fillWidth: true
            Text {
                objectName: "vaultFolderSelectionFooterNote"
                Layout.fillWidth: true
                // Footer note: sized to its text so the folder list above gets the free height.
                visible: !picker.compact
                text: qsTr("Excluded files and folders stay on your drive.")
                verticalAlignment: Text.AlignVCenter
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: theme.vaultCaption
                wrapMode: Text.Wrap
            }
            Item { Layout.fillWidth: true; visible: picker.compact }
            PickerButton {
                objectName: "vaultCancelFolderSelection"
                text: qsTr("Cancel")
                onClicked: picker.library.cancelFolderSelection()
            }
            PickerButton {
                objectName: "vaultConfirmFolderSelection"
                text: picker.selection.editing ? qsTr("Save and rescan") : qsTr("Add and index")
                primary: true
                enabled: !picker.selection.busy && !!picker.selection.canSave
                onClicked: picker.library.confirmFolderSelection()
            }
        }
    }
}
