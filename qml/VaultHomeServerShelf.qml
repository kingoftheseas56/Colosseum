pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    objectName: "homeServerVaultShelf"
    property string serverId: ""
    property var profileGeneration: -1
    property string title: ""
    property var rows: []
    signal titleRequested(var row, string serverId, var profileGeneration)
    spacing: 16
    VaultTheme { id: theme }
    Label {
        objectName: "homeServerVaultShelfName"
        Layout.fillWidth: true
        text: root.title
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: theme.ink
        font.family: theme.display
        font.pixelSize: 26
    }
    Label {
        objectName: "homeServerVaultShelfEmpty"
        visible: root.rows.length === 0
        Layout.fillWidth: true
        text: qsTr("No matched titles on this server yet.")
        wrapMode: Text.Wrap
        color: theme.inkDimmer
        font.family: theme.ui
        font.pixelSize: 14
    }
    GridView {
        id: titles
        objectName: "homeServerVaultTitles"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.rows
        cellWidth: width / Math.max(1, Math.floor(width / 220))
        cellHeight: 132
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        delegate: Button {
            id: titleButton
            required property var modelData
            required property int index
            objectName: "homeServerVaultTitle_" + modelData.id
            width: titles.cellWidth - 12
            height: titles.cellHeight - 12
            Accessible.name: modelData.title
            onClicked: root.titleRequested(titleButton.modelData, root.serverId, root.profileGeneration)
            background: Rectangle {
                radius: 12
                color: titleButton.hovered || titleButton.down ? theme.glassHi : theme.panel
                border.color: titleButton.activeFocus ? theme.gold : theme.edge
                border.width: titleButton.activeFocus ? 2 : 1
            }
            contentItem: ColumnLayout {
                spacing: 8
                Label {
                    objectName: "homeServerVaultTitleLabel_" + titleButton.modelData.id
                    Layout.fillWidth: true
                    text: titleButton.modelData.title
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 16
                }
                Label {
                    Layout.fillWidth: true
                    text: [titleButton.modelData.kind === "series" ? qsTr("Series") : qsTr("Movie"),
                           titleButton.modelData.year || ""].filter(value => String(value).length > 0).join(" · ")
                    textFormat: Text.PlainText
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 12
                }
            }
        }
    }
}
