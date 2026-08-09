import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/guide" as Guide

TestCase {
    name: "GuidePage"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 720
        visible: true
    }

    Component { id: pageComponent; Guide.GuidePage {} }
    Component { id: catalogComponent; Guide.GuideCatalog {} }
    property var page: null
    SignalSpy { id: closeSpy; signalName: "closeRequested" }
    SignalSpy { id: wallpaperSpy; signalName: "wallpaperChoiceRequested" }

    function init() {
        page = pageComponent.createObject(testWindow, { width: testWindow.width, height: testWindow.height })
        verify(page !== null)
        closeSpy.target = page
        wallpaperSpy.target = page
    }

    function cleanup() {
        closeSpy.clear()
        closeSpy.target = null
        wallpaperSpy.clear()
        wallpaperSpy.target = null
        if (page) page.destroy()
        page = null
    }

    // Break caught: a Guide entry opens a generic blank surface rather than its usable home.
    function test_home_startup_exposes_the_living_codex() {
        compare(page.objectName, "guidePage")
        compare(page.currentView, "home")
        compare(findChild(page, "guideHome").title, "Guide")
        compare(findChild(page, "guideSearch").placeholderText, "What do you want to do?")
        compare(findChild(page, "guideHome").popularLabels,
                ["Continue where I left off", "Open media from this device",
                 "Choose and enable a source", "Something is not working"])
    }

    // Break caught: the stable left index cannot open its published section.
    function test_index_navigation_opens_a_published_section() {
        var index = findChild(page, "guideIndex")
        verify(index !== null)
        index.selectSection("tankoban")
        compare(page.currentSection, "tankoban")
        compare(page.currentView, "section")
        verify(page.visibleLessons.length > 0)
    }

    // Break caught: a visible local search result cannot lead into its lesson.
    function test_search_result_activation_opens_the_lesson() {
        page.search("tankoban fixture")
        compare(page.searchResults.length, 1)
        page.activateSearchResult(0)
        compare(page.currentView, "article")
        compare(page.currentLesson.id, "fixture.tankoban")
    }

    // Break caught: a failed search invents advice or leaves the person at a dead end.
    function test_no_result_offers_stable_section_and_fix_a_problem() {
        findChild(page, "guideSearch").text = "no such local guide answer"
        compare(page.currentView, "search")
        compare(page.searchResults.length, 0)
        verify(page.noResultFallbackSection.length > 0)
        compare(page.noResultFixSection, "fix")
        tryCompare(page, "presentationOpacity", 1)
        var nearest = findChild(page, "guideNoResultNearestAction")
        verify(nearest !== null)
        tryVerify(function() { return nearest.visible && nearest.width > 0 && nearest.height > 0 })
        mouseClick(nearest)
        compare(page.currentView, "section")
        compare(page.currentSection, page.noResultFallbackSection)

        findChild(page, "guideSearch").text = "no such local guide answer"
        tryCompare(page, "presentationOpacity", 1)
        var fix = findChild(page, "guideNoResultFixAction")
        verify(fix !== null)
        tryVerify(function() { return fix.visible && fix.width > 0 && fix.height > 0 })
        mouseClick(fix)
        compare(page.currentView, "section")
        compare(page.currentSection, "fix")
    }

    // Break caught: unknown origin internals leak into a fabricated context strip.
    function test_unknown_context_omits_the_context_strip() {
        page.originLabel = ""
        page.originContext = "private.UnknownSurface"
        compare(findChild(page, "guideContextStrip").visible, false)
    }

    // Break caught: Draft/deep-hidden content becomes visible through a direct lesson ID.
    function test_hidden_deep_link_returns_home() {
        var hiddenPage = pageComponent.createObject(testWindow, {
            width: testWindow.width, height: testWindow.height, initialLessonId: "fixture.draft"
        })
        verify(hiddenPage !== null)
        tryCompare(hiddenPage, "currentView", "home")
        hiddenPage.destroy()
    }

    // Break caught: an unknown published ID routes to a made-up page instead of Home.
    function test_unknown_deep_link_returns_home() {
        var unknownPage = pageComponent.createObject(testWindow, {
            width: testWindow.width, height: testWindow.height, initialLessonId: "missing.lesson"
        })
        verify(unknownPage !== null)
        tryCompare(unknownPage, "currentView", "home")
        unknownPage.destroy()
    }

    // Break caught: a published article drops supported teaching blocks or renders unknown content.
    function test_article_renders_only_supported_blocks_and_keeps_missing_image_text() {
        var fixture = fixtureCatalog()
        var articlePage = pageComponent.createObject(testWindow, {
            width: testWindow.width, height: testWindow.height, catalog: fixture, initialLessonId: "fixture.blocks"
        })
        verify(articlePage !== null)
        tryCompare(articlePage, "currentView", "article")
        var article = findChild(articlePage, "guideArticle")
        compare(article.renderedKinds, ["paragraph", "steps", "bullets", "note", "image"])
        verify(article.visibleText.indexOf("Missing image fallback") >= 0)
        verify(article.visibleText.indexOf("Hidden unknown block") < 0)
        articlePage.destroy()
    }

    // Break caught: a bad related ID renders a broken or fabricated related lesson action.
    function test_article_omits_invalid_related_lessons() {
        var fixture = fixtureCatalog()
        var articlePage = pageComponent.createObject(testWindow, {
            width: testWindow.width, height: testWindow.height, catalog: fixture, initialLessonId: "fixture.blocks"
        })
        verify(articlePage !== null)
        tryCompare(articlePage, "currentView", "article")
        compare(findChild(articlePage, "guideArticle").relatedLessons.length, 0)
        articlePage.destroy()
    }

    // Break caught: an image without usable alt text still creates a visual candidate.
    function test_article_omits_image_visual_when_alt_text_is_missing() {
        var fixture = invalidImageCatalog()
        var articlePage = pageComponent.createObject(testWindow, {
            width: testWindow.width, height: testWindow.height, catalog: fixture, initialLessonId: "fixture.invalid-image"
        })
        verify(articlePage !== null)
        tryCompare(articlePage, "currentView", "article")
        var article = findChild(articlePage, "guideArticle")
        verify(article.visibleText.indexOf("Verified fallback instructions") >= 0)
        compare(findChild(article, "guideArticleImageVisual"), null)
        articlePage.destroy()
    }

    // Break caught: a recognized origin loses the return bridge to the exact calling surface.
    function test_context_strip_and_return_action_preserve_origin_label() {
        page.originLabel = "Manga reader"
        page.originContext = "tankoban"
        var strip = findChild(page, "guideContextStrip")
        tryVerify(function() { return strip.visible })
        verify(strip.y >= page.height - strip.height - 1,
               "recognized context stays in the persistent bottom return lane")
        compare(findChild(page, "guideReturnAction").text, "Return to Manga reader")
        var returns = 0
        page.returnRequested.connect(function() { returns++ })
        page.requestReturn()
        compare(returns, 1)
    }

    // Break caught: keyboard users cannot reach a Guide control through the real tab order.
    function test_keyboard_focus_is_available_and_visible() {
        var search = findChild(page, "guideSearch")
        verify(search.activeFocusOnTab)
        testWindow.requestActivate()
        for (var index = 0; index < 12 && !search.activeFocus; ++index) {
            keyClick(Qt.Key_Tab)
            wait(0)
        }
        verify(search.activeFocus, "Tab must reach the local Guide search field")
        verify(search.focusVisible)
        keyClick(Qt.Key_Backtab)
        wait(0)
        verify(!search.activeFocus, "Backtab must leave the focused Guide search field")
        verify(hasActiveFocus(page), "Backtab keeps focus inside a meaningful Guide control")
    }

    // Break caught: reduced motion is a disconnected preference rather than an immediate visual state.
    function test_reduced_motion_makes_selection_immediate() {
        page.reducedMotion = false
        page.openSection("biblio")
        verify(page.presentationOpacity < 1, "normal navigation starts a visible transition")
        tryCompare(page, "presentationOpacity", 1)
        page.reducedMotion = true
        page.openSection("theatre")
        compare(page.currentSection, "theatre")
        compare(page.presentationOpacity, 1)
        compare(page.presentationTransitionRunning, false)
    }

    // Break caught: Escape escapes the caller rather than closing the Guide utility first.
    function test_escape_emits_close_requested() {
        page.forceActiveFocus()
        testWindow.requestActivate()
        keyClick(Qt.Key_Escape)
        compare(closeSpy.count, 1)
    }

    // Break caught: the narrow page leaves an always-on index consuming the reading width.
    function test_narrow_fixture_turns_index_into_a_temporary_drawer() {
        page.width = 900
        page.height = 640
        wait(0)
        compare(findChild(page, "guideIndex").drawerMode, true)
        compare(findChild(page, "guideIndex").drawerOpen, false)
        page.toggleIndexDrawer()
        compare(findChild(page, "guideIndex").drawerOpen, true)
    }

    // Break caught: First Journey becomes a blocking prerequisite or changes its five local steps.
    function test_first_journey_is_skippable_replayable_and_non_blocking() {
        var journey = findChild(page, "guideFirstJourney")
        compare(journey.stepIds, ["meet-worlds", "taskbar-and-library", "choose-wallpaper",
                                  "sources-are-optional", "open-media-and-return"])
        verify(journey.visible)
        journey.skipCurrent()
        verify(page.currentView === "home")
        journey.completeCurrent()
        journey.replay()
        compare(journey.currentStep, 0)
    }

    // Break caught: the harmless wallpaper lesson cannot hand off to the later shell choice surface.
    function test_first_journey_wallpaper_action_requests_a_choice_without_completing_the_step() {
        var journey = findChild(page, "guideFirstJourney")
        var journeyRequests = 0
        var pageRequests = 0
        journey.wallpaperChoiceRequested.connect(function() { journeyRequests++ })
        page.wallpaperChoiceRequested.connect(function() { pageRequests++ })
        journey.currentStep = 2
        testWindow.requestActivate()
        var chooseWallpaper = findChild(journey, "guideJourneyWallpaperAction")
        verify(chooseWallpaper !== null)
        tryVerify(function() { return chooseWallpaper.visible && chooseWallpaper.width > 0 && chooseWallpaper.height > 0 })
        var actionPosition = chooseWallpaper.mapToItem(page, 0, 0)
        verify(actionPosition.y >= 0 && actionPosition.y + chooseWallpaper.height <= page.height,
               "the visible wallpaper action must be inside the Guide viewport")
        page.forceActiveFocus()
        for (var index = 0; index < 16 && !chooseWallpaper.activeFocus; ++index) {
            keyClick(Qt.Key_Tab)
            wait(0)
        }
        verify(chooseWallpaper.activeFocus, "Tab must reach the visible wallpaper action")
        keyClick(Qt.Key_Space)
        compare(journeyRequests, 1)
        compare(pageRequests, 1)
        compare(wallpaperSpy.count, 1)
        compare(journey.currentStep, 2)
        journey.skipCurrent()
        compare(journey.currentStep, 3)
    }

    function fixtureCatalog() {
        return Qt.createQmlObject('import QtQuick 2.15; QtObject {\n'
            + 'property var allLessons: [{ id: "fixture.blocks", section: "start", title: "Block fixture", '
            + 'outcome: "A local article fixture.", status: "published", order: 1, worlds: [], evidence: [], '
            + 'verifiedCommit: "fixture", verifiedDate: "2026-08-09", contexts: ["home"], searchTerms: ["blocks"], '
            + 'blocks: [{kind:"paragraph", text:"A paragraph"}, {kind:"steps", items:["A step"]}, '
            + '{kind:"bullets", items:["A bullet"]}, {kind:"note", text:"A note"}, '
            + '{kind:"image", path:"missing-local-guide-asset.png", alt:"Missing image fallback"}, '
            + '{kind:"unknown", text:"Hidden unknown block"}], related: ["missing.related"] }];\n'
            + 'property var publishedLessons: allLessons;\n'
            + 'function find(id) { return id === "fixture.blocks" ? allLessons[0] : null; }\n'
            + 'function search(query, context) { return query === "blocks" ? allLessons : []; }\n'
            + 'function section(id) { return id === "start" ? allLessons : []; }\n'
            + '}', testWindow, "fixtureCatalog")
    }

    function invalidImageCatalog() {
        return Qt.createQmlObject('import QtQuick 2.15; QtObject {\n'
            + 'property var allLessons: [{ id: "fixture.invalid-image", section: "start", title: "Invalid image fixture", '
            + 'outcome: "A local image fallback fixture.", status: "published", order: 1, worlds: [], evidence: [], '
            + 'verifiedCommit: "fixture", verifiedDate: "2026-08-09", contexts: ["home"], searchTerms: ["invalid image"], '
            + 'blocks: [{kind:"paragraph", text:"Verified fallback instructions"}, '
            + '{kind:"image", path:"local-asset.png", alt:"", text:"Verified fallback instructions"}], related: [] }];\n'
            + 'property var publishedLessons: allLessons;\n'
            + 'function find(id) { return id === "fixture.invalid-image" ? allLessons[0] : null; }\n'
            + 'function search(query, context) { return []; }\n'
            + 'function section(id) { return id === "start" ? allLessons : []; }\n'
            + '}', testWindow, "invalidImageCatalog")
    }

    function hasActiveFocus(root) {
        if (!root) return false
        if (root.activeFocus) return true
        var kids = root.children || []
        for (var index = 0; index < kids.length; ++index) {
            if (hasActiveFocus(kids[index])) return true
        }
        return false
    }

    function findChild(root, objectName) {
        if (!root) return null
        if (root.objectName === objectName) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], objectName)
            if (found) return found
        }
        return null
    }
}
