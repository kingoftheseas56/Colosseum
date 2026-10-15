pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "DCAUUniverseData.js" as DCAU

Item {
    id: root
    objectName: "dcauWorldPage"
    property var hub: ({})
    property var payload: null
    property bool reducedMotion: false
    readonly property var comicItems: DCAU.comicsForHub(root.payload, String(root.hub.id || "gotham"))
    readonly property var theatreItems: DCAU.videosForHub(root.payload, String(root.hub.id || "gotham"))

    signal watchRequested(var entry)
    signal comicRequested(var entry)

    Theme { id: theme }

    function videoCover(entry) {
        return entry && entry.id ? "https://live.metahub.space/poster/small/" + entry.id + "/img" : ""
    }
    function comicCover(entry) {
        if (!entry) return ""
        if (typeof ComicsCatalog !== "undefined" && ComicsCatalog.ready()) {
            var row = ComicsCatalog.series(Number(entry.gcdId || 0))
            if (row && row.cover) return row.cover
        }
        return entry.cover || ""
    }
    function videoRoute(entry) {
        return { id: entry.id, type: entry.type || "series", title: entry.title || "",
                 cover: root.videoCover(entry) }
    }
    function comicRoute(entry) {
        return { gcdId: Number(entry.gcdId || 0), title: entry.title || "",
                 cover: root.comicCover(entry), posts: entry.posts || [] }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: root.hub.environment === "gotham" ? gothamEnv
                       : root.hub.environment === "metropolis" ? metropolisEnv
                       : root.hub.environment === "future" ? futureEnv : spaceEnv
    }
    Component { id: gothamEnv; DCAUEnvironmentGotham { reducedMotion: root.reducedMotion } }
    Component { id: metropolisEnv; DCAUEnvironmentMetropolis { reducedMotion: root.reducedMotion } }
    Component { id: spaceEnv; DCAUEnvironmentSpace { reducedMotion: root.reducedMotion } }
    Component { id: futureEnv; DCAUEnvironmentFutureGotham { reducedMotion: root.reducedMotion } }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: 128 + content.implicitHeight + 50
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: HouseScrollBar { flick: page }
        ScrollGlide { flick: page }

        Column {
            id: content
            width: page.width
            y: 128
            spacing: 38

            Column {
                id: tankobanShelf
                x: theme.margin
                width: page.width - theme.margin * 2
                spacing: 18
                WidgetHeader { width: parent.width; title: "Tankoban"; moreLabel: ""; navigable: false }
                Flickable {
                    width: parent.width
                    height: 278
                    contentWidth: tankobanRow.width + 32
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: tankobanRow
                        x: 6
                        spacing: 24
                        Repeater {
                            model: root.comicItems
                            delegate: DCAUTankCard {
                                objectName: "dcauComic_" + String(modelData.gcdId || 0) + "_" + String((modelData.posts && modelData.posts.length) ? modelData.posts[0] : 0)
                                required property var modelData
                                caption: modelData.title || ""
                                cover: root.comicCover(modelData)
                                onActivated: root.comicRequested(root.comicRoute(modelData))
                            }
                        }
                    }
                }
            }

            Column {
                id: theatreShelf
                x: theme.margin
                width: page.width - theme.margin * 2
                spacing: 18
                WidgetHeader { width: parent.width; title: "Theatre"; moreLabel: ""; navigable: false }
                Flickable {
                    width: parent.width
                    height: 352
                    contentWidth: theatreRow.width + 32
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: theatreRow
                        x: 6
                        spacing: 26
                        Repeater {
                            model: root.theatreItems
                            delegate: DCAUTheatreCard {
                                objectName: "dcauVideo_" + String(modelData.id || "")
                                required property var modelData
                                item: ({ title: modelData.title || "", year: modelData.year || "" })
                                cover: root.videoCover(modelData)
                                onActivated: root.watchRequested(root.videoRoute(modelData))
                            }
                        }
                    }
                }
            }

        }
    }
}
