import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import "../account" as AccountUi

// A small reserved chrome row. Its parent must place the website below it.
Rectangle {
    id: root
    objectName: "feriaSaveContextBar"
    color: "#202936"
    property var tools: null
    readonly property var snapshot: tools ? tools.contextSnapshot : null
    readonly property bool open: snapshot !== null
    implicitHeight: open ? content.implicitHeight + 12 : 0
    height: implicitHeight
    visible: open
    component ContextButton: AccountUi.AccountButton {
        property string contextIdAtPress: ""
        property Item toolsAtPress: null
        property string contextAction: ""
        property string actionAtPress: ""
        onPressed: { contextIdAtPress = root.snapshot ? root.snapshot.contextId : ""; toolsAtPress = root.tools; actionAtPress = contextAction }
        implicitWidth: Math.max(72, contentItem.implicitWidth + 18)
        implicitHeight: 32
        Accessible.name: text
    }
    Column {
        id: content
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.margins: 6
        spacing: 4
        Flow {
            width: parent.width
            spacing: 6
            ContextButton {
                objectName: "feriaContextSavePage"
                text: qsTr("Save page")
                enabled: root.tools && root.tools.canSavePage
                onClicked: root.tools.savePage(formatChoice.currentValue)
            }
            ComboBox {
                id: formatChoice
                objectName: "feriaSavePageFormat"
                implicitWidth: 150; implicitHeight: 32
                textRole: "text"; valueRole: "value"; currentIndex: 0
                model: [
                    {text: "Web archive", value: WebEngineDownloadRequest.MimeHtmlSaveFormat},
                    {text: "HTML only", value: WebEngineDownloadRequest.SingleHtmlSaveFormat},
                    {text: "Complete HTML", value: WebEngineDownloadRequest.CompleteHtmlSaveFormat}
                ]
                Accessible.name: qsTr("Page save format")
            }
            ContextButton {
                id: resourceAction
                objectName: "feriaContextSaveResource"
                readonly property string kind: root.tools && root.tools.canSaveImage ? "image"
                    : root.tools && root.tools.canSaveMedia ? "media" : "link"
                contextAction: kind
                text: kind === "image" ? qsTr("Save image") : kind === "media" ? qsTr("Save media") : qsTr("Save link")
                visible: root.snapshot && (root.snapshot.linkUrl.length > 0 || root.snapshot.mediaType !== ContextMenuRequest.MediaTypeNone)
                enabled: root.tools && (root.tools.canSaveLink || root.tools.canSaveImage || root.tools.canSaveMedia)
                onClicked: if (resourceAction.toolsAtPress) resourceAction.toolsAtPress.saveFromContext(resourceAction.contextIdAtPress, resourceAction.actionAtPress)
            }
            ContextButton {
                id: openAction
                objectName: "feriaContextOpenLink"
                contextAction: "openLink"
                text: qsTr("Open link")
                visible: root.snapshot && root.snapshot.linkUrl.length > 0
                enabled: root.snapshot && root.tools.admittedSaveUrl(root.snapshot.linkUrl)
                onClicked: if (openAction.toolsAtPress) openAction.toolsAtPress.contextNativeAction(openAction.contextIdAtPress, openAction.actionAtPress)
            }
            ContextButton {
                id: copyAction
                objectName: "feriaContextCopy"
                text: root.snapshot && root.snapshot.linkUrl.length ? qsTr("Copy link")
                    : root.snapshot && (root.snapshot.mediaType === ContextMenuRequest.MediaTypeImage
                        || root.snapshot.mediaType === ContextMenuRequest.MediaTypeCanvas)
                        ? qsTr("Copy image") : qsTr("Copy media link")
                visible: root.snapshot && (root.snapshot.linkUrl.length || root.snapshot.mediaUrl.length
                    || root.snapshot.mediaType === ContextMenuRequest.MediaTypeCanvas)
                contextAction: root.snapshot && root.snapshot.linkUrl.length ? "copyLink"
                    : root.snapshot && (root.snapshot.mediaType === ContextMenuRequest.MediaTypeImage
                        || root.snapshot.mediaType === ContextMenuRequest.MediaTypeCanvas) ? "copyImage" : "copyMediaLink"
                onClicked: if (copyAction.toolsAtPress) copyAction.toolsAtPress.contextNativeAction(copyAction.contextIdAtPress, copyAction.actionAtPress)
            }
            ContextButton {
                objectName: "feriaContextDismiss"
                text: qsTr("Dismiss")
                onClicked: root.tools.invalidateSaveContext("dismissed")
            }
        }
        Text {
            objectName: "feriaContextTarget"
            width: parent.width
            color: "#d8e0eb"; font.pixelSize: 12
            textFormat: Text.PlainText
            text: root.snapshot ? root.snapshot.linkUrl || root.snapshot.mediaUrl || "Current page" : ""
            elide: Text.ElideMiddle
            maximumLineCount: 1
        }
        Text {
            width: parent.width
            color: "#b7bfce"; font.pixelSize: 11
            text: root.snapshot && !root.snapshot.mainFrameOnly
                ? qsTr("Frame-specific saving is unavailable on this engine.")
                : qsTr("Native Save uses Downloads. Protected streams and canvas export are unavailable.")
            wrapMode: Text.WordWrap
        }
    }
}
