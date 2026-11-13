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
    property real viewportHeight: 0
    property real bottomInset: 0

    readonly property bool shortAndroid: androidHost && desktopPresentation && viewportHeight > 0 && viewportHeight < 600
    readonly property bool tightAndroidChrome: androidHost && viewportWidth < 900
    readonly property real chromeGap: tightAndroidChrome ? 8 : 18
    readonly property real chromePillPadding: tightAndroidChrome ? 6 : 12
    readonly property real chromePillGap: tightAndroidChrome ? 2 : 4
    readonly property real chromePillContentGap: tightAndroidChrome ? 3 : 6
    readonly property real chromePillFontSize: tightAndroidChrome ? 12 : 14
    readonly property real chromeSoonFontSize: tightAndroidChrome ? 7 : 8
    readonly property real chromeSystemGap: tightAndroidChrome ? 8 : 12
    readonly property real chromeClockSize: tightAndroidChrome ? 26 : 32
    readonly property real chromeClockPeriodSize: tightAndroidChrome ? 12 : 16
    readonly property real chromeDateSize: tightAndroidChrome ? 11 : 13
    readonly property real heroCopyInset: shortAndroid ? 20 : 42
    readonly property real heroTitleSize: shortAndroid ? 34 : 48
    readonly property real availableHeroHeight: Math.max(0, viewportHeight - contentTop - bottomInset - 22)

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
    // Restore desktop gutters while retaining the existing adaptive phone margins.
    readonly property real outerPageMargin: pageMargin
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
    readonly property real heroHeight: shortAndroid ? Math.min(340, availableHeroHeight)
                                                  : phone ? 260 : (tablet ? 300 : 340)
    readonly property real homeHeroHeight: shortAndroid ? Math.min(360, availableHeroHeight) : 360
}
