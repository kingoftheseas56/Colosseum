import QtQuick
import QtTest
import "../../qml/update" as Updates
import "../../qml/WallpaperApi.js" as Wallpapers
import "../../qml" as App
import "../../qml/account" as Accounts

TestCase {
    id: testCase
    name: "NativeWallpaperAssets"
    when: windowShown
    width: 960; height: 540
    property var view: null
    property var pickerWindow: null
    readonly property url bundledDefault: Qt.resolvedUrl("../../assets/wallpaper/colosseum-tide.jpg")
    readonly property string redImage: "data:image/svg+xml," + encodeURIComponent("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20'><rect width='20' height='20' fill='red'/></svg>")
    readonly property string blueImage: "data:image/svg+xml," + encodeURIComponent("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20'><rect width='20' height='20' fill='blue'/></svg>")

    QtObject {
        id: content
        property int generation: 1
        property var assets: ({})
        function assetUrl(relative) { return assets[relative] || "" }
    }
    Component {
        id: gallery
        Updates.UpdateLivingGallery {
            width: 960; height: 540
            contentStore: content
        }
    }
    Component {
        id: backdrop
        App.MediaArtworkBackdrop {
            width: 960; height: 540
            contentStore: content
        }
    }
    Component {
        id: sagaPage
        App.SagaUniversePage {
            width: 960; height: 540
            contentStore: content
        }
    }
    Component {
        id: accountFrame
        Accounts.AccountPageFrame {
            width: 960; height: 540
            contentStore: content
        }
    }
    Component {
        id: picker
        Window {
            width: 960; height: 540
            visible: true
            App.WallpaperSearch {
                anchors.fill: parent
                contentStore: content
                inheritedImageUrl: Wallpapers.imageSource(null, content)
                function runSearch() {} // This suite exercises the real shelf without contacting Wallhaven.
            }
        }
    }
    function init() {
        content.assets = ({"assets/wallpaper/colosseum-tide.jpg":redImage})
    }
    function cleanup() {
        if (view) { view.destroy(); view = null }
        if (pickerWindow) { pickerWindow.close(); pickerWindow.destroy(); pickerWindow = null }
    }
    function test_gallery_uses_content_owner_and_refreshes_on_publication_change() {
        view = gallery.createObject(testCase)
        verify(view !== null)
        compare(view.fallbackSource, redImage)
        content.assets["assets/wallpaper/colosseum-tide.jpg"] = blueImage
        compare(view.fallbackSource, redImage)
        content.generation++
        tryCompare(view, "fallbackSource", blueImage)
        content.assets = ({})
        content.generation++
        tryCompare(view, "fallbackSource", String(bundledDefault))
    }
    function test_artwork_backdrop_wallpaper_uses_the_same_content_owner() {
        view = backdrop.createObject(testCase)
        verify(view !== null)
        compare(view.fallbackSource, redImage)
        content.assets["assets/wallpaper/colosseum-tide.jpg"] = blueImage
        content.generation++
        tryCompare(view, "fallbackSource", blueImage)
    }
    function test_default_and_saved_owned_wallpaper_use_published_asset() {
        compare(Wallpapers.imageSource(null, content), redImage)
        compare(Wallpapers.imageSource({image_url:"../assets/wallpaper/colosseum-tide.jpg"}, content), redImage)
        compare(Wallpapers.imageSource({image_url:"https://example.test/chosen.jpg"}, content),
                "https://example.test/chosen.jpg")
        content.assets["assets/wallpaper/colosseum-tide.jpg"] = blueImage
        content.generation++
        compare(Wallpapers.imageSource(null, content), blueImage)
    }
    function test_retired_saved_picks_resolve_to_default() {
        const kdeOrigin = "cdn.jsdelivr.net/gh/KDE/plasma-workspace-wallpapers@master/Flow/contents/images/5120x2880.jpg"
        const savedUrls = ["native:noirflow", "native:unknown",
                           "https://wsrv.nl/?url=" + encodeURIComponent(kdeOrigin) + "&w=3840&output=jpg&q=82",
                           "https://" + kdeOrigin]
        for (const url of savedUrls) {
            const savedPick = JSON.parse(JSON.stringify({image_url: url}))
            compare(Wallpapers.imageSource(savedPick, content), Wallpapers.imageSource(null, content), url)
            compare(Wallpapers.imageSource(savedPick, null), String(bundledDefault), url)
        }
    }
    function test_picker_has_only_the_colosseum_shelf_before_wallhaven() {
        const names = ["Silk", "Folds", "Tide"]
        const assets = {}
        for (const name of names) {
            const stem = "assets/wallpaper/colosseum-" + name.toLowerCase()
            for (const suffix of [".jpg", "-thumb.jpg"])
                assets[stem + suffix] = String(Qt.resolvedUrl("../../" + stem + suffix))
        }
        content.assets = assets
        pickerWindow = picker.createObject(testCase)
        verify(pickerWindow !== null)
        const search = findChild(pickerWindow.contentItem, "wallpaperSearch")
        verify(search !== null)
        const sections = findChild(search, "wallpaperSearchSections")
        const shelf = findChild(search, "wallpaperColosseumShelf")
        const results = findChild(search, "wallpaperWallhavenResults")
        verify(sections && shelf && results)
        compare(sections.children.map(item => item.text || item.objectName),
                ["Colosseum", "wallpaperColosseumShelf", "wallpaperWallhavenResults"],
                "Only the Colosseum shelf may appear before Wallhaven results")
        compare(shelf.count, 3)
        compare(shelf.model.map(pick => pick.title), names)
        compare(search.previewUrl, assets["assets/wallpaper/colosseum-tide.jpg"], "Opening the picker keeps the inherited default (Tide)")
        const preview = findChild(search, "wallpaperSearchPreview")
        for (let index = 0; index < names.length; index++) {
            const name = names[index].toLowerCase()
            const stem = "assets/wallpaper/colosseum-" + name
            const pick = shelf.model[index]
            compare(pick.artist, "Colosseum")
            compare(pick.thumb_url, "../" + stem + "-thumb.jpg")
            compare(pick.image_url, "../" + stem + ".jpg")
            shelf.positionViewAtIndex(index, ListView.Contain)
            const tileName = "wallpaperPick_colosseum_" + name
            tryVerify(() => !!findChild(shelf, tileName))
            const tile = findChild(shelf, tileName)
            const thumb = findChild(tile, "wallpaperPickThumbnail")
            verify(thumb !== null)
            compare(String(thumb.source), assets[stem + "-thumb.jpg"])
            tryCompare(thumb, "status", Image.Ready)
            compare(thumb.sourceSize, Qt.size(480, 270))
            mouseClick(tile, tile.width / 2, tile.height / 2)
            compare(search.selectedPick.source_id, pick.source_id)
            compare(String(preview.source), assets[stem + ".jpg"])
            tryCompare(preview, "status", Image.Ready)
            compare(preview.sourceSize, Qt.size(3840, 2160))
        }
    }
    function test_page_fallbacks_follow_the_content_publication() {
        for (const component of [sagaPage, accountFrame]) {
            view = component.createObject(testCase)
            verify(view !== null)
            compare(view.fallbackWallpaperSource, redImage)
            const image = findChild(view, "fallbackWallpaperImage")
            verify(image !== null)
            tryCompare(image, "status", Image.Ready)
            content.assets["assets/wallpaper/colosseum-tide.jpg"] = blueImage
            content.generation++
            tryCompare(view, "fallbackWallpaperSource", blueImage)
            tryCompare(image, "source", Qt.resolvedUrl(blueImage))
            tryCompare(image, "status", Image.Ready)
            view.contentStore = null
            compare(view.fallbackWallpaperSource, component === accountFrame ? String(bundledDefault) : "")
            view.destroy()
            view = null
            init()
        }
    }
}
