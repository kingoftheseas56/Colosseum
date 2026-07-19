import QtQuick
import QtQuick.Effects

// One vendored Lucide SVG glyph, tinted to `ink` via MultiEffect colorization. Replaces the
// retired hand-drawn Canvas IconGlyph across the player transport. The SVGs are the pinned
// lucide-static@0.460.0 subset under assets/icons/lucide/ (provenance in that dir's SOURCE.txt);
// they are vendored at build time by scripts/vendor_lucide_player_icons.ps1, never fetched at runtime.
Item {
    id: root

    // The player's internal icon vocabulary (RoundButton.icon). Mapped to a vendored filename.
    property string kind: ""
    property color  ink: "#f7f7f5"
    property real   iconSize: Math.round(Math.min(width, height) * 0.46)
    property string accessibleName: ""

    Accessible.name: accessibleName

    // Every kind the live player actually instantiates -> its Lucide file. Unknown kinds render
    // circle-alert and warn once, so a stray kind is loud in the log, not silently invisible.
    function fileForKind(k) {
        switch (k) {
        case "play":           return "play"
        case "pause":          return "pause"
        case "back":           return "arrow-left"
        case "prevEpisode":    return "skip-back"
        case "nextEpisode":    return "skip-forward"
        case "seekBack":       return "rotate-ccw"
        case "seekForward":    return "rotate-cw"
        case "retry":          return "rotate-cw"
        case "fullscreen":     return "maximize"
        case "fullscreenExit": return "minimize"
        case "minimizeToBar":  return "minus"
        case "cancel":         return "x"
        case "volume":         return "volume-2"
        case "mute":           return "volume-x"
        case "audio":          return "audio-lines"
        case "subtitle":       return "captions"
        case "stream":         return "gallery-horizontal-end"
        case "fit":            return "scan"
        default:
            console.warn("PlayerIcon: unmapped kind '" + k + "' -> circle-alert")
            return "circle-alert"
        }
    }

    readonly property url iconSource:
        Qt.resolvedUrl("../assets/icons/lucide/" + fileForKind(root.kind) + ".svg")

    Image {
        id: glyphImage
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        // Rasterize the SVG at ~2x for crisp downscaling at 125%/150% Windows scaling.
        sourceSize.width: Math.max(2, Math.round(root.iconSize * 2))
        sourceSize.height: Math.max(2, Math.round(root.iconSize * 2))
        source: root.iconSource
        fillMode: Image.PreserveAspectFit
        smooth: true
        cache: true
        visible: false   // hidden source; MultiEffect draws the tinted copy
    }
    // colorization: 1.0 replaces the glyph's colour with `ink`, keeping the SVG's alpha coverage,
    // so the Lucide stroke geometry tints white (or gold when active) exactly like the old glyphs.
    MultiEffect {
        anchors.fill: glyphImage
        source: glyphImage
        colorization: 1.0
        colorizationColor: root.ink
    }
}
