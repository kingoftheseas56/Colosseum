import QtQuick
import QtTest
import "../../qml/UniverseContentApi.js" as ContentApi
import "../../qml/OnePieceEastBlueData.js" as Story
import "../../qml/OnePieceEastBlueAtlasData.js" as Atlas
import "../../qml/UniverseExtApi.js" as Extension

TestCase {
    name: "UniverseContentRefresh"
    property var documents: ({})
    property int generation: 0
    property var dcauWorld: null
    readonly property string observedDcauCover: dcauWorld ? dcauWorld.comicCover({gcdId: 123, cover: "starter-cover"}) : ""
    QtObject {
        id: remoteComics
        property int revision: 0
        property string resolvedCover: ""
        function ready() { return false }
        function available() { return true }
        function series(gcdId) { return gcdId === 123 ? { cover: resolvedCover } : {} }
    }
    property string renderedTitle: { generation; ContentApi.reload(generation); return Story.arc("romance").title }
    function initTestCase() {
        ContentApi.setAssetResolver(function(p) { return p.replace("../assets/", "https://assets.example/") })
        ContentApi.setReader(function(key) { return documents[key] || {} })
    }
    function test_apiRevisionRebindsStoryAndArtInProcess() {
        var revised = JSON.parse(JSON.stringify(Story.arcs))
        revised[0].title = "Updated Romance Dawn"
        documents = { "tribute-one-piece-east-blue": {
            story: { arcs: revised },
            artByArc: { romance: { badgeSource: "../assets/revision/badge.png", posterSource: "https://assets.example/revision/banner.png" } }
        } }
        generation++
        compare(renderedTitle, "Updated Romance Dawn")
        compare(Atlas.badgeSource("romance"), "https://assets.example/revision/badge.png")
        compare(Atlas.bannerSource("romance"), "https://assets.example/revision/banner.png")
        documents = {}
        generation++
        compare(renderedTitle, "Updated Romance Dawn")
    }
    function test_extensionCachesInvalidateAndRejectMissingProviderPins() {
        var result = null
        documents = { "universe-one-piece": { universe: { title: "Revision A", sections: [
            { title: "Films", kind: "video", entries: [ { title: "Pinned", id: "tt0814243", type: "movie" }, { title: "Unpinned", type: "movie" } ] }
        ] } } }
        ContentApi.reload()
        Extension.load("com.colosseum.universe.onepiece", function(value) { result = value })
        compare(result.title, "Revision A")
        compare(result.sections[0].entries.length, 1)
        documents["universe-one-piece"].universe.title = "Revision B"
        ContentApi.reload()
        Extension.load("com.colosseum.universe.onepiece", function(value) { result = value })
        compare(result.title, "Revision B")
        compare(result.sections[0].entries[0].id, "tt0814243")
    }
    function test_changedTemplatesCompile() {
        var files = ["CosmereUniversePage", "DCAUUniversePage", "DCAUWorldPage", "GalaxyUniversePage",
                     "OnePieceUniversePage", "OnePieceEastBlueAtlas", "OnePieceParadiseAtlas",
                     "OnePieceNewWorldAtlas", "OnePieceFinalSeaAtlas", "UniversesWorld",
                     "SagaUniversePage", "ShelvesUniversePage", "UniverseExtensionPage", "OnePieceArcPage"]
        for (var i = 0; i < files.length; ++i) {
            var component = Qt.createComponent("../../qml/" + files[i] + ".qml")
            compare(component.status, Component.Ready, files[i] + ": " + component.errorString())
            component.destroy()
        }
    }
    function test_dcauRemoteCoverRefreshPreservesIdentity() {
        var component = Qt.createComponent("../../qml/DCAUWorldPage.qml")
        compare(component.status, Component.Ready, component.errorString())
        var world = component.createObject(null, { comicCatalogue: remoteComics })
        verify(world !== null)
        dcauWorld = world
        var entry = { gcdId: 123, title: "Pinned comic", cover: "starter-cover", posts: [11366] }
        compare(world.comicCover(entry), "starter-cover")
        compare(observedDcauCover, "starter-cover")
        remoteComics.resolvedCover = "https://assets.example/revised-cover.jpg"
        remoteComics.revision++
        compare(world.comicCover(entry), "https://assets.example/revised-cover.jpg")
        compare(observedDcauCover, "https://assets.example/revised-cover.jpg")
        var route = world.comicRoute(entry)
        compare(route.gcdId, 123)
        compare(route.posts[0], 11366)
        compare(route.cover, "https://assets.example/revised-cover.jpg")
        dcauWorld = null
        world.destroy(); component.destroy()
    }
}
