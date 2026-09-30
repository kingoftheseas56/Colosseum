import QtQuick

Item {
    id: root
    width: 1
    height: 1
    property int fails: 0

    function check(ok, what) {
        if (!ok) {
            console.log("FAIL " + what)
            fails += 1
        } else {
            console.log("ok   " + what)
        }
    }

    QtObject {
        id: fake
        property var calls: []
        signal eventRaised(string name, string json)

        function record(name, value) { calls.push({ name: name, value: value }) }
        function open(path, cfi, gen) { record("open", { path: path, cfi: cfi, gen: gen }) }
        function next() { record("next", null) }
        function prev() { record("prev", null) }
        function goTo(target) { record("goTo", target) }
        function setAppearance(value) { record("setAppearance", value) }
        function search(value) { record("search", value) }
        function clearSearch() { record("clearSearch", null) }
        function addHighlight(value) { record("addHighlight", value) }
        function removeHighlight(value) { record("removeHighlight", value) }
        function clearSelection() { record("clearSelection", null) }
        function setReadAlongStyle(value) { record("setReadAlongStyle", value) }
        function paintReadAlong(value) { record("paintReadAlong", value) }
        function clearReadAlong() { record("clearReadAlong", null) }
        function ensureReadAlongVisible(value) { record("ensureReadAlongVisible", value) }
        function navigateReadAlong(value) { record("navigateReadAlong", value) }
        function focusPaper() { record("focusPaper", null) }
    }

    Loader {
        id: paperLoader
        source: "../qml/reader2/AndroidPaper.qml"
        onLoaded: {
            item.backend = fake
            var eventName = ""
            var eventPayload = ({})
            item.paperEvent.connect(function(name, payload) {
                eventName = name
                eventPayload = payload
            })

            item.open("/books/a.epub", "epubcfi(/6/4!/4/2)", 7)
            item.next()
            item.prev()
            item.goTo("chapter-2.xhtml")
            item.setAppearance({ theme: "night", sizePx: 20 })
            item.search("whale")
            item.clearSearch()
            item.addHighlight({ id: "h1", cfi: "epubcfi(/6/4!/4/8)" })
            item.removeHighlight("h1")
            item.clearSelection()
            item.setReadAlongStyle({ mode: "sentenceWord" })
            item.paintReadAlong({ cfi: "epubcfi(/6/4!/4/10)" })
            item.clearReadAlong()
            item.ensureReadAlongVisible({ cfi: "epubcfi(/6/6!/4/2)" })
            item.navigateReadAlong({ cfi: "epubcfi(/6/8!/4/2)" })
            item.focusPaper()

            check(fake.calls.length === 16, "all ReaderShell paper commands forward")
            check(fake.calls[0].name === "open", "open forwarded first")
            check(fake.calls[0].value.gen === 7, "open generation preserved")
            check(fake.calls[0].value.cfi === "epubcfi(/6/4!/4/2)", "resume CFI preserved")
            check(fake.calls[4].value.theme === "night", "appearance object preserved")

            fake.eventRaised("relocated", '{"gen":7,"percent":42,"cfi":"epubcfi(/6/4!/4/12)"}')
            check(eventName === "relocated", "renderer event name forwarded")
            check(eventPayload.gen === 7, "renderer event generation preserved")
            check(eventPayload.percent === 42, "renderer event payload preserved")

            item.backend = null
            item.open("/books/missing.epub", "", 8)
            check(eventName === "error", "missing Android renderer fails explicitly")
            check(eventPayload.gen === 8, "missing-renderer error belongs to current open")

            console.log(fails ? "VERDICT: FAIL" : "VERDICT: PASS")
            Qt.callLater(function() { Qt.exit(fails ? 1 : 0) })
        }
    }
}
