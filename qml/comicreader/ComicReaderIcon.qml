import QtQuick
import QtQuick.Effects

// One SVG glyph, tinted to `ink` via MultiEffect colorization — the Comic Reader's icon
// vocabulary. Two provenances, both ORIGINAL to us in the sense the design ledger requires
// (Cover is a black-box UX reference only; none of its assets, icons or brand are ever copied):
//   * vendored Lucide (ISC) strokes, carried with their @license header, and
//   * Colosseum-authored glyphs drawn on the SAME Lucide 24-grid / 2px round-stroke spec so the
//     two sit in one family (the layout + order marks have no Lucide equivalent).
// This MIRRORS qml/PlayerIcon.qml exactly:
// the SVGs (assets/icons/comicreader/, provenance in each file's header) MUST carry stroke="#ffffff",
// because MultiEffect colorization keeps the SVG's alpha coverage and replaces its colour — a
// black-stroke SVG colorizes to black = invisible (reference_multieffect_colorization_needs_white_source).
// The reader never draws a text arrow/character for a glyph: every navigational mark is one of these.
Item {
    id: root

    // The HUD's internal icon vocabulary -> a vendored filename.
    property string kind: ""
    property color  ink: "#f7f7f5"
    property real   iconSize: Math.round(Math.min(width, height) * 0.62)
    property string accessibleName: ""

    Accessible.name: accessibleName

    // Every kind the HUD instantiates -> its vendored Lucide file. An unknown kind renders the close
    // glyph and warns once, so a stray kind is loud in the log, not silently a missing image.
    function fileForKind(k) {
        switch (k) {
        case "back":        return "chevron-left"    // back-to-library pill
        case "prev":        return "chevron-left"    // previous page pill
        case "next":        return "chevron-right"   // next page pill
        case "minimize":    return "minus"           // window verb: minimize to bar
        case "fullscreen":  return "maximize"        // window verb: fullscreen toggle
        case "close":       return "x"               // window verb: close
        case "chapters":    return "chapters"        // chapters / volume navigator
        case "thumbnails":  return "thumbnails"      // thumbnails grid
        case "settings":    return "sliders-horizontal"  // reader settings sheet
        case "loupe":       return "search"          // command bar: the Loupe magnifier
        case "bookmark":                             // command bar: Bookmark (singular command)
        case "bookmarks":   return "bookmark"        // tool grid: bookmarks (plural list)
        case "shortcuts":   return "keyboard"        // tool grid: keyboard shortcuts
        // ---- Task 5 command chrome: Pages / Image, and the two marks that SHOW current state ----
        case "pages":       return "pages"           // command bar: the temporary page filmstrip
        case "image":       return "image"           // command bar: the compact image panel
        case "layoutSingle": return "layout-single"  // current layout: Single Page
        case "layoutPaired": return "layout-paired"  // current layout: Paired Pages
        case "layoutStrip":  return "layout-strip"   // current layout: Long Strip
        case "orderRtl":     return "order-rtl"      // current order: Manga, right-to-left
        case "orderLtr":     return "order-ltr"      // current order: Comic, left-to-right
        default:
            console.warn("ComicReaderIcon: unmapped kind '" + k + "' -> x")
            return "x"
        }
    }

    readonly property url iconSource:
        Qt.resolvedUrl("../../assets/icons/comicreader/" + fileForKind(root.kind) + ".svg")

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
        // a MISSING svg FILE (distinct from an unmapped KIND, which fileForKind already warns on)
        // would otherwise render a silent blank pill — warn loudly so it's caught, not shipped.
        onStatusChanged: if (status === Image.Error)
            console.warn("ComicReaderIcon: failed to load SVG for kind '" + root.kind + "' -> " + root.iconSource)
    }
    // colorization: 1.0 replaces the glyph's colour with `ink`, keeping the SVG's alpha coverage,
    // so the white Lucide stroke tints to the ink shade (or gold when a caller sets ink to gold).
    MultiEffect {
        anchors.fill: glyphImage
        source: glyphImage
        colorization: 1.0
        colorizationColor: root.ink
    }
}
