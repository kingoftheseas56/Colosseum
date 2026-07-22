import QtQuick
import QtQuick.Effects

// One vendored Lucide glyph, tinted to `ink` via MultiEffect colorization — the same technique as
// the production PlayerIcon, so Player 2's chrome matches the current player's iconography exactly.
// The SVGs ship white-stroke, so colorization replaces the colour while keeping the alpha coverage.
Item {
    id: root

    property string kind: ""
    property color ink: "#f7f7f5"
    property real iconSize: Math.min(width, height) * 0.46
    property alias accessibleName: root._accessibleName
    property string _accessibleName: ""

    // Internal vocabulary -> Lucide file name (mirrors the production PlayerIcon mapping).
    readonly property var _map: ({
        "play": "play", "pause": "pause", "back": "arrow-left",
        "prevEpisode": "skip-back", "nextEpisode": "skip-forward",
        "seekBack": "rotate-ccw", "seekForward": "rotate-cw", "retry": "rotate-cw",
        "fullscreen": "maximize", "fullscreenExit": "minimize",
        "minimizeToBar": "minus", "cancel": "x",
        "volume": "volume-2", "mute": "volume-x",
        "stream": "replace", "download": "download", "check": "circle-check",
        "warning": "triangle-alert", "fit": "sliders-horizontal",
        "episodes": "list-video", "audio": "languages", "subtitle": "captions",
        "speed": "gauge"
    })
    readonly property string _file: _map[kind] !== undefined ? _map[kind] : "circle-alert"
    readonly property url iconSource: Qt.resolvedUrl("../icons/" + _file + ".svg")

    Accessible.role: Accessible.Graphic
    Accessible.name: _accessibleName

    Image {
        id: glyph
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        sourceSize.width: Math.max(2, Math.round(root.iconSize * 2))
        sourceSize.height: Math.max(2, Math.round(root.iconSize * 2))
        source: root.iconSource
        fillMode: Image.PreserveAspectFit
        smooth: true
        visible: false // hidden source; the tinted copy is drawn below
    }

    MultiEffect {
        anchors.fill: glyph
        source: glyph
        colorization: 1.0
        colorizationColor: root.ink
    }
}
