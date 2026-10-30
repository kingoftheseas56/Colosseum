// AdaptiveLayout - shared width policy for the Android phone/tablet presentation.
// Geometry only: content/models/navigation stay in the existing shared QML pages.
import QtQuick
import "TouchMetrics.js" as TouchMetrics

QtObject {
    id: metrics

    property Item page: null
    function viewportFor(item) {
        let ancestor=item ? item.parent : null
        while (ancestor) {
            if (ancestor.clip) return ancestor
            ancestor=ancestor.parent
        }
        return item && item.Window.window ? item.Window.window.contentItem : item
    }
    readonly property Item viewportItem: viewportFor(page)
    property real viewportWidth: page && viewportItem ? Math.min(page.width, viewportItem.width) : 0
    property real viewportHeight: viewportItem ? viewportItem.height : 0
    property bool touch: Qt.platform.os === "android"
        || !!(page && page["androidHost"] === true)
        || !!(page && page.Window.window && page.Window.window.androidHost)
    property var safeInsets: page ? page.SafeArea.margins : ({left:0, top:0, right:0, bottom:0})
    property real railInset: 0
    property real keyboardInset: 0
    readonly property real usableWidth: Math.max(0, viewportWidth - railInset
        - Math.max(0, safeInsets.left) - Math.max(0, safeInsets.right))
    readonly property real availableHeight: Math.max(0, viewportHeight
        - Math.max(0, safeInsets.top) - Math.max(0, safeInsets.bottom))
    readonly property real usableHeight: Math.max(0, availableHeight - keyboardInset)

    readonly property bool compactPhone: usableWidth <= 430
    readonly property bool phone: usableWidth < 600
    readonly property bool tablet: usableWidth >= 600 && usableWidth < 840
    readonly property bool largeTablet: usableWidth >= 840
    readonly property bool compactChrome: usableWidth < 840
    readonly property bool portrait: viewportHeight > 0 && usableWidth <= availableHeight
    readonly property bool portraitCompact: compactChrome && portrait
    readonly property bool tabletPortrait: tablet && portrait
    readonly property bool shortLandscape: viewportHeight > 0 && availableHeight < 500
        && usableWidth > availableHeight
    readonly property bool wideWithRail: largeTablet && !shortLandscape
    readonly property bool phoneLayout: touch && (compactChrome || shortLandscape)
    readonly property real touchTargetSize: touch ? TouchMetrics.minimumTargetSize : 0
    function canFitRail(railWidth) {
        return usableWidth - Math.max(0, railWidth) >= 840 && !shortLandscape
    }

    readonly property string layoutClass: shortLandscape ? "short-landscape"
                                                : compactPhone ? "compact-phone"
                                                : phone ? "phone"
                                                : tabletPortrait ? "tablet-portrait"
                                                : tablet ? "tablet"
                                                         : "wide-with-rail"

    readonly property real pageMargin: compactPhone ? 18
                                           : phone ? 24
                                           : tablet ? 32
                                                    : 54
    readonly property real topInset: compactChrome ? 12 : 30
    readonly property real topBarHeight: compactChrome ? 102 : 56
    readonly property real contentTop: topInset + topBarHeight + (compactChrome ? 8 : 10)
    readonly property real sectionSpacing: phone ? 22 : (tablet ? 28 : 36)
    // Home historically used a slightly tighter 30 px desktop rhythm than WorldPage's 36 px.
    readonly property real homeSectionSpacing: phone ? 22 : (tablet ? 28 : 30)
    readonly property real heroHeight: phone ? 260 : (tablet ? 300 : 340)
}
