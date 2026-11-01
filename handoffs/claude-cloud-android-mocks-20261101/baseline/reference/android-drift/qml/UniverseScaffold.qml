import QtQuick

Item {
    id:frame
    property bool androidHost:Qt.platform.os==="android" || !!(Window.window && Window.window.androidHost)
    property real desktopPageInset:54
    property real contentBottomInset:0
    readonly property alias layout:metrics
    readonly property bool phoneLayout:metrics.phoneLayout
    readonly property bool shortPhone:metrics.touch && metrics.shortLandscape
    readonly property bool compactUniverse:metrics.touch ? metrics.phoneLayout : width<800
    readonly property bool portraitPosters:metrics.touch && metrics.phone && metrics.portrait
    readonly property real universeInset:metrics.touch ? Math.max(metrics.pageMargin,metrics.safeInsets.left,metrics.safeInsets.right) : desktopPageInset
    readonly property real bandCopyTop:metrics.touch ? Math.max(84,metrics.safeInsets.top+64) : 120
    readonly property bool showWindowActions:!androidHost
    readonly property real standardPosterWidth:metrics.touch ? Math.max(64,Math.min(150,Math.floor((width-universeInset*2-16)/2))) : 150
    readonly property real standardPosterHeight:metrics.touch ? Math.round(standardPosterWidth*1.5) : 236
    readonly property real captionHeight:metrics.touch ? Math.max(56,titleMetrics.height*2+yearMetrics.height+12) : 56
    function bandHeight(copyHeight,desktopHeight) {
        return metrics.touch ? Math.max(metrics.shortLandscape ? 180 : metrics.heroHeight,bandCopyTop+copyHeight+32) : desktopHeight
    }
    function posterWidth(available,columns,minimum,maximum,gap) {
        return Math.max(minimum,Math.min(maximum,Math.floor((available-(columns-1)*gap)/columns)))
    }
    AdaptiveLayout { id:metrics;page:frame;touch:frame.androidHost }
    Theme { id:theme }
    FontMetrics { id:titleMetrics;font.family:theme.ui;font.pixelSize:theme.fontSize(12) }
    FontMetrics { id:yearMetrics;font.family:theme.ui;font.pixelSize:theme.fontSize(11) }
}
