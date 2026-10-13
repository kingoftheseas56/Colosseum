import QtQuick
import QtQuick.Controls
import ".." as Colosseum

Item {
    id: root
    objectName: "feriaSiteIdentity"
    property url currentUrl: "about:blank"
    property bool loading: false
    property bool documentLoaded: false
    property color foreground: theme.ink
    readonly property string canonicalOrigin: FeriaBrowserPolicy.displayOrigin(currentUrl)
    readonly property string originText: canonicalOrigin === "about:blank" ? qsTr("New page")
        : canonicalOrigin || qsTr("Address unavailable")
    readonly property bool plainHttp: String(currentUrl).toLowerCase().indexOf("http://") === 0
    readonly property bool https: String(currentUrl).toLowerCase().indexOf("https://") === 0
    readonly property string connectionText: plainHttp ? qsTr("Not encrypted (HTTP)")
        : https ? (loading ? qsTr("Connecting…") : documentLoaded ? qsTr("HTTPS connection") : qsTr("Connection unverified"))
        : String(currentUrl).indexOf("blob:") === 0 ? qsTr("Page content") : qsTr("No site connection")
    implicitWidth: 240
    implicitHeight: 36
    Accessible.role: Accessible.StaticText
    Accessible.name: originText + ". " + connectionText
    ToolTip.visible: hover.hovered
    ToolTip.delay: 400
    ToolTip.text: originText + "\n" + connectionText
        + (https && documentLoaded && !loading ? "\n" + qsTr("Encryption does not verify the site's trustworthiness.") : "")
    HoverHandler { id: hover }
    Colosseum.Theme { id: theme }
    Column {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        spacing: 1
        Text {
            objectName: "feriaSiteOrigin"
            width: parent.width
            text: root.originText
            textFormat: Text.PlainText
            elide: Text.ElideLeft
            color: root.foreground
            font.family: theme.ui
            font.pixelSize: 13
            Accessible.ignored: true
        }
        Text {
            objectName: "feriaConnectionStatus"
            width: parent.width
            text: root.connectionText
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: root.plainHttp ? theme.gold : theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 11
            Accessible.ignored: true
        }
    }
}
