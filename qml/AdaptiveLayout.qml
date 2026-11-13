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
    property real presentationScale: typeof PlatformRuntime !== "undefined" && PlatformRuntime
        && typeof PlatformRuntime.androidPresentationScale === "number"
        ? PlatformRuntime.androidPresentationScale : 1

    readonly property bool shortAndroid: androidHost && desktopPresentation && viewportHeight > 0 && viewportHeight < 600
    readonly property bool tightAndroidChrome: androidHost && viewportWidth < 900
    readonly property real chromeGap: tightAndroidChrome ? 8 : 18
    property real chromeAvailableWidth: viewportWidth - pageMargin * 2
    readonly property bool stackedWorldChrome: androidHost && chromeAvailableWidth < 1000
    readonly property real chromeUtilityHeight: Math.max(48, touchTargetSize)
    readonly property real chromeRowGap: 4
    readonly property real chromePickerHeight: 46
    readonly property real chromePillPadding: chromeAvailableWidth < 620 ? 24 : 34
    readonly property real chromePillGap: 4
    readonly property real chromePillContentGap: 6
    readonly property real chromePillFontSize: 14
    readonly property real chromeSoonFontSize: 8
    readonly property real chromeSystemGap: tightAndroidChrome ? 8 : 12
    readonly property real chromeClockSize: tightAndroidChrome ? 26 : 32
    readonly property real chromeClockPeriodSize: tightAndroidChrome ? 12 : 16
    readonly property real chromeDateSize: tightAndroidChrome ? 11 : 13
    readonly property real chromeMinimumDateSize: 8
    readonly property real heroCopyInset: shortAndroid ? 20 : 42
    readonly property real heroTitleSize: shortAndroid ? 34 : 48
    readonly property real heroMinimumTitleSize: 24
    readonly property real heroCopyTopInset: 8
    readonly property real heroCoverCopyGap: 20
    readonly property real libraryFilterInset: 10
    readonly property real libraryToolsGap: 12
    readonly property real bookCoverColumnWidth: shortAndroid ? 200 : 268
    readonly property real bookColumnGap: shortAndroid ? 24 : 64
    readonly property real bookCoverGap: shortAndroid ? 14 : 28
    readonly property real bookCoverTopPadding: 16
    readonly property real bookPrimaryHeight: 50
    readonly property real bookTitleSize: shortAndroid ? 32 : 54
    readonly property real bookTaglineSize: shortAndroid ? 20 : 28
    readonly property real mangaHeaderCoverHeight: shortAndroid ? 64 : 164
    readonly property real mangaHeaderCoverTitleMinimum: shortAndroid ? 8 : 17
    readonly property real mangaShelfMinimumHeight: 340
    readonly property real mangaFooterHeight: Math.ceil(targetSize(52))
    function mangaCoverHeight(viewportHeight, captionHeight, itemScale) {
        return Math.max(80, Math.min(276, Math.floor(viewportHeight / itemScale - captionHeight - 16)))
    }
    function bookCoverHeight(availableHeight) {
        return shortAndroid ? Math.max(80, Math.min(402,
            availableHeight - bookCoverTopPadding - bookCoverGap - bookPrimaryHeight - 12)) : 402
    }
    readonly property real availableHeroHeight: Math.max(0, viewportHeight - contentTop - bottomInset - 22)
    readonly property bool compactWelcome: androidHost && viewportWidth >= 600 && viewportHeight > 0 && viewportHeight < 600
    readonly property real welcomeInset: 12
    readonly property real welcomeGap: 8
    readonly property real welcomeSmallGap: 4
    readonly property real welcomeTitleSize: 28
    readonly property real welcomeBodySize: 12
    readonly property real welcomePanelTitleSize: 22
    readonly property real welcomeChoiceTitleSize: 14
    readonly property real welcomeChoiceDetailSize: 11
    readonly property real welcomeChoiceHeight: 52
    readonly property real welcomeChoicePadding: 8

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
    readonly property real topInset: compactChrome || stackedWorldChrome ? 12 : 30
    readonly property real topBarHeight: compactChrome ? 102 : stackedWorldChrome
        ? chromeUtilityHeight + chromeRowGap + chromePickerHeight : 56
    readonly property real contentTop: topInset + topBarHeight + (compactChrome ? 8 : 10)
    readonly property real sectionSpacing: phone ? 22 : (tablet ? 28 : 36)
    // Home historically used a slightly tighter 30 px desktop rhythm than WorldPage's 36 px.
    readonly property real homeSectionSpacing: phone ? 22 : (tablet ? 28 : 30)
    // Input metrics are presentation-independent: Android desktop presentation keeps the
    // desktop composition but still receives finger-sized hit hosts. Desktop returns 0/identity.
    readonly property real touchTargetSize: androidHost
        ? TouchMetrics.minimumTargetSize / Math.min(1, Math.max(0.1, presentationScale)) : 0
    function targetSize(size) { return Math.max(touchTargetSize, size) }
    function targetInset(size) { return (targetSize(size) - size) / 2 }
    readonly property real heroHeight: shortAndroid ? Math.min(340, availableHeroHeight)
                                                  : phone ? 260 : (tablet ? 300 : 340)
    readonly property real homeHeroHeight: shortAndroid ? Math.min(360, availableHeroHeight) : 360
}
