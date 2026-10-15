import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/feria"

Item {
    id: scene
    width: 1280
    height: 800
    FeriaWorld { id: feria; anchors.fill: parent; lifecycleActive: true }
    TestCase {
        id: test
        name: "FeriaBrowserToolbar"
        when: windowShown
        property var host
        property var browser
        function find(item, name) {
            if (item.objectName === name) return item
            return findChild(item, name)
        }
        function openAddress() {
            var address = find(feria, "feriaBrowserAddress")
            mouseClick(find(feria, "feriaBrowserAddressToggle"))
            tryVerify(function() { return address.visible && address.activeFocus })
            return address
        }
        function initTestCase() {
            host = find(feria, "feriaBrowserHost")
            verify(host !== null)
            host.enginePreferences = ({})
            feria.viewState = "home"
            feria.openHost("mangaplus", "", "home", feriaBrowserTestBaseUrl + "page?a")
            compare(host.engine, "qtwebengine", "QtWebEngine is the default for new provider sessions")
            tryVerify(function() {
                browser = find(feria, "feriaBrowserLoader").item
                return browser && browser.ready && host.loadSucceeded && !host.pageLoading
            }, 10000)
        }
        function init() {
            scene.width = 1280
            host.focusApp()
            feria.toast = ""
            var target = feriaBrowserTestBaseUrl + "page?a"
            host.loadSucceeded = false
            if (String(browser.url) === target) browser.reload()
            else browser.url = target
            tryVerify(function() { return host.loadSucceeded && !host.pageLoading && host.currentUrl === target }, 10000)
        }
        function cleanupTestCase() {
            // Let the final document's callbacks finish before destroying the view.
            browser.documentReady = false
            browser.stopLoad()
            wait(600)
        }
        function test_controls_available_without_options() {
            compare(host.optionsOpen, false)
            var names = ["feriaBrowserBack", "feriaBrowserForward", "feriaBrowserReload", "feriaBrowserAddressToggle"]
            for (var i = 0; i < names.length; ++i) {
                var control = find(feria, names[i])
                verify(control !== null, names[i] + " exists")
                verify(control.visible, names[i] + " is visible without opening Options")
                verify(control.mapToItem(feria, 0, 0).y + control.height <= browser.mapToItem(feria, 0, 0).y)
            }
            compare(find(feria, "feriaBrowserAddress").visible, false)
            compare(find(feria, "feriaAppBar").height, 52)
            grabImage(feria).save(feriaBrowserTestOutput + "/toolbar-wide.png")
            var address = openAddress()
            mouseClick(find(feria, "feriaBrowserAddressToggle"))
            tryCompare(address, "visible", false)
            openAddress()
            address.text = "unfinished.example/draft"
            mouseClick(browser, 100, 100)
            tryCompare(address, "visible", false)
            compare(address.text, host.currentUrl)
            compare(feria.viewState, "host")
        }
        function test_options_pointer_and_close() {
            var options = find(feria, "feriaAppOptions")
            mouseClick(options)
            tryCompare(host, "optionsOpen", true)
            verify(find(feria, "feriaAppLayout").visible)
            mouseClick(options)
            tryCompare(host, "optionsOpen", false)
        }
        function test_address_navigation_and_history() {
            var address = find(feria, "feriaBrowserAddress")
            verify(address !== null, "Editable address field exists")
            browser.forceActiveFocus()
            keyClick(Qt.Key_L, Qt.ControlModifier)
            tryVerify(function() { return address.activeFocus })
            verify(address.visible)
            compare(address.selectedText, host.currentUrl)
            address.text = feriaBrowserTestBaseUrl + "page?b"
            keyClick(Qt.Key_Return)
            tryCompare(address, "visible", false)
            tryVerify(function() { return !host.pageLoading && host.currentUrl.endsWith("page?b") && host.engineCanGoBack }, 10000)
            var back = find(feria, "feriaBrowserBack")
            verify(back.visible && back.enabled)
            mouseClick(back)
            tryVerify(function() { return !host.pageLoading && host.currentUrl.endsWith("page?a") && host.engineCanGoForward }, 10000)
            var forward = find(feria, "feriaBrowserForward")
            verify(forward.visible && forward.enabled)
            mouseClick(forward)
            tryVerify(function() { return !host.pageLoading && host.currentUrl.endsWith("page?b") && host.engineCanGoBack }, 10000)
            keyClick(Qt.Key_Left, Qt.AltModifier)
            tryVerify(function() { return !host.pageLoading && host.currentUrl.endsWith("page?a") && host.engineCanGoForward }, 10000, "Alt+Left returns to page A; current URL: " + host.currentUrl)
            keyClick(Qt.Key_Right, Qt.AltModifier)
            tryVerify(function() { return !host.pageLoading && host.currentUrl.endsWith("page?b") }, 10000)
        }
        function test_address_edit_and_escape() {
            var address = find(feria, "feriaBrowserAddress")
            verify(address !== null, "Editable address field exists")
            openAddress()
            address.text = "unfinished.example/draft"
            host.currentUrl = feriaBrowserTestBaseUrl + "page?redirected"
            wait(20)
            compare(address.text, "unfinished.example/draft")
            keyClick(Qt.Key_Escape)
            tryCompare(address, "text", host.currentUrl)
            verify(!address.activeFocus)
            compare(address.visible, false)
            compare(feria.viewState, "host", "Cancelling address editing does not exit Feria's browser")
        }
        function test_address_rejects_executable_scheme() {
            var address = find(feria, "feriaBrowserAddress")
            verify(address !== null, "Editable address field exists")
            var before = host.currentUrl
            openAddress()
            address.text = "javascript:document.title='wrong'"
            keyClick(Qt.Key_Return)
            compare(host.currentUrl, before)
            verify(address.activeFocus, "Invalid address stays editable")
            verify(feria.toast.length > 0, "Invalid address has feedback")
        }
        function test_address_context_menu_preserves_draft() {
            var address = find(feria, "feriaBrowserAddress")
            openAddress()
            address.text = "example.com/draft"
            address.select(12, 17)
            mouseClick(address, 30, address.height / 2, Qt.RightButton)
            var menu = findChild(feria, "feriaAddressMenu")
            verify(menu !== null)
            tryCompare(menu, "opened", true)
            compare(address.text, "example.com/draft")
            compare(address.selectedText, "draft")
            grabImage(scene.Window.window.contentItem).save(feriaBrowserTestOutput + "/toolbar-context-menu.png")
            mouseClick(findChild(feria, "feriaAddressDelete"))
            tryCompare(menu, "visible", false)
            compare(address.text, "example.com/")
        }
        function test_shell_escape_cancels_address() {
            var address = find(feria, "feriaBrowserAddress")
            openAddress()
            address.text = "unfinished.example/draft"
            // Main.qml's global Escape command delegates to FeriaWorld.back().
            feria.back()
            compare(feria.viewState, "host")
            compare(address.text, host.currentUrl)
            verify(!address.activeFocus)
            compare(address.visible, false)
        }
        function test_reload_and_stop() {
            var reload = find(feria, "feriaBrowserReload")
            var completed = 0
            function observed() { completed++ }
            browser.completed.connect(observed)
            mouseClick(reload)
            tryVerify(function() { return completed > 0 && !host.pageLoading }, 10000)
            var previous = completed
            keyClick(Qt.Key_R, Qt.ControlModifier)
            tryVerify(function() { return completed > previous && !host.pageLoading }, 10000)
            previous = completed
            keyClick(Qt.Key_F5)
            tryVerify(function() { return completed > previous && !host.pageLoading }, 10000)
            browser.completed.disconnect(observed)
            var address = find(feria, "feriaBrowserAddress")
            openAddress()
            address.text = feriaBrowserTestBaseUrl + "slow-image"
            keyClick(Qt.Key_Return)
            tryCompare(host, "pageLoading", true)
            compare(reload.text, "×")
            mouseClick(reload)
            tryCompare(host, "pageLoading", false)
            compare(reload.text, "↻")
        }
        function test_address_normalization() {
            verify(typeof FeriaBrowserPolicy.navigationUrl === "function", "Native address parsing is available")
            compare(String(FeriaBrowserPolicy.navigationUrl("  example.com/chapter  ")), "http://example.com/chapter")
            compare(String(FeriaBrowserPolicy.navigationUrl("https://example.com/chapter?q=1#page")), "https://example.com/chapter?q=1#page")
            compare(String(FeriaBrowserPolicy.navigationUrl("http://localhost:8080/book")), "http://localhost:8080/book")
            compare(String(FeriaBrowserPolicy.navigationUrl("javascript:alert(1)")), "")
            compare(String(FeriaBrowserPolicy.navigationUrl("file:///C:/secret.txt")), "")
            compare(String(FeriaBrowserPolicy.navigationUrl("")), "")
        }
        function test_narrow_controls_do_not_overlap() {
            scene.width = 760
            wait(50)
            var address = find(feria, "feriaBrowserAddress")
            verify(address !== null, "Editable address field exists")
            grabImage(feria).save(feriaBrowserTestOutput + "/toolbar-narrow.png")
            openAddress()
            verify(address.width >= 120)
            var reload = find(feria, "feriaBrowserReload")
            var toggle = find(feria, "feriaBrowserAddressToggle")
            verify(reload.mapToItem(feria, reload.width, 0).x <= toggle.mapToItem(feria, 0, 0).x)
            verify(address.mapToItem(feria, 0, 0).x >= 0)
            verify(address.mapToItem(feria, address.width, 0).x <= feria.width)
            var options = find(feria, "feriaAppOptions")
            verify(options.mapToItem(feria, options.width, 0).x <= find(feria, "feriaHostMinimize").mapToItem(feria, 0, 0).x)
            grabImage(scene.Window.window.contentItem).save(feriaBrowserTestOutput + "/toolbar-address-expanded.png")
            mouseClick(options)
            tryCompare(host, "optionsOpen", true)
        }
    }
}
