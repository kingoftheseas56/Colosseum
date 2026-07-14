// AddonLogo — draws an add-on's real icon the way Harbor's extension page does.
// Priority: (1) a bundled official logo if we ship one (assets/addon-logos,
// matched by id/name in AddonLogos.js — instant, offline, no network stall),
// (2) the add-on's own manifest logo URL when given, (3) the honest coloured
// letter square as the last resort. Mirrors Harbor src/components/addon-logo.tsx,
// with bundled-first ordering because arbitrary logo hosts aren't IPv4-pinned here.
//
// The logo floats on a rounded plate (PreserveAspectFit) rather than an effect-
// masked full-bleed crop: no per-icon MultiEffect FBO (the Discover page shows
// 20+ icons at once), and it renders identically on-GPU and in offscreen grabs.
import QtQuick
import "AddonLogos.js" as AddonLogos

Item {
    id: root

    property string addonId: ""
    property string addonName: ""
    property string manifestLogo: ""       // remote URL (e.g. installed rows' manifest.logo)
    property int size: 44
    property int radius: Math.max(4, Math.round(size * 0.24))
    // fallback letter-square gradient — keeps the store's original monogram look
    property color tone1: "#1a2028"
    property color tone2: "#0f141a"

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    Theme { id: theme }

    readonly property string _bundled: AddonLogos.logoFor(addonId, addonName)
    readonly property string _src: _bundled !== "" ? _bundled
                                                    : (manifestLogo ? manifestLogo : "")
    // bundled logos are local (load synchronously → no flash); a manifest logo is
    // a remote URL on an un-pinned host, so load it async and keep the letter up
    // until it's actually Ready — never a blank square during a network stall.
    readonly property bool _isRemote: _bundled === "" && manifestLogo !== ""
    readonly property bool _showImage: _src !== "" && logo.status === Image.Ready

    // ---- the rounded plate: a soft glass tile under a real logo, or the tone
    //      gradient + letter when we have no logo ----
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        border.width: 1
        border.color: root._showImage ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.14)
        gradient: Gradient {
            GradientStop { position: 0.0; color: root._showImage ? Qt.rgba(1, 1, 1, 0.06) : root.tone1 }
            GradientStop { position: 1.0; color: root._showImage ? Qt.rgba(1, 1, 1, 0.02) : root.tone2 }
        }
        Text {
            anchors.centerIn: parent
            visible: !root._showImage
            text: (root.addonName || "?").charAt(0).toUpperCase()
            color: theme.ink
            font.family: theme.display
            font.pixelSize: Math.round(root.size * 0.46)
        }
    }

    // ---- the real logo, fit inside the plate ----
    Image {
        id: logo
        anchors.fill: parent
        anchors.margins: Math.max(1, Math.round(root.size * 0.06))
        source: root._src
        visible: root._showImage
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignHCenter
        verticalAlignment: Image.AlignVCenter
        smooth: true
        mipmap: true
        asynchronous: root._isRemote
        cache: true
        sourceSize.width: Math.round(root.size * 2)
        sourceSize.height: Math.round(root.size * 2)
    }
}
