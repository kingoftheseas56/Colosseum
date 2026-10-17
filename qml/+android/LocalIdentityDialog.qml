// Local identity ceremonies belong to the desktop-only local-media surface.
// Preserve the shell's interface without loading that surface on Android.
import QtQuick

Item {
    property var ceremony: ({})
    readonly property bool opened: false
    signal choiceMade(string relationship, string choice)
    signal cancelRequested()
    function open() {}
    function close() {}
}
