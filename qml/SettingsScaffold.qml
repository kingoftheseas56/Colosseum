import QtQuick

Rectangle {
    id:frame
    color:"transparent"
    property var inputMethod:Qt.inputMethod
    property var safeInsets:SafeArea.margins
    property real desktopInset:54
    readonly property alias layout:metrics
    readonly property real settingsInset:metrics.touch ? Math.max(metrics.pageMargin,safeInsets.left,safeInsets.right) : desktopInset
    readonly property real keyboardOverlap: {
        if(!metrics.touch || !inputMethod || !inputMethod.visible)return 0
        const keyboard=inputMethod.keyboardRectangle
        if(!keyboard || keyboard.height<=0)return 0
        return Math.min(height,Math.max(0,height-mapFromItem(null,0,keyboard.y).y))
    }
    AdaptiveLayout { id:metrics; page:frame; safeInsets:frame.safeInsets; keyboardInset:frame.keyboardOverlap }
}
