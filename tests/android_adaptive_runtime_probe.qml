// W03 one-shot viewport probe. Run with qml.exe on an offscreen platform.
import QtQuick
import QtQuick.Window
import "../qml"

Window {
    id: root
    width: 1000
    height: 1000
    visible: true

    Rectangle { id: backdrop; anchors.fill: parent; color: "#101218" }

    AdaptiveLayout { id: at390; viewportWidth: 390 }
    AdaptiveLayout { id: at430; viewportWidth: 430 }
    AdaptiveLayout { id: at600; viewportWidth: 600 }
    AdaptiveLayout { id: at840; viewportWidth: 840 }

    TopBar {
        id: phoneTopBar
        visible: false
        width: 354
        height: implicitHeight
        backdrop: backdrop
        androidHost: true
    }
    WorldPage {
        id: phoneWorld
        visible: false
        width: 390
        height: 800
        backdrop: backdrop
        medium: "Tankoban"
        androidHost: true
    }

    FeaturedCarousel {
        id: phoneHero
        visible: false
        width: 354
        slides: [{ title: "Viewport", blurb: "Phone hero", ghost: "T",
                   c1: "#302050", c2: "#101018" }]
    }

    Bookshelf {
        id: phoneShelf
        visible: false
        width: 354
        backdrop: backdrop
        mangaBooks: []
        comicsBooks: []
    }
    VaultHomeWidget {
        id: phoneVault
        visible: false
        width: 354
        backdrop: backdrop
    }

    WorldTabBar {
        id: phoneWorldTabs
        visible: false
        width: 354
        backdrop: backdrop
        tabModel: [
            { key: "discover", label: "Discover" },
            { key: "manga", label: "Manga" },
            { key: "comics", label: "Comics" },
            { key: "library", label: "Library" }
        ]
        currentTab: "discover"
    }

    TheatreTabBar {
        id: phoneTheatreTabs
        visible: false
        width: 354
        backdrop: backdrop
    }
    title: "W3|390=" + at390.layoutClass + "/" + at390.pageMargin + "/" + at390.topBarHeight + "/" + at390.heroHeight
         + "|430=" + at430.layoutClass + "/" + at430.pageMargin
         + "|600=" + at600.layoutClass + "/" + at600.pageMargin + "/" + at600.topBarHeight + "/" + at600.heroHeight
         + "|840=" + at840.layoutClass + "/" + at840.pageMargin + "/" + at840.topBarHeight + "/" + at840.heroHeight
         + "|top=" + phoneTopBar.layoutClass + "/" + phoneTopBar.implicitHeight + "/" + phoneTopBar.desktopWindowControlsVisible
         + "|world=" + phoneWorld.layoutClass + "/" + phoneWorld.pageMargin
         + "|hero=" + phoneHero.implicitHeight
         + "|shelf=" + phoneShelf.compactLayout + "/" + phoneShelf.height
         + "|vault=" + phoneVault.compactLayout + "/" + phoneVault.height
         + "|tabs=" + phoneWorldTabs.compactLayout + "/" + phoneTheatreTabs.compactLayout
}
