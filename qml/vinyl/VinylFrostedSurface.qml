import QtQuick
import QtQuick.Shapes

Item {
    id: root
    property VinylWallpaperFrost frost: null
    property real radius: 15
    property bool hovered: false
    readonly property Item wallpaperTexture: frost ? frost.texture : null
    readonly property point wallpaperOrigin: {
        if (!wallpaperTexture || !visible) return Qt.point(0, 0);
        // mapToItem alone isn't reactive to ancestor movement. Track the Grid,
        // page positioners and Flickable content item as well as the direct parent.
        for (let item = root; item; item = item.parent) { item.x; item.y; }
        for (let item = frost.wallpaper; item; item = item.parent) { item.x; item.y; }
        return root.mapToItem(frost.wallpaper, 0, 0);
    }

    Rectangle {
        anchors.fill: parent
        visible: !root.wallpaperTexture
        radius: root.radius
        color: "black"
    }
    Loader {
        anchors.fill: parent
        active: !!root.wallpaperTexture
        visible: active
        sourceComponent: Shape {
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: -1
                fillItem: root.wallpaperTexture
                fillTransform: {
                    if (!root.wallpaperTexture) return PlanarTransform.fromTranslate(0, 0);
                    const frost = root.frost;
                    const sx = frost.wallpaper.width * frost.pixelRatio / frost.textureSize.width;
                    const sy = frost.wallpaper.height * frost.pixelRatio / frost.textureSize.height;
                    return Qt.matrix4x4(sx, 0, 0, -root.wallpaperOrigin.x,
                                        0, sy, 0, -root.wallpaperOrigin.y,
                                        0, 0, 1, 0,
                                        0, 0, 0, 1);
                }
                PathRectangle { width: root.width; height: root.height; radius: root.radius }
            }
        }
    }
    Rectangle {
        objectName: root.objectName + "_film"
        anchors.fill: parent
        radius: root.radius
        color: Qt.rgba(1, 1, 1, 0.10)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, root.hovered ? 0.44 : 0.18)
    }
}
