import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Basic 2.15 as Basic

FocusScope {
    id: root
    objectName: "guidePage"
    focus: true

    property var catalog: defaultCatalog
    property var progress: defaultProgress
    property string initialLessonId: ""
    property string originLabel: ""
    property string originContext: ""
    property string originSummary: ""
    property bool reducedMotion: false
    property real presentationOpacity: 1
    property string currentView: "home"
    property string currentSection: "start"
    property var currentLesson: null
    property string searchQuery: ""
    property var searchResults: []
    property bool _clearingSearch: false
    property string noResultFallbackSection: "start"
    property string noResultFixSection: "fix"
    readonly property var visibleLessons: catalog && catalog.publishedLessons ? catalog.publishedLessons : []
    readonly property bool presentationTransitionRunning: presentationTransition.running
    readonly property bool hasKnownOrigin: originLabel.length > 0 && knownContexts.indexOf(originContext) >= 0
    readonly property var knownContexts: ["home", "tankoban", "biblio", "theatre", "house", "downloads", "personalization", "fix"]

    signal closeRequested()
    signal returnRequested()
    signal wallpaperChoiceRequested()

    GuideCatalog { id: defaultCatalog }
    GuideProgress { id: defaultProgress }

    function lessonForId(id) { return catalog && catalog.find ? catalog.find(id) : null }

    function setCurrentView(nextView) {
        currentView = nextView
        presentationTransition.stop()
        if (reducedMotion) {
            presentationOpacity = 1
            return
        }
        presentationOpacity = 0
        presentationTransition.start()
    }

    function openLesson(id) {
        var lesson = lessonForId(id)
        if (!lesson) {
            currentLesson = null
            setCurrentView("home")
            return
        }
        currentLesson = lesson
        currentSection = lesson.section
        setCurrentView("article")
    }

    function openSection(section) {
        currentLesson = null
        currentSection = section === "home" ? "start" : section
        setCurrentView(section === "home" ? "home" : "section")
        _clearingSearch = true
        searchField.text = ""
        _clearingSearch = false
    }

    function search(query) {
        searchQuery = String(query || "")
        searchResults = catalog && catalog.search ? catalog.search(searchQuery, originContext) : []
        if (!searchQuery.length) {
            setCurrentView("home")
            return
        }
        noResultFallbackSection = knownContexts.indexOf(originContext) >= 0 && originContext !== "home" ? originContext : "start"
        setCurrentView("search")
    }

    function activateSearchResult(index) {
        if (index >= 0 && index < searchResults.length) openLesson(searchResults[index].id)
    }

    function openNoResultFallback() { openSection(noResultFallbackSection) }
    function toggleIndexDrawer() { guideIndex.drawerOpen = !guideIndex.drawerOpen }
    function requestReturn() { returnRequested() }
    function requestClose() { closeRequested() }

    function openPopularPath(label) {
        if (label === "Something is not working") openSection("fix")
        else if (label === "Open media from this device") openSection("house")
        else if (label === "Choose and enable a source") openSection("downloads")
        else openSection("start")
    }

    Component.onCompleted: {
        if (initialLessonId.length > 0) openLesson(initialLessonId)
    }

    Keys.onEscapePressed: requestClose()

    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: root.requestClose()
    }

    NumberAnimation {
        id: presentationTransition
        target: root
        property: "presentationOpacity"
        to: 1
        duration: 140
    }

    Rectangle { anchors.fill: parent; color: "#0b0b0b" }

    GuideIndex {
        id: guideIndex
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        currentSection: root.currentSection
        onSectionRequested: root.openSection(section)
    }

    Basic.Button {
        visible: guideIndex.drawerMode
        z: 11
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        text: guideIndex.drawerOpen ? "Close index" : "Index"
        activeFocusOnTab: true
        onClicked: root.toggleIndexDrawer()
    }

    Flickable {
        id: scroll
        anchors.left: guideIndex.drawerMode ? parent.left : guideIndex.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: contextStrip.visible ? contextStrip.top : parent.bottom
        anchors.leftMargin: guideIndex.drawerMode ? 0 : 1
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight + 48
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            opacity: root.presentationOpacity
            width: Math.min(scroll.width - 48, 820)
            x: Math.max(24, (scroll.width - width) / 2)
            y: 28
            spacing: 20

            GuideSearchField {
                id: searchField
                width: parent.width
                onQueryChanged: if (!root._clearingSearch) root.search(query)
                onAccepted: root.search(text)
            }
            GuideHome {
                id: guideHome
                width: parent.width
                visible: root.currentView === "home"
                height: visible ? implicitHeight : 0
                progress: root.progress
                onPopularPathRequested: root.openPopularPath(label)
            }
            Column {
                visible: root.currentView === "section" || root.currentView === "search"
                width: parent.width
                spacing: 9
                Text { text: root.currentView === "search" ? "SEARCH" : "SECTION"; color: "#969696"; font.pixelSize: 11; font.letterSpacing: 1.1 }
                Text { text: root.currentView === "search" ? root.searchQuery : root.currentSection; color: "#f4f4f4"; font.pixelSize: 26; font.weight: Font.DemiBold }
                Text { visible: root.currentView === "search" && root.searchResults.length === 0; text: "No local Guide result. Try this stable section or Fix a problem."; color: "#c8c8c8"; width: parent.width; wrapMode: Text.WordWrap }
                Row {
                    visible: root.currentView === "search" && root.searchResults.length === 0
                    spacing: 10
                    Basic.Button { objectName: "guideNoResultNearestAction"; text: "Open " + root.noResultFallbackSection; activeFocusOnTab: true; onClicked: root.openNoResultFallback() }
                    Basic.Button { objectName: "guideNoResultFixAction"; text: "Fix a problem"; activeFocusOnTab: true; onClicked: root.openSection("fix") }
                }
                Repeater {
                    model: root.currentView === "search" ? root.searchResults : (root.catalog && root.catalog.section ? root.catalog.section(root.currentSection) : [])
                    delegate: Basic.Button {
                        width: parent.width
                        height: 50
                        text: modelData.title
                        activeFocusOnTab: true
                        contentItem: Text { text: parent.text; color: "#ededed"; verticalAlignment: Text.AlignVCenter; leftPadding: 12; font.pixelSize: 15 }
                        background: Rectangle { color: parent.hovered ? "#252525" : "#181818"; border.color: parent.activeFocus ? "#efefef" : "#383838"; radius: 2 }
                        onClicked: root.openLesson(modelData.id)
                    }
                }
            }
            GuideArticle {
                width: parent.width
                visible: root.currentView === "article"
                height: visible ? implicitHeight : 0
                lesson: root.currentLesson
                catalog: root.catalog
                reducedMotion: root.reducedMotion
                onRelatedRequested: root.openLesson(lessonId)
            }
        }
    }

    Connections {
        target: guideHome
        function onWallpaperChoiceRequested() { root.wallpaperChoiceRequested() }
    }

    GuideContextStrip {
        id: contextStrip
        z: 2
        anchors.left: guideIndex.drawerMode ? parent.left : guideIndex.right
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: guideIndex.drawerMode ? 0 : 1
        originLabel: root.originLabel
        summary: root.originSummary
        recognized: root.hasKnownOrigin
        onRelevantHelpRequested: root.openSection(root.originContext)
        onReturnRequested: root.requestReturn()
    }
}
