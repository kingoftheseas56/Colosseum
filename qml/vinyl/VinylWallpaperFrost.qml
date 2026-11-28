import QtQuick
import QtQuick.Effects

// One wallpaper-only blur for Home. No cards, chrome or scroll contents enter this texture.
Item {
    id: root
    objectName: "vinylWallpaperFrost"
    property Image wallpaper: null
    readonly property Item texture: available ? frozen : null
    readonly property bool available: GraphicsInfo.api !== GraphicsInfo.Unknown
        && GraphicsInfo.api !== GraphicsInfo.Software && wallpaper !== null
        && wallpaper.status === Image.Ready && wallpaper.width > 0 && wallpaper.height > 0
    readonly property size rasterSize: available
        ? Qt.size(Math.ceil(wallpaper.width / 2), Math.ceil(wallpaper.height / 2)) : Qt.size(0, 0)
    // The final texture uses physical pixels in Qt Quick Shapes' coordinate system.
    readonly property real pixelRatio: Screen.devicePixelRatio
    readonly property size textureSize: Qt.size(Math.ceil(rasterSize.width * pixelRatio),
                                                Math.ceil(rasterSize.height * pixelRatio))

    ShaderEffectSource {
        id: viewport
        objectName: "vinylFrostWallpaperCapture"
        width: root.rasterSize.width
        height: root.rasterSize.height
        visible: false
        sourceItem: root.available ? root.wallpaper : null
        textureSize: root.textureSize
        live: false
        smooth: true
    }
    MultiEffect {
        id: blur
        objectName: "vinylFrostWallpaperBlur"
        width: viewport.width
        height: viewport.height
        source: viewport
        autoPaddingEnabled: false
        blurEnabled: root.available
        blur: 1
        blurMax: 24 // at half-scale, for a soft ~24 px CSS-style backdrop blur
    }
    ShaderEffectSource {
        id: frozen
        objectName: "vinylFrostWallpaperTexture"
        width: viewport.width
        height: viewport.height
        visible: false
        hideSource: true
        sourceItem: root.available ? blur : null
        textureSize: root.textureSize
        live: false
        smooth: true
    }
    // Invalidate only for a different wallpaper or viewport, never for Home layout/scroll.
    function refresh() {
        if (!available) return;
        viewport.scheduleUpdate();
        frozen.scheduleUpdate();
    }
    onAvailableChanged: refresh()
    onTextureSizeChanged: refresh()
    onWallpaperChanged: refresh()
    Connections {
        target: root.wallpaper
        function onSourceChanged() { root.refresh(); }
        function onWidthChanged() { root.refresh(); }
        function onHeightChanged() { root.refresh(); }
    }
    Connections {
        target: root.Window.window
        // A recreated graphics context has lost both cached textures.
        function onSceneGraphInitialized() { root.refresh(); }
    }
}
