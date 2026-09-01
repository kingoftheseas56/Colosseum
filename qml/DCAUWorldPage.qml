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
        contentHeight: content.implicitHeight + 54
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: HouseScrollBar { flick: page }
        ScrollGlide { flick: page }

        Column {
            id: content
            width: page.width
            spacing: 38

            Item { width: 1; height: 128 }

            Column {
                id: tankobanShelf
                x: theme.margin
                width: page.width - theme.margin * 2
                spacing: 18
                WidgetHeader { width: parent.width; title: "Tankoban"; moreLabel: "" }
                Flickable {
                    width: parent.width
                    height: 282
                    contentWidth: tankobanRow.width
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: tankobanRow
                        spacing: 24
                        Repeater {
                            model: root.comicItems
                            delegate: PortraitTile {
                                objectName: "dcauComic_" + String(modelData.gcdId || 0) + "_" + String((modelData.posts && modelData.posts.length) ? modelData.posts[0] : 0)
                                required property var modelData
                                posterWidth: 180
                                caption: modelData.title || ""
                                cover: root.comicCover(modelData)
                                onClicked: root.comicRequested(root.comicRoute(modelData))
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
                WidgetHeader { width: parent.width; title: "Theatre"; moreLabel: "" }
                Flickable {
                    width: parent.width
                    height: 352
                    contentWidth: theatreRow.width
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: theatreRow
                        spacing: 26
                        Repeater {
                            model: root.theatreItems
                            delegate: CataloguePosterCard {
                                objectName: "dcauVideo_" + String(modelData.id || "")
                                required property var modelData
                                width: 200
                                height: 352
                                visualProfile: "gallery"
                                hoverSourceText: "IMDb"
                                item: ({ title: modelData.title || "", year: modelData.year || "",
                                         cover: root.videoCover(modelData) })
                                onActivated: root.watchRequested(root.videoRoute(modelData))
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 24 }
        }
    }
}
