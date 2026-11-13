import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: suite
    name: "AndroidLayout140"
    when: windowShown

    Window {
        id: host
        visible: true
        width: 762
        height: 457
        property bool desktopPresentation: true
        property bool androidHost: true
        property real browseBottomInset: 80
        Item { id: wallpaper; anchors.fill: parent }
    }
    QtObject {
        id: localAccount
        property string mode: "localOnly"
        property string username: ""
        property bool accountFeaturesAvailable: true
    }
    Component {
        id: heroComponent
        App.FeaturedCarousel {
            androidHost: host.androidHost
            width: host.width - 108
            slides: [{title: "A featured title", blurb: "The featured description", cta: "Watch"}]
        }
    }
    function test_featured_buttons_fit_first_view_data() {
        return [{tag: "t20", width: 762, height: 457},
                {tag: "oneplus", width: 955, height: 427}]
    }
    function test_featured_buttons_fit_first_view(data) {
        host.width = data.width
        host.height = data.height
        const hero = createTemporaryObject(heroComponent, host.contentItem)
        verify(hero)
        verify(hero.height <= host.height - 96 - 12 - host.browseBottomInset,
               "Whole hero must fit between the header and the dock")
    }
    Component {
        id: barComponent
        App.TopBar {
            backdrop: wallpaper
            androidHost: host.androidHost
            width: host.width - 108
            activeMedium: "Theatre"
            trackersEnabled: true
            accountController: localAccount
        }
    }
    function test_clock_and_controls_fit_data() {
        return [{tag: "t20", width: 762}, {tag: "oneplus", width: 955},
                {tag: "t20-125", width: 914}]
    }
    function test_clock_and_controls_fit(data) {
        host.width = data.width
        const bar = createTemporaryObject(barComponent, host.contentItem)
        verify(bar)
        bar.date = "Wednesday, September 30"
        wait(50)
        const clock = findChild(bar, "topBarClock")
        const picker = findChild(bar, "topBarWorldPicker")
        const systems = findChild(bar, "topBarSystemChrome")
        const left = findChild(bar, "topBarLeftChrome")
        verify(clock.visible, "Clock must remain visible")
        verify(picker.x >= left.x + left.width + 8, "Pills must clear clock and Back")
        verify(picker.x + picker.width + 8 <= systems.x, "Pills must clear Search")
        verify(systems.x + systems.width <= bar.width + 1)
    }
}
