import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum

TestCase {
    name: "VaultPageBackground"
    when: windowShown

    Window {
        id: testWindow
        width: 1000
        height: 720
        visible: true

        Rectangle {
            id: wallpaper
            anchors.fill: parent
            color: "#24364a"
        }
        Rectangle {
            id: previousPage
            anchors.fill: parent
            color: "#ff00ff"
        }
        Colosseum.VaultPage {
            id: vault
            anchors.fill: parent
            backdrop: wallpaper
        }
    }

    function init() {
        vault.visible = true
        vault.backdrop = wallpaper
        wallpaper.color = "#24364a"
        previousPage.color = "#ff00ff"
    }

    function backgroundPixel() {
        // Inside the page margin, away from the chrome, empty-state card and scrollbar.
        return grabImage(testWindow.contentItem).pixel(970, 350)
    }

    function test_previous_page_does_not_show_through() {
        waitForRendering(vault)
        compare(backgroundPixel(), wallpaper.color)
        previousPage.color = "#00ff00"
        waitForRendering(previousPage)
        compare(backgroundPixel(), wallpaper.color)
    }

    function test_wallpaper_stays_live() {
        wallpaper.color = "#493824"
        waitForRendering(wallpaper)
        compare(backgroundPixel(), wallpaper.color)
    }

    function test_missing_wallpaper_still_covers_previous_page() {
        vault.backdrop = null
        waitForRendering(vault)
        var before = backgroundPixel()
        previousPage.color = "#00ff00"
        waitForRendering(previousPage)
        compare(backgroundPixel(), before)
    }

    function test_leaving_vault_reveals_preserved_page() {
        vault.visible = false
        waitForRendering(previousPage)
        compare(backgroundPixel(), previousPage.color)
    }
}
