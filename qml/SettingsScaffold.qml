import QtQuick
import "ViewportAvoidance.js" as Viewport

Rectangle {
    id:frame
    color:"transparent"
    property var inputMethod:Qt.inputMethod
    property real inputMethodScale:Qt.platform.os==="android" ? Screen.devicePixelRatio : 1
    readonly property rect logicalKeyboardRectangle: {
        const box=Viewport.logicalKeyboardRect(inputMethod ? inputMethod.keyboardRectangle : null,inputMethodScale)
        return Qt.rect(box.x,box.y,box.width,box.height)
    }
    property var safeInsets:SafeArea.margins
    property real desktopInset:54
    readonly property alias layout:metrics
    readonly property real settingsInset:metrics.touch ? Math.max(metrics.pageMargin,safeInsets.left,safeInsets.right) : desktopInset
    readonly property real keyboardOverlap: {
        if(!metrics.touch || !inputMethod || !inputMethod.visible)return 0
        const keyboard=logicalKeyboardRectangle
        if(!keyboard || keyboard.height<=0)return 0
        return Math.min(height,Math.max(0,height-mapFromItem(null,0,keyboard.y).y))
    }
    AdaptiveLayout { id:metrics; page:frame; safeInsets:frame.safeInsets; keyboardInset:frame.keyboardOverlap }
}
