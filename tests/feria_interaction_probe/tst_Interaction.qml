import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

// Browser set 3: website interaction as real page workflows. Production deck,
// QtWebEngine wrapper and request owner, loopback fixture, disposable
// profiles. Each case ends with what the page itself observed.
Item {
    id: root
    width: 1060; height: 760
    // Fixed, disposable saved-address results for the accessibility/keyboard probe.
    property var suggestionStore: ({
        searchBrowserBookmarks: function(query) {
            return [{url: FixtureUrl + "/a", title: "Fixture page A"}]
        },
        searchBrowserHistory: function(query) {
            return [
                {url: FixtureUrl + "/b", title: ""},
                {url: FixtureUrl + "/daily", title: FixtureUrl + "/daily"}
            ]
        }
    })
    Feria.FeriaBrowserToolbar {
        id: toolbar
        x: 10; y: 10; width: 470; height: 36
        browserReady: true
        currentUrl: FixtureUrl + "/a"
        accountStore: root.suggestionStore
    }
    Item { id: embedded; x: 25; y: 125; width: 820; height: 580 }
    Feria.FeriaSessionDeck {
        id: deck
        parent: embedded
        anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        id: tests
        name: "FeriaInteraction"
        when: windowShown
        property int serial: 0
        SignalSpy { id: messageSpy; signalName: "message" }
        SignalSpy { id: addressAcceptedSpy; target: toolbar; signalName: "addressAccepted" }
        SignalSpy { id: addressCancelledSpy; target: toolbar; signalName: "editingCancelled" }

        function init() {
            failOnWarning(/TypeError|ReferenceError|incompatible arguments|Could not convert argument/)
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.switchProfile("interaction-" + (++serial))
            tryCompare(deck, "quiescing", false, 15000)
            deck.displayed = true; deck.suppressed = false
        }
        function cleanup() {
            messageSpy.target = null
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
        }
        function descriptor(path, extra) {
            return Object.assign({providerId: "mangaplus", providerName: "Fixture",
                url: FixtureUrl + path, startUrl: FixtureUrl + "/home", mode: "read"}, extra || {})
        }
        function loaded(page, path) {
            tryVerify(function() { return page && page.documentReady && !page.loading && !page.replacementLoadPending
                && String(page.url) === FixtureUrl + path }, 15000, "Expected " + path + " to finish loading")
        }
        function open(path, extra) {
            var id = deck.open(descriptor(path, extra), true, false, true)
            verify(id.length > 0)
            tryVerify(function() { return deck.browserForSession(id) }, 10000)
            loaded(deck.browserForSession(id), path)
            return id
        }
        function js(page, source) {
            var done = false, result
            verify(page.evaluateJavaScript(source, function(value) { result = value; done = true }))
            tryVerify(function() { return done }, 5000)
            return result
        }
        // Start something in the page without waiting for it: a page blocked
        // in a dialog cannot answer a script until the dialog is answered.
        function start(page, source) {
            verify(page.evaluateJavaScript("setTimeout(function(){" + source + "}, 0); true", function() {}))
        }
        function titled(page, title) {
            tryVerify(function() { return page.title === title }, 10000,
                "Expected the page to report " + title + ", it says " + page.title)
        }
        function asked(page, kind) {
            var requests = page.browserRequests
            tryVerify(function() { return requests.current !== null && requests.current.kind === kind }, 10000,
                "Expected a " + kind + " prompt")
            return requests
        }
        function part(page, name) {
            var item = findChild(page.browserRequests, name)
            verify(item !== null, name + " exists")
            return item
        }
        function press(page, name) {
            var button = part(page, name)
            verify(button.visible, name + " must be showing")
            mouseClick(button)
        }
        function stockDialogShowing() {
            var kids = Overlay.overlay.children
            for (var i = 0; i < kids.length; ++i)
                if (/AlertDialog|ConfirmDialog|PromptDialog|AuthenticationDialog|FilePicker/.test(
                        String(kids[i].objectName) + String(kids[i]))) return true
            return false
        }

        function test_addressSuggestionsAccessibilityAndKeyboard() {
            addressAcceptedSpy.clear()
            addressCancelledSpy.clear()
            toolbar.focusAddress()
            tryVerify(function() {
                var field = findChild(toolbar.overlay, "feriaBrowserAddress")
                return field !== null && field.visible && field.activeFocus
            }, 5000, "address TextField must own keyboard focus")
            var field = findChild(toolbar.overlay, "feriaBrowserAddress")
            verify(field !== null, "address field found by objectName")
            field.text = "fixture"
            tryVerify(function() {
                var list = findChild(toolbar.overlay, "feriaAddressSuggestions")
                return list !== null && list.visible && list.count === 3
            }, 5000, "three real suggestion delegates must appear")
            var list = findChild(toolbar.overlay, "feriaAddressSuggestions")
            // ListView delegates live in its contentItem and are created asynchronously.
            tryVerify(function() {
                return list.itemAtIndex(0) !== null && list.itemAtIndex(1) !== null
                    && list.itemAtIndex(2) !== null
            }, 5000, "ListView must materialize all three visible delegates")
            var first = list.itemAtIndex(0)
            var second = list.itemAtIndex(1)
            var third = list.itemAtIndex(2)
            compare(first.objectName, "feriaAddressSuggestion_0")
            compare(second.objectName, "feriaAddressSuggestion_1")
            compare(third.objectName, "feriaAddressSuggestion_2")
            compare(list.Accessible.role, Accessible.List)
            compare(String(list.Accessible.name), "Address suggestions")
            compare(first.Accessible.role, Accessible.ListItem)
            compare(second.Accessible.role, Accessible.ListItem)
            compare(third.Accessible.role, Accessible.ListItem)
            compare(String(first.Accessible.name), "Fixture page A - " + FixtureUrl + "/a")
            compare(String(second.Accessible.name), FixtureUrl + "/b", "empty title uses only URL")
            compare(String(third.Accessible.name), FixtureUrl + "/daily", "identical title uses only URL")
            compare(first.Accessible.selected, false)
            field.forceActiveFocus()
            verify(field.activeFocus, "address TextField must still own keyboard focus")
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 0)
            compare(first.Accessible.selected, true)
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 1)
            compare(first.Accessible.selected, false)
            compare(second.Accessible.selected, true)
            keyClick(Qt.Key_Up)
            compare(list.currentIndex, 0)
            compare(first.Accessible.selected, true)
            compare(second.Accessible.selected, false)
            keyClick(Qt.Key_Up)
            compare(list.currentIndex, -1)
            compare(first.Accessible.selected, false)
            console.log("ACCESSIBILITY_ASSERT_SUGGESTIONS listRole=" + list.Accessible.role
                + " itemRole=" + first.Accessible.role + " firstName="
                + JSON.stringify(String(first.Accessible.name))
                + " fallbackNames=true selectedDownUp=true")
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 0)
            keyClick(Qt.Key_Return)
            tryCompare(addressAcceptedSpy, "count", 1, 5000)
            compare(String(addressAcceptedSpy.signalArguments[0][0]), FixtureUrl + "/a")
            console.log("ACCESSIBILITY_ASSERT_ADDRESS_ENTER url="
                + String(addressAcceptedSpy.signalArguments[0][0]))
            toolbar.focusAddress()
            tryVerify(function() { return field.visible && field.activeFocus }, 5000)
            field.text = "fixture"
            tryVerify(function() { return list.visible && list.count === 3 }, 5000)
            keyClick(Qt.Key_Escape)
            tryCompare(addressCancelledSpy, "count", 1, 5000)
            tryCompare(list, "visible", false, 5000)
            compare(addressAcceptedSpy.count, 1, "Escape must not open another address")
            console.log("ACCESSIBILITY_ASSERT_ADDRESS_ESCAPE closed=true acceptedCount="
                + addressAcceptedSpy.count + " cancelledCount=" + addressCancelledSpy.count)
        }

        function test_pageDialogsAnswerThePage() {
            var page = deck.browserForSession(open("/a"))
            start(page, "alert('Saved.'); document.title = 'alert:returned'")
            var requests = asked(page, "dialog")
            compare(part(page, "feriaRequestMessage").text, "Saved.")
            compare(requests.promptTitle(), FixtureUrl + " says")
            verify(!part(page, "feriaRequestCancel").visible, "an alert offers only OK")
            verify(!stockDialogShowing(), "Qt's stock dialog must not open as well")
            compare(page.title, "Fixture /a", "the page waits for the answer")
            press(page, "feriaRequestAccept")
            titled(page, "alert:returned")

            start(page, "document.title = 'confirm:' + confirm('Delete this?')")
            asked(page, "dialog")
            compare(part(page, "feriaRequestMessage").text, "Delete this?")
            press(page, "feriaRequestAccept")
            titled(page, "confirm:true")
            start(page, "document.title = 'confirm:' + confirm('Delete this?')")
            asked(page, "dialog")
            press(page, "feriaRequestCancel")
            titled(page, "confirm:false")

            start(page, "document.title = 'prompt:' + prompt('Name?', 'Luffy')")
            asked(page, "dialog")
            var input = part(page, "feriaDialogInput")
            verify(input.visible)
            compare(input.text, "Luffy", "the field starts from the page's default")
            compare(String(input.Accessible.name), "Answer to the page's question")
            console.log("ACCESSIBILITY_ASSERT_PROMPT name=" + JSON.stringify(String(input.Accessible.name))
                + " visible=" + input.visible + " requestKind=" + page.browserRequests.current.kind)
            input.text = "Zoro"
            press(page, "feriaRequestAccept")
            titled(page, "prompt:Zoro")
            start(page, "document.title = 'prompt:' + prompt('Name?', 'Luffy')")
            asked(page, "dialog")
            press(page, "feriaRequestCancel")
            titled(page, "prompt:null")
            compare(page.browserRequests.current, null)
        }

        function test_tabBlockedInAlertCanBeLeftAndClosed() {
            var id = open("/a"), page = deck.browserForSession(id)
            // Leaving the page answers the dialog; the page is not stuck.
            start(page, "alert('Still here?')")
            asked(page, "dialog")
            var generation = page.navigationGeneration
            verify(deck.navigateSession(id, FixtureUrl + "/b", 0, null, null))
            tryVerify(function() { return page.navigationGeneration > generation && String(page.url) === FixtureUrl + "/b"
                && page.documentReady && !page.loading }, 15000, "navigation must not wait for the alert")
            compare(page.browserRequests.current, null)

            // Closing the tab does the same.
            start(page, "alert('Still here?')")
            asked(page, "dialog")
            verify(deck.closeTab(id))
            tryVerify(function() { return Sessions.get(id).id === undefined }, 10000,
                "a tab showing an alert must close")
            tryVerify(function() { return FeriaBrowserPolicy.runtimeViewCount === 0 }, 10000)
        }

        function test_httpSignIn() {
            var id = open("/a"), page = deck.browserForSession(id)
            // Cancel: the site's own refusal page is shown.
            verify(deck.navigateSession(id, FixtureUrl + "/auth", 0, null, null))
            var requests = asked(page, "signin")
            compare(requests.promptTitle(), "Sign in to " + FixtureUrl)
            verify(part(page, "feriaSignInUser").visible && part(page, "feriaSignInPassword").visible)
            compare(part(page, "feriaSignInPassword").echoMode, TextInput.Password)
            verify(!stockDialogShowing())
            press(page, "feriaRequestCancel")
            titled(page, "Fixture /auth refused")

            // A wrong password is asked again; the right one opens the page.
            verify(deck.navigateSession(id, FixtureUrl + "/auth", 0, null, null))
            asked(page, "signin")
            var first = requests.current.id
            part(page, "feriaSignInUser").text = "reader"
            part(page, "feriaSignInPassword").text = "wrong"
            press(page, "feriaRequestAccept")
            tryVerify(function() { return requests.current !== null && requests.current.kind === "signin"
                && requests.current.id !== first }, 10000, "a refused sign-in must be asked again")
            compare(part(page, "feriaSignInUser").text, "", "fields never carry over")
            compare(part(page, "feriaSignInPassword").text, "")
            part(page, "feriaSignInUser").text = "reader"
            part(page, "feriaSignInPassword").text = "fixture-pass"
            press(page, "feriaRequestAccept")
            titled(page, "Fixture /auth granted")
            compare(requests.current, null)
        }

        function test_fileUpload() {
            var id = open("/a"), page = deck.browserForSession(id), requests = page.browserRequests
            js(page, "document.body.innerHTML = '<form method=post action=/post enctype=multipart/form-data>"
                + "<input type=file name=upload id=f style=\"position:fixed;left:20px;top:20px;width:300px;height:40px\">"
                + "<button id=go style=\"position:fixed;left:20px;top:90px;width:200px;height:40px\">Send</button></form>'; true")
            var chosen = "document.getElementById('f').files.length ? document.getElementById('f').files[0].name : ''"

            // Cancelled: nothing chosen, and the page can ask again.
            mouseClick(page, 60, 40)
            tryVerify(function() { return requests.fileTicket !== null }, 10000, "the page's file chooser must reach the app")
            verify(!stockDialogShowing())
            requests.finishFiles([])
            compare(requests.fileTicket, null)
            compare(js(page, chosen), "")

            // Chosen: the page gets the file and the server receives its bytes.
            mouseClick(page, 60, 40)
            tryVerify(function() { return requests.fileTicket !== null }, 10000)
            verify(requests.finishFiles([Qt.resolvedUrl("upload-fixture.txt")]))
            tryVerify(function() { return js(page, chosen) === "upload-fixture.txt" }, 5000)
            mouseClick(page, 60, 110)
            loaded(page, "/post")
            var body = js(page, "window.postBody")
            verify(body.indexOf('filename="upload-fixture.txt"') >= 0, "the upload names the chosen file")
            verify(body.indexOf("Colosseum upload fixture: 7f3a") >= 0, "the upload carries the file's content")
        }

        function test_leavingWithdrawsAFileChooser() {
            var id = open("/a"), page = deck.browserForSession(id), requests = page.browserRequests
            js(page, "document.body.innerHTML = '<input type=file id=f style=\"position:fixed;left:20px;top:20px;width:300px;height:40px\">'; true")
            mouseClick(page, 60, 40)
            tryVerify(function() { return requests.fileTicket !== null }, 10000)
            verify(deck.navigateSession(id, FixtureUrl + "/b", 0, null, null))
            loaded(page, "/b")
            compare(requests.fileTicket, null)
            verify(!requests.finishFiles([Qt.resolvedUrl("upload-fixture.txt")]), "a late choice goes nowhere")
        }

        function test_permissionAllowPersistsUntilReset() {
            var id = open("/a"), page = deck.browserForSession(id)
            var ask = "Notification.requestPermission().then(function(r){ document.title = 'asked:' + r })"
            compare(js(page, "Notification.permission"), "default")
            start(page, ask)
            var requests = asked(page, "permission")
            compare(part(page, "feriaRequestMessage").text, "Allow " + FixtureUrl + " to send notifications?")
            press(page, "feriaRequestAccept")
            titled(page, "asked:granted")

            // The decision belongs to the site: it survives a reload and is not
            // asked again.
            var generation = page.navigationGeneration
            verify(deck.reload(id))
            tryVerify(function() { return page.navigationGeneration > generation && page.documentReady && !page.loading
                && !page.replacementLoadPending }, 15000)
            compare(js(page, "Notification.permission"), "granted")
            compare(requests.currentSitePermissions().length, 1)

            // Reset this site's permissions: the site has to ask again.
            compare(requests.resetSitePermissions(), 1)
            tryVerify(function() { return js(page, "Notification.permission") === "default" }, 5000)
            start(page, ask)
            asked(page, "permission")
            press(page, "feriaRequestCancel")
            titled(page, "asked:denied")
        }

        function test_downloadCompletesAndIsListed() {
            var id = open("/a"), page = deck.browserForSession(id)
            deck.sharedProfile.downloadPath = Fixture.downloadRoot
            mouseClick(page, 280, 210)            // the fixture page's download link
            tryVerify(function() { return deck.downloads.items.length === 1 }, 10000)
            var download = deck.downloads.items[0]
            tryCompare(download, "completed", true, 15000)
            compare(String(Fixture.downloadedContent(download.path)), "Colosseum browser download fixture\n")
            tryVerify(function() { return FeriaAccount.browserDownloadHistory.length === 1 }, 5000)
            compare(FeriaAccount.browserDownloadHistory[0].fileName, "browser-fixture.txt")
            compare(String(page.url), FixtureUrl + "/a", "a download does not replace the page")
        }

        function test_printToPdf() {
            var id = open("/a"), page = deck.browserForSession(id), requests = page.browserRequests
            messageSpy.target = requests
            // The page's own print button and the app's Print action meet here.
            start(page, "window.print()")
            tryVerify(function() { return requests.printTicket !== null }, 10000, "window.print() must reach the app")
            var target = Fixture.downloadRoot + "/printed-page.pdf"
            verify(requests.savePrint("file:///" + target))
            tryVerify(function() { return messageSpy.count > 0 }, 20000)
            compare(messageSpy.signalArguments[messageSpy.count - 1][0], "PDF saved.")
            var bytes = new Uint8Array(Fixture.downloadedContent(target))
            verify(bytes.length > 500, "the PDF has content: " + bytes.length + " bytes")
            compare(String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]), "%PDF")
        }
    }
}
