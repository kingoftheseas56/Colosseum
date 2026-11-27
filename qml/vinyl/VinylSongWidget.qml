import QtQuick

// Standalone art face; the host owns the caption and input.
VinylTileArt {
    property string artist: ""
    property string uiFont: "Segoe UI"
    readonly property bool includesTitle: false
    readonly property bool includesSubtitle: false
}
