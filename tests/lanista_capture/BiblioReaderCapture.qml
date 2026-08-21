import QtQuick
import "../../qml/reader2" as Reader2

Window {
    id: win
    objectName: "biblioReaderCaptureWindow"
    width: 1280
    height: 720
    visible: true
    title: "Biblio reader capture"
    color: "#000000"

    FontLoader { source: "../../assets/fonts/Fraunces-Regular.ttf" }
    FontLoader { source: "../../assets/fonts/Fraunces-Italic.ttf" }
    FontLoader { source: "../../assets/fonts/Inter-Regular.otf" }
    FontLoader { source: "../../assets/fonts/Inter-SemiBold.otf" }
    FontLoader { source: "../../assets/fonts/Literata-Regular.ttf" }
    FontLoader { source: "../../assets/fonts/Literata-Italic.ttf" }

    readonly property string fixturePath: decodeURIComponent(
        String(Qt.resolvedUrl("../fixtures/vault/real/tiny-book.epub"))
            .replace(/^file:\/\/\//, ""))

    Reader2.ReaderShell {
        id: shell
        objectName: "biblioReaderCaptureShell"
        anchors.fill: parent
        visible: true
        readerDebug: false
        bookMeta: ({ title: "Vault Test Book", author: "The Brotherhood" })
    }

    Component.onCompleted: Qt.callLater(function() { shell.openBook(win.fixturePath) })
}
