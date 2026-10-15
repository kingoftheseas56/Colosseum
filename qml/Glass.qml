// Glass — the Colosseum material. A frosted surface with REAL backdrop blur of whatever
// sits in `backdrop` behind it. Proven in the spine slice; reused for all chrome.
//
// Coordinate note: the blurred region is mapped from this item's position INTO the backdrop's
// coordinate space, so a Glass placed anywhere (not just a direct child) blurs the right patch.
// (Live-tracking blur during vertical SCROLL is a known later problem — v1 home is single-screen.)

import QtQuick
import QtQuick.Effects

Item {
    id: root

    // what to blur (the persistent wallpaper / content layer)
    required property Item backdrop
    property real radius: 18
    property real tint: 0.10          // glass white film
    property real scrim: 0.14         // adaptive scrim: keeps text legible over ANY wallpaper
    property real blurAmount: 1.0
    property int blurMax: 48
    property real blurMultiplier: 0.0
    property color edge: Qt.rgba(1, 1, 1, 0.18)

    // Bind to a scroll offset (e.g. flick.contentY) when this surface lives inside a scroller,
    // so the blurred backdrop region recomputes as the panel moves. Static surfaces leave it 0.
    property real track: 0

    // Optional second layer: content that scrolls UNDER this surface (the world page beneath the
    // docked tab bar). Blurred over the backdrop blur, so the glass frosts the posters passing
    // behind it like the mock's backdrop-filter, not just the wallpaper. Bind contentTrack to the
    // content's scroll offset so the grab follows it.
    property Item contentBackdrop: null
    property real contentTrack: 0

    default property alias content: holder.data

    // GPU TEXTURE CEILING. The blur costs TWO textures the size of this whole item (the
    // backdrop grab and the rounded-rect mask layer). Ask for one bigger than the driver
    // allows and it does not degrade — it refuses, and the card renders as garbage:
    //   QSGRhiLayer: Unsupported size requested: [1758, 54375]. Maximum texture size: 16384
    // (hit live 2026-07-30 by a 232-chapter list inside a Glass card). Content-sized panels
    // can exceed any ceiling, so past a safe bound we stop allocating and fall back to the
    // tint + scrim + border below, which always draw. The card keeps its material identity;
    // it just loses the blur, which is invisible on a panel that tall anyway.
    // 8192 is comfortably under every desktop GPU's limit and far above any blurrable panel.
    readonly property bool blurAffordable: width > 0 && height > 0
                                           && width <= 8192 && height <= 8192

    // this panel's top-left expressed in backdrop coordinates (reactive to x/y AND scroll)
    readonly property point _origin: {
        root.track;   // dependency: re-evaluate when the bound scroll offset changes
        return root.backdrop ? root.mapToItem(root.backdrop, 0, 0) : Qt.point(0, 0);
    }

    ShaderEffectSource {
        id: grab
        anchors.fill: parent
        visible: false
        live: root.blurAffordable
        hideSource: false
        // null source frees the texture outright — `visible: false` would not, since a
        // ShaderEffectSource is a texture provider and allocates regardless.
        sourceItem: root.blurAffordable ? root.backdrop : null
        sourceRect: Qt.rect(root._origin.x, root._origin.y, root.width, root.height)
    }
    // The rounded-corner mask, captured explicitly. The documented form (a hidden item with
    // layer.enabled) rendered EMPTY inside the full app scene (it works in isolation), which
    // masked the blur away on every Glass surface: only the tint and scrim ever showed
    // (found 2026-09-29, Hemanth: "our tabs don't have the glass effect").
    Rectangle {
        id: maskShape
        anchors.fill: parent
        radius: root.radius
        color: "white"
        visible: root.blurAffordable
    }
    ShaderEffectSource {
        id: maskItem
        anchors.fill: parent
        visible: false
        hideSource: true
        live: root.blurAffordable
        sourceItem: root.blurAffordable ? maskShape : null
    }
    MultiEffect {
        anchors.fill: parent
        visible: root.blurAffordable
        source: grab
        autoPaddingEnabled: false
        blurEnabled: root.blurAffordable
        blur: root.blurAmount
        blurMax: root.blurMax
        blurMultiplier: root.blurMultiplier
        maskEnabled: true
        maskSource: maskItem
    }
    readonly property point _contentOrigin: {
        root.contentTrack; root.track;
        return root.contentBackdrop ? root.mapToItem(root.contentBackdrop, 0, 0) : Qt.point(0, 0);
    }
    ShaderEffectSource {
        id: contentGrab
        anchors.fill: parent
        visible: false
        live: root.blurAffordable && root.contentBackdrop !== null
        hideSource: false
        sourceItem: root.blurAffordable ? root.contentBackdrop : null
        sourceRect: Qt.rect(root._contentOrigin.x, root._contentOrigin.y, root.width, root.height)
    }
    MultiEffect {
        anchors.fill: parent
        visible: root.blurAffordable && root.contentBackdrop !== null
        source: contentGrab
        autoPaddingEnabled: false
        blurEnabled: true
        blur: root.blurAmount
        blurMax: root.blurMax
        blurMultiplier: root.blurMultiplier
        maskEnabled: true
        maskSource: maskItem
    }
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: Qt.rgba(1, 1, 1, root.tint)
        border.width: 1
        border.color: root.edge
    }
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: Qt.rgba(0, 0, 0, root.scrim)
    }
    Item { id: holder; anchors.fill: parent }
}
