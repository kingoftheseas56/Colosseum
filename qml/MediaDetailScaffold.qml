import QtQuick

Item {
    id: scaffold
    property Item pageOwner: parent
    property Item coverItem: null
    property Item summaryItem: null
    property Item actionsItem: null
    property Item sectionsItem: null
    property real desktopCoverWidth: 268
    property real desktopCoverHeight: 402
    property real compactCoverWidth: 208
    property real compactCoverHeight: 312
    property real desktopColumnGap: 64
    property real desktopActionGap: 28
    property real compactRowGap: 20
    property real footerPadding: 36
    property bool centerCover: true
    property bool showCover: true
    property bool bottomAlignCover: false
    property real desktopBodyWidth: -1
    property real bodyRightInset: 0
    property bool backdropMode: false
    property real contentInset: 0
    property real desktopHeroHeight: 360
    property real desktopSummaryBottom: 34
    property real compactHeroTail: 46
    property real contentTop: 0
    property bool manageDesktop: true
    readonly property alias layout: metrics
    readonly property bool phoneLayout: metrics.phoneLayout
    readonly property bool shortPhone: metrics.touch && metrics.shortLandscape
    readonly property real coverHeight: backdropMode ? (phoneLayout ? compactCoverHeight : height) : shortPhone
        ? Math.max(144, Math.min(desktopCoverHeight, metrics.viewportHeight - 132))
        : phoneLayout ? compactCoverHeight : desktopCoverHeight
    readonly property real coverWidth: backdropMode ? width : shortPhone ? coverHeight * 2/3
        : phoneLayout ? Math.min(compactCoverWidth, Math.max(0, width - 24)) : desktopCoverWidth
    readonly property bool stackColumns: backdropMode ? phoneLayout : phoneLayout || width < coverWidth + desktopColumnGap + 320
    readonly property real coverBlockHeight: showCover && coverItem ? coverItem.implicitHeight || coverItem.height : 0
    readonly property real summaryBlockHeight: summaryItem ? summaryItem.implicitHeight || summaryItem.height : 0
    readonly property real actionsBlockHeight: actionsItem ? actionsItem.implicitHeight || actionsItem.height : 0
    readonly property real sectionsBlockHeight: sectionsItem ? sectionsItem.implicitHeight || sectionsItem.height : 0
    readonly property int columns: stackColumns ? 1 : 2
    readonly property real textX: backdropMode || stackColumns ? contentInset : !showCover ? 0 : coverWidth + desktopColumnGap
    readonly property real textWidth: Math.max(0, !stackColumns && desktopBodyWidth >= 0 ? desktopBodyWidth : backdropMode || stackColumns ? width-contentInset*2 : width - textX - bodyRightInset)
    readonly property real summaryY: backdropMode ? phoneLayout ? compactCoverHeight+compactRowGap : height-summaryBlockHeight-desktopSummaryBottom
        : contentTop + (stackColumns && showCover && coverItem ? coverBlockHeight + compactRowGap : 0)
    readonly property real actionsY: stackColumns ? summaryY + summaryBlockHeight + 12
        : coverBlockHeight + desktopActionGap
    readonly property real sectionsY: stackColumns
        ? actionsItem ? actionsY + actionsBlockHeight + compactRowGap : summaryY + summaryBlockHeight
        : summaryBlockHeight
    implicitHeight: backdropMode ? phoneLayout ? compactCoverHeight+summaryBlockHeight+compactHeroTail : desktopHeroHeight
        : (stackColumns ? sectionsItem ? sectionsY + sectionsBlockHeight : actionsItem ? actionsY + actionsBlockHeight : summaryItem ? summaryY + summaryBlockHeight : contentTop + coverBlockHeight
        : Math.max(coverBlockHeight + (actionsItem ? desktopActionGap + actionsBlockHeight : 0), summaryBlockHeight + sectionsBlockHeight)) + footerPadding

    AdaptiveLayout { id:metrics; page:scaffold.pageOwner }
    Binding { target:scaffold.coverItem; property:"x"; value:scaffold.stackColumns && scaffold.centerCover ? Math.max(0,(scaffold.width-scaffold.coverWidth)/2) : 0; when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.coverItem !== null }
    Binding { target:scaffold.coverItem; property:"width"; value:scaffold.coverWidth; when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.coverItem !== null }
    Binding { target:scaffold.coverItem; property:"height"; value:scaffold.coverHeight; when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.backdropMode && scaffold.coverItem !== null }
    Binding { target:scaffold.coverItem; property:"y"; value:scaffold.contentTop + (!scaffold.stackColumns && scaffold.bottomAlignCover ? Math.max(0,scaffold.summaryBlockHeight-scaffold.coverBlockHeight) : 0); when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.coverItem !== null }
    Binding { target:scaffold.summaryItem; property:"x"; value:scaffold.textX; when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.summaryItem !== null }
    Binding { target:scaffold.summaryItem; property:"y"; value:scaffold.summaryY; when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.summaryItem !== null }
    Binding { target:scaffold.summaryItem; property:"width"; value:scaffold.textWidth; when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.summaryItem !== null }
    Binding { target:scaffold.actionsItem; property:"x"; value:scaffold.stackColumns ? scaffold.contentInset : scaffold.coverItem ? scaffold.coverItem.x : 0; when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.actionsItem !== null }
    Binding { target:scaffold.actionsItem; property:"y"; value:scaffold.actionsY; when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.actionsItem !== null }
    Binding { target:scaffold.actionsItem; property:"width"; value:scaffold.stackColumns ? scaffold.width-scaffold.contentInset*2 : scaffold.coverWidth; when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.actionsItem !== null }
    Binding { target:scaffold.sectionsItem; property:"x"; value:scaffold.textX; when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.sectionsItem !== null }
    Binding { target:scaffold.sectionsItem; property:"y"; value:scaffold.sectionsY; when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.sectionsItem !== null }
    Binding { target:scaffold.sectionsItem; property:"width"; value:scaffold.textWidth; when:(scaffold.manageDesktop || scaffold.phoneLayout) && scaffold.sectionsItem !== null }
}
