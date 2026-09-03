// AdaptiveLayout - shared width policy for the Android phone/tablet presentation.
// Geometry only: content/models/navigation stay in the existing shared QML pages.
import QtQml

QtObject {
    id: metrics

    property real viewportWidth: 0

    readonly property bool compactPhone: viewportWidth >= 0 && viewportWidth <= 430
    readonly property bool phone: viewportWidth < 600
    readonly property bool tablet: viewportWidth >= 600 && viewportWidth < 840
    readonly property bool largeTablet: viewportWidth >= 840
    readonly property bool compactChrome: viewportWidth < 840

    readonly property string layoutClass: compactPhone ? "compact-phone"
                                                : phone ? "phone"
                                                : tablet ? "tablet"
                                                         : "large-tablet"

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