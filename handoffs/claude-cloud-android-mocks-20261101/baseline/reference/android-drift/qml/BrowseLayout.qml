import QtQuick

AdaptiveLayout {
    property real desktopMargin:54
    property real railExtent:0
    readonly property bool compactBrowse:phoneLayout || (touch && railExtent>0 && !canFitRail(railExtent))
    readonly property bool portraitBrowse:compactBrowse && portrait
    readonly property real browseMargin:touch ? Math.max(pageMargin,safeInsets.left,safeInsets.right) : desktopMargin
}
