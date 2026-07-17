// HarnessShelf.qml — the harness book shelf. Lists the REAL downloaded books
// (booksDir context property → <AppData>/Brotherhood/Colosseum/books, read-only)
// via FolderListModel and emits bookChosen(filePath) on click. Not shipped chrome
// — just the entry point that lets a human open a book without a Colosseum shell.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import Qt.labs.folderlistmodel

Rectangle {
    id: root
    color: "#0d0f13"
    signal bookChosen(string path)

    // booksDir is a plain filesystem path (context property); FolderListModel wants a URL.
    property url booksFolder: "file:///" + booksDir

    Column {
        anchors.fill: parent
        anchors.margins: 32
        spacing: 18

        Text {
            text: "reader2 — pick a book"
            color: "#e6e1d5"
            font.pixelSize: 22
            font.bold: true
        }
        Text {
            text: booksDir
            color: Qt.rgba(1, 1, 1, 0.4)
            font.pixelSize: 12
            elide: Text.ElideMiddle
            width: parent.width
        }
        Text {
            visible: shelf.count === 0
            text: "No books found in this folder. Download a book in Colosseum first."
            color: Qt.rgba(1, 1, 1, 0.5)
            font.pixelSize: 14
        }

        ListView {
            id: shelf
            width: parent.width
            height: parent.height - y
            clip: true
            spacing: 4

            model: FolderListModel {
                id: books
                folder: root.booksFolder
                nameFilters: ["*.epub", "*.mobi", "*.azw3", "*.fb2", "*.fbz", "*.pdf", "*.txt", "*.cbz"]
                showDirs: false
                sortField: FolderListModel.Name
            }

            delegate: Rectangle {
                required property string fileName
                required property string filePath
                width: ListView.view.width
                height: 44
                color: hov.hovered ? "#1a1e26" : "transparent"
                radius: 6

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    text: fileName
                    color: "#e6e1d5"
                    font.pixelSize: 15
                    elide: Text.ElideRight
                }
                HoverHandler { id: hov }
                TapHandler { onTapped: root.bookChosen(filePath) }
            }
        }
    }
}
