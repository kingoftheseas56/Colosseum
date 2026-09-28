import QtQuick

Item {
    id: root
    property string kind: "play"
    property color ink: "#f7f7f5"
    property bool darkGlyph: false
    property bool filled: false
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
        visible: root.kind !== "star" && root.kind !== "layers" && root.kind !== "preview"
        sourceSize.width: Math.max(2, Math.round(width * 2))
        sourceSize.height: Math.max(2, Math.round(height * 2))
        source: Qt.resolvedUrl(root.sourceForKind(root.kind))
        fillMode: Image.PreserveAspectFit
        smooth: true
        cache: true
    }

    Text {
        anchors.centerIn: parent
        visible: root.kind === "star"
        text: root.filled ? "★" : "☆"
        color: root.ink
        font.family: "Segoe UI Symbol"
        font.pixelSize: root.iconSize + 2
    }

    Item {
        visible: root.kind === "preview"
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize

        Rectangle {
            anchors.centerIn: parent
            width: Math.max(14, root.iconSize)
            height: Math.max(10, root.iconSize * 0.72)
            radius: 3
            color: "transparent"
            border.width: 1
            border.color: root.ink
        }

        Text {
            anchors.centerIn: parent
            text: "▶"
            color: root.ink
            font.family: "Segoe UI Symbol"
            font.pixelSize: Math.max(7, root.iconSize * 0.42)
        }
    }

    Item {
        visible: root.kind === "layers"
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize

        Rectangle {
            x: 2
            y: 2
            width: Math.max(12, root.iconSize - 4)
            height: Math.max(7, root.iconSize * 0.42)
            radius: 2
            color: "transparent"
            border.width: 1
            border.color: root.ink
        }
        Rectangle {
            x: 2
            y: Math.round(root.iconSize * 0.33)
            width: Math.max(12, root.iconSize - 4)
            height: Math.max(7, root.iconSize * 0.42)
            radius: 2
            color: "transparent"
            border.width: 1
            border.color: root.ink
        }
        Rectangle {
            x: 2
            y: Math.round(root.iconSize * 0.58)
            width: Math.max(12, root.iconSize - 4)
            height: Math.max(7, root.iconSize * 0.42)
            radius: 2
            color: "transparent"
            border.width: 1
            border.color: root.ink
        }
    }
}
