import QtQuick
import "UniverseApi.js" as UniverseApi

Item {
    id: root
    property var entry: ({})
    property string kind: "video"
    property bool keyboardFocused: false
    property string resolvedCover: ""
    signal activated(var entry)

    width: 148
    height: kind === "book" ? 286 : 274

    readonly property string sourceLabel:
        kind === "video" ? "IMDb" : (kind === "book" ? "Apple Books" : "")

    readonly property var displayItem: ({
        title: (entry && entry.title) || "",
        year: (entry && entry.year) || "",
        author: (entry && entry.author) || "",
        cover: resolvedCover
    })

    function resolveCover() {
        resolvedCover = ""
        var e = root.entry || ({})
        if (!e.title) return
        if (root.kind === "video" && e.id) {
            resolvedCover = "https://images.metahub.space/poster/small/" + e.id + "/img"
            return
        }
        UniverseApi.coverFor(e, root.kind, function(url) {
            if (root.entry === e && url) root.resolvedCover = url
        })
    }
    Component.onCompleted: resolveCover()
    onEntryChanged: resolveCover()
    onKindChanged: resolveCover()

    CataloguePosterCard {
        anchors.fill: parent
        item: root.displayItem
        visualProfile: "gallery"
        keyboardFocused: root.keyboardFocused
        revealOnFocus: root.kind === "book"
        showAuthorAtRest: root.kind === "book"
        hoverSourceText: root.sourceLabel
        onActivated: root.activated(root.entry)
    }
}
