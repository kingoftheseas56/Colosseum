import QtQuick
import ".."

QtObject {
    id: root
    required property real viewportHeight
    required property real hostBottomInset
    required property real dockBottomExtent
    required property Item playerBar
    required property WorldPage browseShell

    // Main's host forwards the desktop taskbar as an inset too. Only that
    // matching footprint floats; a separate host inset (e.g. phone tabs) stays.
    readonly property real viewportInset: hostBottomInset === dockBottomExtent ? 0 : hostBottomInset
    readonly property real viewportBottom: viewportHeight - viewportInset
    readonly property real overlayExtent: Math.max(dockBottomExtent,
        playerBar.visible ? viewportHeight - playerBar.y : 0)
    readonly property real endPadding: overlayExtent > 0 ? Math.max(0, overlayExtent + 14 - viewportInset) : 0

    property Binding browseInsetBinding: Binding {
        target: root.browseShell
        property: "contentBottomInset"
        value: root.viewportInset
        when: root.browseShell !== null
    }
    property Binding browsePaddingBinding: Binding {
        target: root.browseShell
        property: "contentEndPadding"
        value: root.endPadding
        when: root.browseShell !== null
    }
}
