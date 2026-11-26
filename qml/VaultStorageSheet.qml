pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

FocusScope {
    id: root
    objectName: "vaultStorageSheet"
    required property var adapter
    property bool open: false
    property bool editingIgnore: false
    property Item returnFocus: null
    property Item confirmationInvoker: null
    property var pendingSource: null
    signal dismissed()
    signal addFolderRequested()
    signal allowAccessRequested()
    signal connectServerRequested()
    signal editFolderRequested(string path)

    Theme { id: theme }
    visible: open || slide.running
    onOpenChanged: if (!open) {
        pendingSource = null
        confirmationInvoker = null
        editingIgnore = false
    }

    function restoreFocus(target) {
        Qt.callLater(function() {
            if (target && target.visible && target.enabled)
                target.forceActiveFocus(Qt.PopupFocusReason)
        })
    }

    function show(invoker) {
        returnFocus = invoker || null
        editingIgnore = false
        pendingSource = null
        open = true
        restoreFocus(closeButton)
    }

    function cancelForget() {
        pendingSource = null
        restoreFocus(confirmationInvoker || closeButton)
        confirmationInvoker = null
    }

    function finishEditing() {
        editingIgnore = false
        restoreFocus(patternsButton)
    }

    function close() {
        if (pendingSource) {
            cancelForget()
        } else if (editingIgnore) {
            finishEditing()
        } else if (open) {
            open = false
            dismissed()
            restoreFocus(returnFocus)
        }
    }

    Keys.onEscapePressed: event => { root.close(); event.accepted = true }

    Rectangle {
        anchors.fill: parent
        color: theme.vaultScrim
        opacity: root.open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: theme.vaultMotionDuration } }
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    Rectangle {
        id: panel
        objectName: "vaultSourcesPanel"
        x: root.open ? root.width - width - 24 : root.width + 24
        y: theme.vaultSidebarTop
        width: Math.min(theme.vaultSheetWidth, root.width - 48)
        height: Math.min(Math.max(0, root.height - y - 24),
                         root.pendingSource ? panelContent.implicitHeight + 40 : root.height)
        radius: theme.vaultRadius
        color: theme.vaultSurface
        border.color: theme.edge
        Behavior on x { NumberAnimation { id: slide; duration: theme.vaultMotionDuration; easing.type: Easing.OutCubic } }
        MouseArea { anchors.fill: parent }

        Rectangle {
            objectName: "vaultSourcesReadabilitySurface"
            anchors.fill: parent
            anchors.margins: 1
            radius: panel.radius - 1
            color: theme.vaultSurface
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.lighter(theme.vaultSurfaceHi, 1.12) }
                GradientStop { position: 1; color: theme.vaultSurface }
            }
        }

        ColumnLayout {
            id: panelContent
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12
            enabled: root.open

            RowLayout {
                Layout.fillWidth: true
                Text {
                    objectName: "vaultSourcesTitle"
                    Layout.fillWidth: true
                    text: root.pendingSource ? qsTr("Forget this source?")
                          : root.editingIgnore ? qsTr("Ignore patterns") : qsTr("Sources")
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: theme.vaultSectionTitle
                    wrapMode: Text.Wrap
                }
                VaultLibraryButton {
                    id: closeButton
                    objectName: "vaultSourcesClose"
                    iconPath: root.pendingSource || root.editingIgnore
                              ? "../assets/icons/lucide/arrow-left.svg" : "../assets/icons/lucide/x.svg"
                    accessibleName: root.pendingSource || root.editingIgnore ? qsTr("Back to sources") : qsTr("Close sources")
                    onClicked: root.close()
                }
            }

            Text {
                objectName: "vaultSourcesSubtitle"
                Layout.fillWidth: true
                visible: !root.pendingSource
                text: root.editingIgnore
                      ? qsTr("One pattern per line. Matching files stay outside your library.")
                      : qsTr("Manage the folders and servers Vault uses.")
                wrapMode: Text.Wrap
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: theme.vaultBody
            }

            RowLayout {
                visible: !root.editingIgnore && !root.pendingSource
                Layout.fillWidth: true
                spacing: 8
                VaultLibraryButton {
                    objectName: "vaultSourcesAddFolder"
                    text: qsTr("Add folder")
                    iconPath: "../assets/icons/lucide/folder-open.svg"
                    primary: true
                    onClicked: root.addFolderRequested()
                }
                VaultLibraryButton {
                    objectName: "vaultSourcesConnectServer"
                    text: qsTr("Connect a server")
                    onClicked: { root.close(); root.connectServerRequested() }
                }
            }

            Rectangle {
                visible: !root.editingIgnore && !root.pendingSource
                Layout.fillWidth: true
                implicitHeight: 1
                color: theme.edge
            }

            Text {
                visible: !root.editingIgnore && !root.pendingSource
                text: qsTr("Folders on this PC")
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: theme.vaultBody
                font.weight: Font.DemiBold
            }

            ListView {
                id: folders
                objectName: "vaultSourcesFolderList"
                visible: !root.editingIgnore && !root.pendingSource
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                boundsBehavior: Flickable.StopAtBounds
                model: root.adapter.roots
                ScrollBar.vertical: HouseScrollBar { id: folderBar; flick: folders; glassStyle: true }
                delegate: Rectangle {
                    id: folder
                    required property var modelData
                    width: folders.width - (folderBar.policy === ScrollBar.AlwaysOn ? folderBar.width : 0)
                    height: details.implicitHeight + 24
                    radius: theme.vaultControlRadius
                    color: theme.glassTint
                    border.color: theme.edge

                    ColumnLayout {
                        id: details
                        x: 12; y: 12
                        width: parent.width - 24
                        spacing: 8
                        SourceIdentity { Layout.fillWidth: true; sourceData: folder.modelData }
                        Text {
                            objectName: "vaultSourcesStatusText"
                            Layout.fillWidth: true
                            text: [folder.modelData.subtitle, folder.modelData.state].filter(Boolean).join(" · ")
                            elide: Text.ElideRight
                            color: folder.modelData.offline ? theme.gold : theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: theme.vaultCaption
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            VaultLibraryButton {
                                objectName: "vaultSourcesRescan"
                                text: qsTr("Rescan")
                                implicitHeight: 34
                                enabled: !root.adapter.busy && !folder.modelData.accessOff
                                onClicked: root.adapter.rescan(folder.modelData.path)
                            }
                            VaultLibraryButton {
                                objectName: "vaultSourcesAllowAccess"
                                text: qsTr("Allow access again")
                                primary: true
                                implicitHeight: 34
                                visible: !!folder.modelData.accessOff
                                onClicked: root.allowAccessRequested()
                            }
                            VaultLibraryButton {
                                objectName: "vaultSourcesEditFolder"
                                text: qsTr("Choose files")
                                implicitHeight: 34
                                visible: !folder.modelData.synthetic && !!root.adapter.library
                                         && typeof root.adapter.library.openFolderSelection === "function"
                                onClicked: root.editFolderRequested(folder.modelData.path)
                            }
                            VaultLibraryButton {
                                id: forgetButton
                                objectName: "vaultSourcesForget"
                                text: qsTr("Forget")
                                implicitHeight: 34
                                onClicked: {
                                    root.confirmationInvoker = forgetButton
                                    root.pendingSource = { name: folder.modelData.name, path: folder.modelData.path }
                                    root.restoreFocus(cancelButton)
                                }
                            }
                        }
                    }
                }
                Text {
                    visible: folders.count === 0
                    width: parent.width
                    text: qsTr("No folders added yet.")
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: theme.vaultBody
                }
            }

            VaultLibraryButton {
                id: patternsButton
                objectName: "vaultEditIgnorePatterns"
                visible: !root.editingIgnore && !root.pendingSource
                text: qsTr("Ignore patterns")
                iconPath: "../assets/icons/lucide/sliders-horizontal.svg"
                onClicked: {
                    ignoreInput.text = root.adapter.scanIgnore().join("\n")
                    root.editingIgnore = true
                    root.restoreFocus(ignoreInput)
                }
            }

            ScrollView {
                visible: root.editingIgnore
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    id: ignoreInput
                    objectName: "vaultIgnorePatterns"
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: theme.vaultBody
                    wrapMode: TextEdit.Wrap
                    background: Rectangle {
                        radius: theme.vaultControlRadius
                        color: theme.glassTint
                        border.color: ignoreInput.activeFocus ? theme.gold : theme.edge
                    }
                }
            }

            RowLayout {
                visible: root.editingIgnore
                Layout.alignment: Qt.AlignRight
                VaultLibraryButton {
                    objectName: "vaultCancelIgnorePatterns"
                    text: qsTr("Cancel")
                    onClicked: root.finishEditing()
                }
                VaultLibraryButton {
                    objectName: "vaultSaveIgnorePatterns"
                    text: qsTr("Save patterns")
                    primary: true
                    onClicked: {
                        root.adapter.setScanIgnore(ignoreInput.text.split(/\r?\n/).map(s => s.trim()).filter(s => s.length))
                        root.finishEditing()
                    }
                }
            }

            ColumnLayout {
                objectName: "vaultForgetSourceConfirmation"
                visible: !!root.pendingSource
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 14
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: selectedFolder.implicitHeight + 24
                    radius: theme.vaultControlRadius
                    color: theme.glassTint
                    border.color: theme.edge
                    SourceIdentity {
                        id: selectedFolder
                        anchors.fill: parent
                        anchors.margins: 12
                        sourceData: root.pendingSource
                        pathObjectName: "vaultForgetSourcePath"
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Vault will stop watching this folder and remove its entries from your library. Your files stay on your drive.")
                    wrapMode: Text.Wrap
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: theme.vaultBody
                }
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    VaultLibraryButton {
                        id: cancelButton
                        objectName: "vaultCancelForgetSource"
                        text: qsTr("Cancel")
                        onClicked: root.cancelForget()
                    }
                    VaultLibraryButton {
                        objectName: "vaultConfirmForgetSource"
                        text: qsTr("Forget source")
                        onClicked: {
                            const path = root.pendingSource.path
                            root.cancelForget()
                            root.adapter.forgetRoot(path)
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }

    component SourceIdentity: RowLayout {
        id: identity
        required property var sourceData
        property string pathObjectName: "vaultSourcesPathText"
        spacing: 10
        Rectangle {
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            radius: theme.vaultControlRadius
            color: theme.vaultSurface
            border.color: theme.edge
            Image {
                anchors.centerIn: parent
                source: "../assets/icons/lucide/folder-open.svg"
                width: 17; height: 17
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: identity.sourceData?.name || qsTr("Folder")
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: theme.vaultBody
                font.weight: Font.DemiBold
                elide: Text.ElideMiddle
            }
            Text {
                objectName: identity.pathObjectName
                Layout.fillWidth: true
                text: identity.sourceData?.path || ""
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: theme.vaultSmall
                elide: Text.ElideMiddle
            }
        }
    }
}
