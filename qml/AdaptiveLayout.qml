// AdaptiveLayout - shared desktop and phone presentation policy.
// Geometry only: content/models/navigation stay in the existing shared QML pages.
import QtQml
import "TouchMetrics.js" as TouchMetrics

QtObject {
    id: metrics

    property bool desktopPresentation: true
    // Android host capability; production derives it from the platform, fixtures may set it.
    property bool androidHost: Qt.platform.os === "android"
    property real viewportWidth: 0

    readonly property bool compactPhone: !desktopPresentation && viewportWidth >= 0 && viewportWidth <= 430
    readonly property bool phone: !desktopPresentation && viewportWidth < 600
    readonly property bool tablet: !desktopPresentation && viewportWidth >= 600 && viewportWidth < 840
    readonly property bool largeTablet: desktopPresentation || viewportWidth >= 840
    readonly property bool compactChrome: !desktopPresentation && viewportWidth < 840

    readonly property string layoutClass: compactPhone ? "compact-phone"
                                                : phone ? "phone"
                                                : tablet ? "tablet"
                                                         : "large-tablet"

    readonly property real pageMargin: compactPhone ? 18
                                           : phone ? 24
                                           : tablet ? 32
                                                    : 54
    // Installed Android desktop-presentation composition: content runs edge to edge.
    // Desktop and phone layouts keep the adaptive page margin.
    readonly property real outerPageMargin: androidHost && desktopPresentation ? 0 : pageMargin
    readonly property real topInset: compactChrome ? 12 : 30
    readonly property real topBarHeight: compactChrome ? 102 : 56
    readonly property real contentTop: topInset + topBarHeight + (compactChrome ? 8 : 10)
    readonly property real sectionSpacing: phone ? 22 : (tablet ? 28 : 36)
    // Home historically used a slightly tighter 30 px desktop rhythm than WorldPage's 36 px.
    readonly property real homeSectionSpacing: phone ? 22 : (tablet ? 28 : 30)
    // Input metrics are presentation-independent: Android desktop presentation keeps the
    // desktop composition but still receives finger-sized hit hosts. Desktop returns 0/identity.
    readonly property real touchTargetSize: androidHost ? TouchMetrics.minimumTargetSize : 0
    function targetSize(size) { return androidHost ? Math.max(TouchMetrics.minimumTargetSize, size) : size }
    readonly property real heroHeight: phone ? 260 : (tablet ? 300 : 340)
}