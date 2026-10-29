import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneMangaBack"
    when: windowShown

    Window {
        id: win
        width: 354
        height: 792
        visible: true
        property bool androidHost: true
    }

    Component {
        id: chapterViewComponent
        App.MangaChapterSeriesView {
            anchors.fill: parent
            seriesId: "mal:13"
            seriesTitle: "One Piece"
            author: "Eiichiro Oda"
            status: "Publishing"
            year: 1997
            chapters: testCase.fixtureChapters()
            chapterLanguages: [
                { "code": "en", "label": "English" },
                { "code": "es", "label": "Español" }
            ]
            selectedChapterLanguage: "es"
            downloader: null
        }
    }

    property var page: null

    SignalSpy { id: languageSpy; target: testCase.page; signalName: "chapterLanguageRequested" }
    SignalSpy { id: tankobanSpy; target: testCase.page; signalName: "tankobanRequested" }
    SignalSpy { id: backSpy; target: testCase.page; signalName: "backRequested" }

    function fixtureChapters() {
        const rows = []
        for (let i = 1; i <= 12; ++i)
            rows.push({ id: "fixture-ch-" + i, seriesId: "mal:13", number: i,
                        label: "Chapter " + i, name: "Chapter " + i, rawOrder: i })
        return rows
    }

    function init() {
        win.width = 354
        win.height = 792
        page = chapterViewComponent.createObject(win.contentItem)
        verify(page !== null, "production chapter view should load with local fixtures only")
        tryCompare(page, "visible", true)
        wait(30)
    }

    function cleanup() {
        if (page)
            page.destroy()
        page = null
        wait(0)
    }

    function findChild(root, wanted) {
        if (!root)
            return null
        if (root.objectName === wanted)
            return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findChild(children[i], wanted)
            if (found)
                return found
        }
        const data = root.data || []
        for (let i = 0; i < data.length; ++i) {
            if (children.indexOf(data[i]) >= 0)
                continue
            const found = findChild(data[i], wanted)
            if (found)
                return found
        }
        return null
    }

    function findPhoneScroll(root) {
        if (!root)
            return null
        if (root.clip === true && root.interactive === true
                && root.contentHeight !== undefined && root.contentWidth !== undefined)
            return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findPhoneScroll(children[i])
            if (found)
                return found
        }
        const data = root.data || []
        for (let i = 0; i < data.length; ++i) {
            if (children.indexOf(data[i]) >= 0)
                continue
            const found = findPhoneScroll(data[i])
            if (found)
                return found
        }
        return null
    }

    function revealInPhoneScroll(item) {
        const flick = findPhoneScroll(page)
        verify(flick !== null, "phone page should expose its actual clipped scroll viewport")
        if (!flick || !item)
            return
        const current = item.mapToItem(flick, 0, 0).y
        const desired = Math.max(8, (flick.height - item.height) / 2)
        const maxOffset = Math.max(0, flick.contentHeight - flick.height)
        flick.contentY = Math.max(0, Math.min(maxOffset, flick.contentY + current - desired))
        wait(30)
        const point = item.mapToItem(flick, 0, 0)
        verify(point.y >= -0.5 && point.y + item.height <= flick.height + 0.5,
               "control must be inside the real phone scroll viewport")
    }

    function touchItem(item) {
        const root = win.contentItem
        const point = item.mapToItem(root, item.width / 2, item.height / 2)
        verify(point.x >= 0 && point.y >= 0
               && point.x <= win.width && point.y <= win.height,
               "touched control must be inside the visible host")
        touchEvent(root).press(0, root, point.x, point.y).commit()
        wait(20)
        touchEvent(root).release(0, root, point.x, point.y).commit()
        wait(30)
    }

    function recreateAt(width, height) {
        if (page)
            page.destroy()
        win.width = width
        win.height = height
        wait(20)
        page = chapterViewComponent.createObject(win.contentItem)
        verify(page !== null)
        tryCompare(page, "phoneNarrow", true)
        wait(40)
    }

    function exerciseEscapeAt(width, height) {
        recreateAt(width, height)
        const selector = findChild(page, "mangaChapterPageSelector")
        const combo = findChild(page, "mangaLanguageCombo")
        verify(selector !== null, "chapter page selector should be present")
        verify(combo !== null, "shared language chooser should be present")
        if (!selector || !combo)
            return

        revealInPhoneScroll(selector)
        touchItem(selector)
        tryCompare(page, "pageMenuOpen", true)
        verify(typeof page.requestEscape === "function",
               "chapter view must expose Android shell requestEscape()")
        if (typeof page.requestEscape !== "function")
            return
        compare(page.requestEscape(), true, "the page selector consumes Back first")
        tryCompare(page, "pageMenuOpen", false)
        compare(page.visible, true, "dismissing the page menu keeps the chapter page visible")
        compare(page.selectedChapterLanguage, "es")

        revealInPhoneScroll(combo)
        touchItem(combo)
        tryVerify(function() { return combo.popup && combo.popup.visible },
                  1000, "a real touch should open the language ComboBox popup")
        compare(page.requestEscape(), true, "the language chooser consumes Back")
        tryVerify(function() { return !combo.popup.visible },
                  1000, "Back should dismiss the language popup")
        compare(page.visible, true, "dismissing the chooser keeps the chapter page visible")
        compare(page.selectedChapterLanguage, "es", "Back must not change the selected language")
        compare(languageSpy.count, 0, "dismissing the chooser must not select a language")
        compare(page.requestEscape(), false, "plain Back should fall through to Main to close the page")
        compare(page.visible, true, "the page hook reports fallthrough without closing the page itself")
        compare(backSpy.count, 0, "requestEscape must not emit the page back signal")
    }

    function test_escape_dismisses_page_and_language_popups_at_phone_sizes() {
        exerciseEscapeAt(354, 792)
        exerciseEscapeAt(792, 354)
    }

    function test_chapter_mode_center_button_emits_return_to_tankoban_request() {
        const tankoban = findChild(page, "mangaModeTankoban")
        verify(tankoban !== null, "shared mode switch should be present")
        if (!tankoban)
            return
        touchItem(tankoban)
        tryCompare(tankobanSpy, "count", 1)
        compare(backSpy.count, 0)
    }
}