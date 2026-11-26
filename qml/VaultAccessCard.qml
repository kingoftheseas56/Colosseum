// Android only: the one card that asks for "All files access" before the folder chooser.
// Same glass panel and buttons as the Vault folder screen; the page decides when it shows.
import QtQuick
import QtQuick.Layouts

FocusScope {
    id: card
    objectName: "vaultAccessCard"
    property Item backdrop: null
    signal openSettingsRequested()
    signal cancelRequested()

    Theme { id: theme }
    focus: true
    Keys.onEscapePressed: event => { event.accepted = true; card.cancelRequested() }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.45)
        MouseArea { anchors.fill: parent }
    }
    Item {
        id: panel
        anchors.centerIn: parent
        width: Math.min(460, card.width - 32)
        height: body.implicitHeight + 44
        Glass { anchors.fill: parent; backdrop: card.backdrop; radius: theme.vaultRadius; scrim: 0.38 }
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
            id: body
            x: 22; y: 22
            width: parent.width - 44
            spacing: 14
            Text {
                Layout.fillWidth: true
                text: qsTr("Allow access to your files")
                color: theme.ink
                font.family: theme.display
                font.pixelSize: theme.vaultSectionTitle
                wrapMode: Text.Wrap
            }
            Text {
                objectName: "vaultAccessCardText"
                Layout.fillWidth: true
                text: qsTr("Colosseum needs All files access to read your folders. It only reads the folders you add.")
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: theme.vaultBody
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.fillWidth: true }
                VaultLibraryButton {
                    objectName: "vaultAccessCardCancel"
                    text: qsTr("Cancel")
                    quiet: true
                    onClicked: card.cancelRequested()
                }
                VaultLibraryButton {
                    objectName: "vaultAccessCardOpenSettings"
                    text: qsTr("Open Android settings")
                    primary: true
                    onClicked: card.openSettingsRequested()
                }
            }
        }
    }
}
