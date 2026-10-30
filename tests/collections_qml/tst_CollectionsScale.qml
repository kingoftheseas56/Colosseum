import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtTest
import "../../qml/collections" as CollectionsUi

// Bounded member-card construction against the real native store. A large
// valid collection must only instantiate a viewport-bounded card set while
// notes, reorder, remove/Undo, filters, and Save keep working.
Item {
    width: 1
    height: 1

    ApplicationWindow {
        id: window
        visible: true
        x: 16
        y: 16
        width: Math.min(1180, Screen.width - 32)
        height: Math.max(640, Math.min(880, Screen.desktopAvailableHeight - 48))
        title: "Bounded Collections member view tests"
        color: "#0c0f18"

        Button {
            objectName: "scaleOpenCollections"
            x: 20; y: 20
            text: "Your Collections"
            visible: !panel.showing
            onClicked: panel.showOverview()
        }
        CollectionsUi.CollectionsPanel {
            id: panel
            anchors.fill: parent
            nativeStore: CollectionsTest.store
            bottomInset: 20
        }
        Connections {
            target: CollectionsTest
            function onProfileChanging() { panel.profileChanged() }
        }
    }

    TestCase {
        id: checks
        name: "CollectionsScale"
        parent: window.contentItem
        when: windowShown

        function snapshot() { return JSON.stringify(CollectionsTest.store.collections()) }
        function byId(id) { return CollectionsTest.store.collection(id) }
        function visualControl(name) {
            var hiddenMatch = null
            function scan(item, depth) {
                if (!item || depth > 80) return null
                if (item.objectName === name) {
                    if (item.visible) return item
                    if (!hiddenMatch) hiddenMatch = item
                }
                var children = item.children || []
                for (var i = 0; i < children.length; ++i) {
                    var found = scan(children[i], depth + 1)
                    if (found) return found
                }
                return null
            }
            return scan(window.contentItem, 0) || scan(window.Overlay.overlay, 0)
                || hiddenMatch || findChild(window, name)
        }
        function control(name) {
            var item = visualControl(name)
            for (var attempt = 0; !item && attempt < 60; ++attempt) {
                wait(50)
                item = visualControl(name)
            }
            verify(item !== null && item !== undefined, "Control exists: " + name)
            return item
        }
        function reveal(item) {
            var ancestor = item.parent
            while (ancestor) {
                if (typeof ancestor.contentY === "number" && typeof ancestor.contentHeight === "number") {
                    for (var i = 0; i < 32; ++i) {
                        var p = item.mapToItem(ancestor, 0, 0)
                        if (p.y >= -1 && p.y + item.height <= ancestor.height + 1) break
                        var direction = p.y < 0 ? 120 : -120
                        mouseWheel(ancestor, Math.max(4, ancestor.width - 12), ancestor.height / 2,
                                   0, direction, Qt.NoButton)
                        wait(35)
                        tryVerify(function() { return ancestor.moving !== true && ancestor.flicking !== true },
                            2000, "Wheel motion settles before the next pointer action")
                    }
                }
                ancestor = ancestor.parent
            }
        }
        function press(name) {
            var item = control(name)
            verify(item.visible && item.enabled, "Visible enabled control: " + name)
            verify(item.Window.window === window, "Pointer target belongs to the fixture window: " + name)
            reveal(item)
            var p = item.mapToItem(window.contentItem, 0, 0)
            verify(p.x >= -1 && p.y >= -1 && p.x + item.width <= window.width + 1
                && p.y + item.height <= window.height + 1, "Pointer target is inside the window: " + name)
            mouseClick(item, item.width / 2, item.height / 2, Qt.LeftButton)
            wait(40)
        }
        function typeText(name, value) {
            press(name)
            var item = control(name)
            for (var attempt = 0; !item.activeFocus && attempt < 20; ++attempt) wait(50)
            verify(item.activeFocus, "Pointer focuses edit control: " + name)
            keyClick(Qt.Key_A, Qt.ControlModifier)
            keyClick(Qt.Key_Backspace)
            for (var i = 0; i < value.length; ++i) {
                var code = value.charCodeAt(i)
                if (code >= 97 && code <= 122) keyClick(Qt.Key_A + code - 97)
                else if (code >= 48 && code <= 57) keyClick(Qt.Key_0 + code - 48)
                else if (code === 32) keyClick(Qt.Key_Space)
                else if (code === 45) keyClick(Qt.Key_Minus)
                else fail("Unsupported test text character")
            }
            compare(item.text, value)
        }
        function matchingNames(prefix) {
            var names = []
            function scan(item, depth) {
                if (!item || depth > 200) return
                var objectName = item.objectName || ""
                if (objectName.indexOf(prefix) === 0) names.push(objectName)
                var children = item.children || []
                for (var i = 0; i < children.length; ++i) scan(children[i], depth + 1)
            }
            scan(window.contentItem, 0)
            return names
        }
        function countInstantiated(prefix) { return matchingNames(prefix).length }
        function assertDiskMatches() {
            var disk = CollectionsTest.diskState(false)
            verify(disk.ok, disk.error)
            compare(JSON.stringify(disk.collections), snapshot(),
                "Independent native reload agrees with UI writes")
        }
        // Seeding runs through the real store's public membership API; the
        // registry only owns picker admission, not member storage.
        function seedReference(index) {
            var manga = index % 2 === 1
            return {
                key: "scale:" + index,
                world: manga ? "tankoban" : "theatre",
                kind: manga ? "manga" : "movie",
                source: manga ? "mal" : "theatre",
                sourceId: "scale:" + index,
                editionId: "",
                title: "Scale member " + index,
                creator: "Fixture maker",
                year: "2026",
                route: manga ? "manga-detail" : "theatre-detail",
                payload: ({})
            }
        }
        function init() {
            panel.profileChanged()
            verify(CollectionsTest.reset(), CollectionsTest.setupError)
            verify(CollectionsTest.ready)
            compare(CollectionsTest.store.collections().length, 0)
            window.requestActivate()
            wait(160)
            // The panel stays closed while tests seed: a live overview re-reads
            // the whole store on every revision and would stall bulk seeding.
            compare(panel.pendingChanges, false)
        }
        function cleanup() { panel.profileChanged(); wait(50) }
        function cleanupTestCase() { window.close() }

        function test_02_crossRowReorderRestoresVisibleFocus() {
            var total = 40
            var made = CollectionsTest.store.createCollection("cross row shelf", seedReference(0), "")
            verify(!!made, CollectionsTest.store.error)
            for (var i = 1; i < total; ++i)
                verify(CollectionsTest.store.saveMemberships(seedReference(i), [made]).ok)
            var seeded = byId(made)
            press("scaleOpenCollections")
            press("collectionCard_" + made)
            press("editCollectionButton")
            var page = control("collectionPage")
            var list = control("collectionScroller")
            var rowIndex = 4
            list.positionViewAtIndex(rowIndex, ListView.Beginning)
            wait(80)
            var rowItem = list.itemAtIndex(rowIndex)
            verify(!!rowItem, "Middle row is instantiated")
            list.contentY = rowItem.y + rowItem.height - 170
            wait(80)
            var memberIndex = rowIndex * page.memberColumns
            var key = seeded.items[memberIndex]
            press("moveEarlier_" + key)
            tryVerify(function() {
                var earlier = visualControl("moveEarlier_" + key)
                return !!earlier && earlier.activeFocus
            }, 5000, "Moving across an uncached row restores focus")
            var action = control("moveEarlier_" + key)
            var point = action.mapToItem(list, 0, 0)
            verify(point.y >= -1 && point.y + action.height <= list.height + 1,
                "The moved control is visible without manual scrolling: " + point.y)
            press("saveCollectionButton")
            compare(byId(made).items.indexOf(key), memberIndex - 1)
            assertDiskMatches()
        }

        function test_01_largeCollectionBoundedInstantiation() {
            var total = 400
            console.info("COLLECTIONS_QML_SCALE seeding " + total)
            var made = CollectionsTest.store.createCollection("large shelf", seedReference(0), "")
            verify(!!made, CollectionsTest.store.error)
            for (var i = 1; i < total; ++i) {
                var result = CollectionsTest.store.saveMemberships(seedReference(i), [made])
                verify(result.ok, CollectionsTest.store.error)
            }
            var seeded = byId(made)
            compare(seeded.items.length, total)
            console.info("COLLECTIONS_QML_SCALE seeding complete")
            press("scaleOpenCollections")
            tryCompare(panel, "showing", true)
            press("collectionCard_" + made)
            compare(panel.collectionId, made)
            console.info("COLLECTIONS_QML_SCALE opened collection")

            tryVerify(function() { return countInstantiated("mediaCard_") > 0 })
            var idleCards = countInstantiated("mediaCard_")
            console.info("COLLECTIONS_QML_SCALE idle cards " + idleCards + " of " + total)
            verify(idleCards <= 40, "Idle member cards are viewport-bounded: " + idleCards)

            press("editCollectionButton")
            tryCompare(panel, "pendingChanges", true)
            tryVerify(function() { return countInstantiated("noteInput_") > 0 })
            var headEditors = countInstantiated("noteInput_")
            console.info("COLLECTIONS_QML_SCALE editing editors " + headEditors + " of " + total)
            verify(headEditors <= 40, "Editing note inputs are viewport-bounded: " + headEditors)

            // Draft a note on the second member (top row, always instantiated
            // and its Move-earlier control enabled), then destroy its
            // delegate by scrolling deep; the draft must survive recreation.
            var headKey = seeded.items[1]
            var headIndex = seeded.items.indexOf(headKey)
            verify(headIndex > 0 && headIndex < 50, "Picked a head member, index " + headIndex)
            var before = snapshot()
            typeText("noteInput_" + headKey, "deep note")

            var list = control("collectionScroller")
            list.positionViewAtIndex(list.count - 1, ListView.End)
            tryVerify(function() { return list.contentY > list.height },
                5000, "Viewport actually scrolled into the tail rows")
            tryVerify(function() { return countInstantiated("noteInput_") > 0 })
            verify(countInstantiated("noteInput_") <= 40, "Deep-scroll editors stay bounded")
            var tailKeys = matchingNames("noteInput_").map(function(name) {
                return name.slice("noteInput_".length)
            })
            var tailIndex = seeded.items.indexOf(tailKeys[tailKeys.length - 1])
            verify(tailIndex >= total - 100, "Reached tail members, index " + tailIndex)

            list.contentY = 0
            tryVerify(function() { return countInstantiated("noteInput_") > 0 })
            compare(control("noteInput_" + headKey).text, "deep note",
                "Draft note survives scrolling its card out of the viewport")

            press("moveEarlier_" + headKey)
            tryVerify(function() {
                var earlier = visualControl("moveEarlier_" + headKey)
                var later = visualControl("moveLater_" + headKey)
                return (!!earlier && earlier.activeFocus) || (!!later && later.activeFocus)
            }, 5000, "Reorder restores focus on the moved member's card")
            press("removeTitle_" + headKey)
            tryVerify(function() { return control("undoRemoveTitle").visible })
            verify(countInstantiated("noteInput_") <= 40, "Editors stay bounded after remove")
            press("undoRemoveTitle")
            tryVerify(function() { return !control("undoRemoveTitle").visible })
            compare(snapshot(), before, "Draft edits never mutate the native saved rows")

            press("saveCollectionButton")
            tryCompare(panel, "pendingChanges", false)
            var saved = byId(made)
            compare(saved.items.length, total)
            compare(saved.items.indexOf(headKey), headIndex - 1, "Reorder persisted through the native store")
            compare(saved.notes[headKey], "deep note")
            assertDiskMatches()
            verify(countInstantiated("mediaCard_") <= 40, "Cards stay bounded after save")

            var savedRows = snapshot()
            press("filter_manga")
            tryVerify(function() { return countInstantiated("mediaCard_") > 0 })
            var filteredCards = countInstantiated("mediaCard_")
            console.info("COLLECTIONS_QML_SCALE filtered cards " + filteredCards)
            verify(filteredCards <= 40, "Filtered member cards stay viewport-bounded: " + filteredCards)
            compare(snapshot(), savedRows, "Filters never mutate the native saved rows")
            press("filter_all")
            tryVerify(function() { return countInstantiated("mediaCard_") > 0 })
            verify(countInstantiated("mediaCard_") <= 40)
            compare(panel.collectionId, made)
        }
    }
}
