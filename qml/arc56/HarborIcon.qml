import QtQuick

Item {
    id: root
    property string kind: "play"
    property color ink: "#f7f7f5"
    property bool darkGlyph: false
    property real iconSize: Math.min(width, height)
    Accessible.name: ""

    function sourceForKind(value) {
        if (value === "play" && root.darkGlyph)
            return "../../assets/icons/play-dark.svg"
        switch (value) {
        case "play": return "../../assets/icons/lucide/play.svg"
        case "download": return "../../assets/icons/lucide/download.svg"
        case "eye": return "../../assets/icons/lucide/eye.svg"
        case "eyeOff": return "../../assets/icons/lucide/eye-off.svg"
        case "search": return "../../assets/icons/lucide/search.svg"
        case "list": return "../../assets/icons/lucide/list-video.svg"
        case "check": return "../../assets/icons/lucide/circle-check.svg"
        case "bookmark": return "../../assets/icons/lucide/bookmark-plus.svg"
        case "bookmarkCheck": return "../../assets/icons/lucide/bookmark-check.svg"
        case "star": return "../../assets/icons/star.svg"
        case "info": return "../../assets/icons/lucide/info.svg"
        default: return "../../assets/icons/lucide/circle-alert.svg"
        }
    }

    Image {
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        sourceSize.width: Math.max(2, Math.round(width * 2))
        sourceSize.height: Math.max(2, Math.round(height * 2))
        source: Qt.resolvedUrl(root.sourceForKind(root.kind))
        fillMode: Image.PreserveAspectFit
        smooth: true
        cache: true
    }
}
