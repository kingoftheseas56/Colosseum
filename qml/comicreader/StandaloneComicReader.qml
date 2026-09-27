import QtQuick

Item {
    id: root
    anchors.fill: parent
    property string seriesId: ""
    property string seriesTitle: ""
    property string seriesCover: ""
    property string unitId: ""
    property string unitLabel: ""
    property var chapters: []

    signal backRequested()
    signal closeRequested()
    signal minimizeRequested()
    signal fullscreenRequested()

    function requestEscape() { shell.closeTop() }

    ComicReaderShell {
        id: shell
        objectName: "comicReaderShell"
        anchors.fill: parent
        focus: true
        western: true
        seriesId: root.seriesId
        seriesTitle: root.seriesTitle
        seriesCover: root.seriesCover
        chapters: root.chapters
        chapterId: root.unitId
        chapterLabel: root.unitLabel
        onBackRequested: root.backRequested()
        onCloseRequested: root.closeRequested()
        onMinimizeRequested: root.minimizeRequested()
        onFullscreenRequested: root.fullscreenRequested()
    }
}
