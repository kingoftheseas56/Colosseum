import QtQuick
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

Item {
    width: 960; height: 640
    Feria.FeriaSessionDeck {
        id: deck
        anchors.fill: parent
        sessions: Sessions
        accountStore: FeriaAccount
        policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        name: "FeriaFoundation"
        when: windowShown
        property int serial: 0
        function init() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 10000)
            Fixture.switchProfile("foundation-" + (++serial))
            tryCompare(deck, "quiescing", false, 10000)
        }
        function cleanup() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 10000)
        }
        function open(path, foreground) {
            var id = Sessions.openBrowser({providerId: "mangaplus", providerName: "Reading",
                url: FixtureUrl + path, startUrl: FixtureUrl + "/home", mode: "read"},
                true, false, foreground !== false)
            verify(id.length > 0)
            tryVerify(function() { return deck.entries[id] && deck.entries[id].browser
                && deck.entries[id].browser.documentReady }, 15000)
            return id
        }
        function js(page, script) {
            var done = false, value
            page.evaluateJavaScript(script, function(result) { value = result; done = true })
            tryVerify(function() { return done }, 5000)
            return value
        }
        function test_crossOriginAndSameDocumentHistory() {
            var a = open("/a"), page = deck.entries[a].browser
            var other = FixtureUrl.replace("127.0.0.1", "localhost") + "/cross"
            verify(deck.navigateInput(a, other))
            tryVerify(function() { return String(page.url) === other && page.title === "Fixture /cross" && page.documentReady }, 10000)
            var nav = deck.navigationForSession(a)
            tryCompare(nav, "canGoBack", true)
            verify(deck.back(a))
            tryVerify(function() { return String(page.url) === FixtureUrl + "/a" && page.title === "Fixture /a" && page.documentReady }, 10000)
            js(page, "window.sessionMarker=88; history.pushState({},'', '#one'); history.pushState({},'', '#two')")
            tryVerify(function() { return String(page.url).endsWith("#two") && nav.backCount >= 2 })
            verify(deck.historyOffset(a, -1, nav.historyRevision))
            tryVerify(function() { return String(page.url).endsWith("#one") })
            compare(js(page, "window.sessionMarker"), 88)
            verify(!nav.canGoForward || nav.forwardCount > 0)
        }
        function test_privateRequestProfileAndRetirementFence() {
            var a = open("/a")
            var b = deck.newSpace(a, true, true)
            tryVerify(function() { return deck.entries[b] && deck.entries[b].browser.documentReady }, 15000)
            var privateView = deck.entries[b].browser
            verify(privateView.profile.offTheRecord)
            mouseClick(privateView, 80, 220)
            tryVerify(function() { return Sessions.browsers().length === 3 }, 10000)
            var c = Sessions.activeId
            verify(c !== b && c !== a)
            tryVerify(function() { return deck.entries[c] && deck.entries[c].browser.documentReady }, 10000)
            compare(deck.entries[c].browser.profile, privateView.profile)
            verify(Sessions.get(c).target.privateBrowsing)
            Sessions.close(c)
            compare(deck.navigationForSession(c), null)
            verify(!deck.back(c))
            verify(!deck.navigateInput(c, FixtureUrl + "/b"))
        }
        function test_namedTargetAndNoopener() {
            var a = open("/a"), page = deck.entries[a].browser
            js(page, "document.querySelector('button').onclick=function(){window.open('/named','feria-target')}")
            mouseClick(page, 80, 220)
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { return deck.entries[b].browser.documentReady }, 10000)
            var target = deck.entries[b].browser
            compare(js(target, "window.name"), "feria-target")
            Sessions.switchTo(a)
            js(page, "document.querySelector('button').onclick=function(){window.open('/named-again','feria-target')}")
            mouseClick(page, 80, 220)
            tryVerify(function() { return target.title === "Fixture /named-again" && target.documentReady }, 10000)
            compare(Sessions.browsers().length, 2)
            compare(deck.entries[b].browser, target)
            Sessions.switchTo(a)
            js(page, "document.querySelector('button').onclick=function(){window.open('/isolated','_blank','noopener')}")
            mouseClick(page, 80, 220)
            tryVerify(function() { return Sessions.browsers().length === 3 }, 10000)
            var c = Sessions.browsers().filter(function(r) { return r.id !== a && r.id !== b })[0].id
            tryVerify(function() { return deck.entries[c].browser.documentReady }, 10000)
            compare(js(deck.entries[c].browser, "window.opener === null"), true)
        }
        function test_siteWindowKeepsExistingHost() {
            var a = open("/a"), page = deck.entries[a].browser
            js(page, "document.querySelector('button').onclick=function(){window.open('/dialog','fixture-dialog','width=420,height=300')}")
            mouseClick(page, 80, 220)
            tryVerify(function() { return page.popups.length === 1 }, 10000)
            var popup = page.popups[0]
            tryCompare(popup.view, "title", "Fixture /dialog", 10000)
            compare(popup.view.profile, page.profile)
            compare(Sessions.browsers().length, 1)
            popup.close()
            tryVerify(function() { return page.popups.length === 0 }, 5000)
        }
        function test_captureRealEngine() {
            if (!Fixture.evidenceRoot.length) skip("No explicit capture directory")
            var a = open("/a")
            waitForRendering(deck)
            verify(Fixture.captureWindow(deck, "foundation-engine.png"))
        }
        function test_stopAndReload() {
            var a = open("/a"), page = deck.entries[a].browser
            var nav = deck.navigationForSession(a)
            verify(!deck.stop(a))
            verify(deck.navigateInput(a, FixtureUrl + "/slow"))
            tryCompare(nav, "loading", true, 5000)
            verify(deck.stop(a))
            tryCompare(nav, "loading", false, 10000)
            verify(!deck.stop(a))
            verify(deck.navigateInput(a, FixtureUrl + "/a"))
            tryVerify(function() { return String(page.url).endsWith("/a") && page.documentReady }, 10000)
            js(page, "window.sessionMarker=99")
            verify(deck.reload(a))
            tryVerify(function() { return page.documentReady && js(page, "window.sessionMarker") === 0 }, 10000)
        }
        function test_originalPostRequest() {
            var a = open("/a"), page = deck.entries[a].browser
            js(page, "window.sessionMarker=2718")
            mouseClick(page, 80, 280)
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var ids = Sessions.browsers().map(function(r) { return r.id })
            var b = ids.filter(function(id) { return id !== a })[0]
            tryVerify(function() { return deck.entries[b] && deck.entries[b].browser.documentReady }, 10000)
            var target = deck.entries[b].browser
            console.log("REVIEW_POST_EARLY", target.documentReady, String(target.url), target.title, js(target, "location.href"))
            tryVerify(function(){return String(target.url)===FixtureUrl+"/post" && target.title==="POST result"},10000)
            compare(js(target, "window.postBody"), "marker=original-post-body")
            compare(js(target, "window.openerMarker"), 2718)
            compare(target.profile, page.profile)
            compare(Sessions.activeId, b)
        }
        function test_backgroundLinkDoesNotSelect() {
            var a = open("/a"), page = deck.entries[a].browser
            // A real middle-click asks Chromium for a background destination.
            mouseClick(page, 45, 74, Qt.MiddleButton)
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            compare(Sessions.activeId, a)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { return deck.entries[b] && deck.entries[b].browser.documentReady }, 10000)
            compare(String(deck.entries[b].browser.url), FixtureUrl + "/b")
        }
        function test_nativeNavigationTargetsBackgroundSpace() {
            var a = open("/a"), page = deck.entries[a].browser
            // Toolbar Back follows Chromium's user-interaction skip policy.
            // Navigate with a real user click, not a renderer-script redirect.
            mouseClick(page, 45, 74)
            tryVerify(function() { return String(page.url).endsWith("/b") && page.documentReady }, 10000)
            var b = open("/other")
            var nav = deck.navigationForSession(a)
            verify(nav !== null)
            console.log("USER_NAV", page.canGoBack, nav.canGoBack, nav.backCount)
            tryCompare(nav, "canGoBack", true)
            verify(deck.back(a))
            tryVerify(function() { return String(page.url).endsWith("/a") && page.documentReady }, 10000)
            compare(Sessions.activeId, b)
            compare(String(deck.entries[b].browser.url), FixtureUrl + "/other")
            tryCompare(nav, "canGoForward", true)
            verify(deck.forward(a))
            tryVerify(function() { return String(page.url).endsWith("/b") && page.documentReady }, 10000)
            verify(!deck.back("missing"))
        }
        function test_scriptNavigationKeepsNativeSkipSemantics() {
            var a = open("/a"), page = deck.entries[a].browser
            js(page, "location.href='/b'")
            tryVerify(function() { return page.title === "Fixture /b" && page.documentReady }, 10000)
            var nav = deck.navigationForSession(a)
            tryVerify(function() { return nav.backCount > 0 })
            console.log("SCRIPT_NAV", page.canGoBack, nav.canGoBack, nav.backCount)
            compare(nav.canGoBack, page.canGoBack)
            // An explicit chooser may traverse an entry the toolbar skips.
            verify(deck.historyOffset(a, -1, nav.historyRevision))
            tryVerify(function() { return page.title === "Fixture /a" && page.documentReady }, 10000)
        }
        function test_historyOffsetAndStaleToken() {
            var a = open("/a"), page = deck.entries[a].browser
            js(page, "location.href='/b'")
            tryVerify(function() { return String(page.url).endsWith("/b") && page.documentReady }, 10000)
            js(page, "location.href='/c'")
            tryVerify(function() { return String(page.url).endsWith("/c") && page.documentReady }, 10000)
            var nav = deck.navigationForSession(a)
            tryVerify(function() { return nav.backCount >= 2 })
            var token = nav.historyRevision
            verify(!deck.historyOffset(a, -999, token))
            verify(deck.historyOffset(a, -2, token))
            tryVerify(function() { return String(page.url).endsWith("/a") && page.documentReady }, 10000)
            verify(!deck.historyOffset(a, 1, token), "stale chooser token must be rejected")
            verify(deck.historyOffset(a, 2, nav.historyRevision))
            tryVerify(function() { return String(page.url).endsWith("/c") && page.documentReady }, 10000)
        }
        function test_liveIdentityAndAppProjection() {
            var a = open("/a"), first = deck.entries[a].browser
            js(first, "window.sessionMarker=2718")
            var b = open("/b"), second = deck.entries[b].browser
            compare(Sessions.browserApps().length, 1)
            compare(Sessions.browserSpaces("mangaplus").length, 2)
            Sessions.updateBrowser(a, FixtureUrl + "/renamed", "A different skin label")
            Sessions.switchTo(a)
            compare(deck.entries[a].browser, first)
            compare(js(first, "window.sessionMarker"), 2718)
            compare(deck.entries[b].browser, second)
            compare(Sessions.browserApps()[0].selectedSpaceId, a)
            Sessions.activateBrowserApp("mangaplus")
            compare(Sessions.activeId, "")
            Sessions.activateBrowserApp("mangaplus")
            compare(Sessions.activeId, a)
        }
        function test_explicitNewUsesProviderStart() {
            var a = open("/a")
            var b = deck.newSpace(a, false, true)
            verify(a !== b && b.length > 0)
            tryVerify(function() { return deck.entries[b] && deck.entries[b].browser
                && deck.entries[b].browser.documentReady }, 15000)
            compare(String(deck.entries[b].browser.url), FixtureUrl + "/home")
        }

        function test_review_localhostPathIsAddress() {
            var parsed=String(FeriaBrowserPolicy.omniboxUrl("localhost/account?code=private-token"))
            compare(parsed, "http://localhost/account?code=private-token")
        }
        function test_review_normalizedStartUrlCreatesSpace() {
            var a=Sessions.openBrowser({providerId:"mangaplus",providerName:"Reading",
                url:FixtureUrl+"/a",startUrl:FixtureUrl.replace("http://", "")+"/home",mode:"read"},true)
            tryVerify(function(){return deck.entries[a] && deck.entries[a].browser.documentReady},15000)
            var accepted=String(FeriaBrowserPolicy.navigationUrl(Sessions.browsers()[0].target.startUrl))
            compare(accepted, FixtureUrl+"/home")
            var b=deck.newSpace(a,false,true)
            verify(b.length>0,"A valid parsed startUrl must create a space")
            tryVerify(function(){return deck.entries[b] && String(deck.entries[b].browser.url)===accepted},10000)
        }
        function test_review_childWindowSurvivesOpenerClose() {
            var a=open("/a"), page=deck.entries[a].browser
            js(page,"document.querySelector('button').onclick=function(){window.open('/dialog','review-dialog','width=420,height=300')}")
            mouseClick(page,80,220)
            tryVerify(function(){return page.popups.length===1},10000)
            var child=page.popups[0]
            tryCompare(child.view,"title","Fixture /dialog",10000)
            Sessions.close(a)
            tryVerify(function(){return !deck.entries[a]},10000)
            wait(300)
            compare(child.visible,true,"Closing the opener must not destroy a live child browsing window")
        }
        function test_review_backgroundTargetFromChildWindow() {
            var a=open("/a"),page=deck.entries[a].browser
            js(page,"document.querySelector('button').onclick=function(){window.open('/dialog','review-background','width=420,height=300')}")
            mouseClick(page,80,220)
            tryVerify(function(){return page.popups.length===1},10000)
            var child=page.popups[0]
            tryCompare(child.view,"title","Fixture /dialog",10000)
            var completed=false, hit
            child.view.runJavaScript("JSON.stringify({at74:document.elementFromPoint(80,74).outerHTML,link:document.querySelector('a').getBoundingClientRect().toJSON(),dpi:devicePixelRatio,h:innerHeight,w:innerWidth})",function(v){hit=v;completed=true})
            tryVerify(function(){return completed},5000)
            console.log("CHILD_GEOMETRY",hit,child.view.width,child.view.height,child.visible)
            mouseClick(child.view,80,74,Qt.MiddleButton)
            tryVerify(function(){return Sessions.browsers().length>1 || page.popups.length>1},10000)
            compare(Sessions.browsers().length,2,"Child background link must enter the canonical space owner")
            compare(Sessions.activeId,a)
            compare(page.popups.length,1,"Background tab must not create another visible desktop popup")
        }

        function test_review_newWindowDestinationSurvivesOpenerClose() {
            var a=open("/a"),page=deck.entries[a].browser
            var contextReady=false
            page.contextMenuRequested.connect(function(request){request.accepted=true;contextReady=true})
            page.newWindowRequested.connect(function(request){console.log("REVIEW_WINDOW_DESTINATION",request.destination,WebEngineNewWindowRequest.InNewWindow)})
            mouseClick(page,45,74,Qt.RightButton)
            tryVerify(function(){return contextReady},10000)
            page.triggerWebAction(WebEngineView.OpenLinkInNewWindow)
            tryVerify(function(){return page.popups.length===1 || Sessions.browsers().length===2},10000)
            var child=page.popups.length ? page.popups[0] : null
            var other=Sessions.browsers().filter(function(r){return r.id!==a})
            if(child) tryCompare(child.view,"title","Fixture /b",10000)
            else tryVerify(function(){return deck.entries[other[0].id].browser.documentReady},10000)
            Sessions.close(a)
            tryVerify(function(){return !deck.entries[a]},10000)
            wait(300)
            if(child) compare(child.visible,true,"InNewWindow page must survive closing its opener")
            else verify(deck.entries[other[0].id]!==undefined)
        }
        function test_review_newWindowSurvivesMinimize() {
            var a=open("/a"),page=deck.entries[a].browser
            var contextReady=false
            page.contextMenuRequested.connect(function(request){request.accepted=true;contextReady=true})
            page.newWindowRequested.connect(function(request){console.log("REVIEW_WINDOW_DESTINATION",request.destination,WebEngineNewWindowRequest.InNewWindow)})
            mouseClick(page,45,74,Qt.RightButton)
            tryVerify(function(){return contextReady},10000)
            page.triggerWebAction(WebEngineView.OpenLinkInNewWindow)
            tryVerify(function(){return page.popups.length===1 || Sessions.browsers().length===2},10000)
            var child=page.popups.length ? page.popups[0] : null
            var other=Sessions.browsers().filter(function(r){return r.id!==a})
            if(child) tryCompare(child.view,"title","Fixture /b",10000)
            else tryVerify(function(){return deck.entries[other[0].id].browser.documentReady},10000)
            Sessions.switchTo("")
            wait(300)
            if(child) compare(child.visible,true,"Minimizing the app must not destroy its live requested window")
            else verify(deck.entries[other[0].id]!==undefined)
        }
    }
}
