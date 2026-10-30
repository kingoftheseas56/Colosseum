import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtTest
import "../../qml/collections" as CollectionsUi

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
        title: "Isolated native Collections interaction tests"
        color: "#0c0f18"
        property int fixtureIndex: 0

        // This fixture window supplies only the app-shell entry seam. Both the
        // actual Add button and the canonical-reference owner are production code.
        function openNamedCollectionsPicker(candidate, opener) {
            const resolved = CollectionsIdentity.collectionReference(candidate)
            if (resolved.ok) panel.openPicker(resolved.reference, opener)
            else panel.showMessage(resolved.error)
        }
        CollectionsUi.AddToCollectionsButton {
            id: titleAction
            objectName: "fixtureTitleCollections"
            x: 20; y: 20
            visible: !panel.showing
            reference: CollectionsTest.rawReference(window.fixtureIndex)
        }
        Button {
            objectName: "fixtureOpenCollections"
            x: titleAction.x + titleAction.width + 20; y: 20
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
        name: "CollectionsNative"
        parent: window.contentItem
        when: windowShown
        SignalSpy { id: opened; target: panel; signalName: "openReferenceRequested" }

        function rows() { return CollectionsTest.store.collections() }
        function snapshot() { return JSON.stringify(rows()) }
        function byId(id) { return CollectionsTest.store.collection(id) }
        function byName(name) {
            return rows().find(function(row) { return row.name === name })
        }
        function reference(index) { return CollectionsTest.reference(index) }
        function windowGeometry() {
            var origin = window.contentItem.mapToGlobal(0, 0)
            return {x:window.x, y:window.y, width:window.width, height:window.height,
                globalX:origin.x, globalY:origin.y, active:window.active,
                screenName:Screen.name, screenWidth:Screen.width, screenHeight:Screen.height,
                availableWidth:Screen.desktopAvailableWidth, availableHeight:Screen.desktopAvailableHeight,
                virtualX:Screen.virtualX, virtualY:Screen.virtualY, devicePixelRatio:Screen.devicePixelRatio}
        }
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
            // Delegate QObject ownership differs from its visual parent. Use
            // the same visual-tree approach as production Lanista findItem.
            return scan(window.contentItem, 0) || scan(window.Overlay.overlay, 0)
                || hiddenMatch || findChild(window, name)
        }
        function diagnoseMissingControl(name) {
            var page = visualControl("overviewPage")
            var popup = visualControl("addToCollectionsPopup")
            var list = visualControl("membershipList")
            var visibleDelegates = []
            function collect(item, depth) {
                if (!item || depth > 80) return
                var objectName = item.objectName || ""
                if (objectName.indexOf("collectionCard_") === 0 || objectName.indexOf("membership_") === 0) {
                    var p = item.mapToItem(window.contentItem, 0, 0)
                    visibleDelegates.push({name:objectName, visible:item.visible,
                        x:p.x, y:p.y, width:item.width, height:item.height})
                }
                var children = item.children || []
                for (var i = 0; i < children.length; ++i) collect(children[i], depth + 1)
            }
            collect(window.contentItem, 0)
            collect(window.Overlay.overlay, 0)
            function ids(values) {
                return (values || []).map(function(row) { return row.id })
            }
            console.info("COLLECTIONS_QML_MISSING_CONTROL " + JSON.stringify({
                control:name,
                native:{revision:CollectionsTest.store.revision, healthy:CollectionsTest.store.healthy,
                    error:CollectionsTest.store.error, ids:ids(rows())},
                panel:{showing:panel.showing, pickerActive:panel.pickerActive, collectionId:panel.collectionId},
                overview:page ? {visible:page.visible, width:page.width, height:page.height,
                    search:page.searchText, shownIds:ids(page.shownCollections),
                    adapterRevision:page.store.revision, adapterIds:ids(page.store.collections)} : null,
                picker:popup ? {visible:popup.visible, creating:popup.creating,
                    adapterRevision:popup.store.revision, adapterIds:ids(popup.store.collections)} : null,
                memberships:list ? {visible:list.visible, count:list.count, modelIds:ids(list.model),
                    width:list.width, height:list.height, contentHeight:list.contentHeight} : null,
                renderedDelegates:visibleDelegates
            }))
            captureFailure("missing", name)
        }
        function captureFailure(reason, name) {
            try {
                // Capture the real visual root, including the popup overlay.
                var captureRoot = window.contentItem
                while (captureRoot.parent) captureRoot = captureRoot.parent
                var picture = grabImage(captureRoot)
                var filename = "collections-native-" + reason + "-" + name.replace(/[^a-zA-Z0-9_-]/g, "_").slice(0, 90) + ".png"
                picture.save(filename)
                console.info("COLLECTIONS_QML_FAILURE_CAPTURE " + filename
                    + " " + picture.width + "x" + picture.height)
            } catch (error) {
                console.info("COLLECTIONS_QML_FAILURE_CAPTURE_ERROR " + String(error))
            }
        }
        function diagnoseFocusFailure(name, item) {
            function describe(target) {
                if (!target) return null
                var p = target.mapToItem(window.contentItem, 0, 0)
                return {name:target.objectName, type:String(target), x:p.x, y:p.y,
                    width:target.width, height:target.height, visible:target.visible,
                    enabled:target.enabled, activeFocus:target.activeFocus, focus:target.focus,
                    focusPolicy:target.focusPolicy, activeFocusOnPress:target.activeFocusOnPress}
            }
            var ancestors = []
            var ancestor = item.parent
            for (var depth = 0; ancestor && depth < 80; ++depth) {
                var point = item.mapToItem(ancestor, 0, 0)
                var centerX = point.x + item.width / 2
                var centerY = point.y + item.height / 2
                var state = describe(ancestor)
                state.clip = ancestor.clip
                state.targetX = point.x
                state.targetY = point.y
                state.clipsClick = !!ancestor.clip && (centerX < 0 || centerY < 0
                    || centerX > ancestor.width || centerY > ancestor.height)
                state.contentY = ancestor.contentY
                state.contentHeight = ancestor.contentHeight
                state.moving = ancestor.moving
                state.flicking = ancestor.flicking
                ancestors.push(state)
                ancestor = ancestor.parent
            }
            console.info("COLLECTIONS_QML_FOCUS_FAILURE " + JSON.stringify({
                control:name, windowActive:window.active, target:describe(item),
                windowGeometry:windowGeometry(), targetUsesFixtureWindow:item.Window.window === window,
                activeFocusItem:describe(window.activeFocusItem), ancestors:ancestors,
                panel:{showing:panel.showing, pickerActive:panel.pickerActive,
                    collectionId:panel.collectionId, pendingChanges:panel.pendingChanges}
            }))
            captureFailure("focus", name)
        }
        function control(name) {
            var item = visualControl(name)
            for (var attempt = 0; !item && attempt < 60; ++attempt) {
                wait(50)
                item = visualControl(name)
            }
            if (!item) diagnoseMissingControl(name)
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
                        // A press during kinetic scrolling stops the Flickable
                        // and can be consumed before the editor receives it.
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
            if (!item.activeFocus) diagnoseFocusFailure(name, item)
            verify(item.activeFocus, "Pointer focuses edit control: " + name)
            keyClick(Qt.Key_A, Qt.ControlModifier)
            keyClick(Qt.Key_Backspace)
            // Typed fixtures use lower-case ASCII; native store tests cover Unicode and case folding.
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
        function assertDiskMatches() {
            var disk = CollectionsTest.diskState(false)
            verify(disk.ok, disk.error)
            compare(JSON.stringify(disk.collections), snapshot(), "Independent native reload agrees with UI writes")
        }
        function createEmpty(name) {
            press("newCollectionButton")
            typeText("collectionNameField", name)
            press("createAndAddButton")
            tryCompare(panel, "pickerActive", false)
            var created = byName(name)
            verify(created !== undefined, "Created through overview UI")
            compare(created.items.length, 0)
            return created.id
        }
        function titlePicker(index) {
            if (panel.showing) press("closeCollectionsButton")
            tryCompare(panel, "showing", false)
            window.fixtureIndex = index
            wait(40)
            verify(titleAction.available, "Actual registry admits the fixture reference")
            press("fixtureTitleCollectionsAction")
            tryCompare(panel, "pickerActive", true)
            tryVerify(function() { return control("pickerSearch").activeFocus })
        }
        function overview() {
            if (!panel.showing) press("fixtureOpenCollections")
            tryCompare(panel, "showing", true)
            compare(panel.collectionId, "")
        }
        function addTo(index, id) {
            titlePicker(index)
            press("membership_" + id)
            press("pickerDone")
            tryCompare(panel, "pickerActive", false)
            verify(CollectionsTest.store.contains(id, reference(index).key))
        }
        function init() {
            panel.profileChanged()
            verify(CollectionsTest.reset(), CollectionsTest.setupError)
            verify(CollectionsTest.ready)
            compare(rows().length, 0)
            window.fixtureIndex = 0
            opened.clear()
            window.requestActivate()
            panel.showOverview()
            wait(160)
            console.info("COLLECTIONS_QML_WINDOW " + JSON.stringify(windowGeometry()))
            compare(panel.pendingChanges, false)
        }
        function cleanup() { panel.profileChanged(); wait(50) }
        function cleanupTestCase() { window.close() }

        function test_01_createValidationCancelAndPersistence() {
            press("newCollectionButton")
            press("createAndAddButton")
            verify(control("pickerValidationError").text.length > 0)
            compare(rows().length, 0)
            verify(!CollectionsTest.diskState(false).fileExists)
            typeText("collectionNameField", "canceled shelf")
            press("pickerCancelCreate")
            tryCompare(panel, "pickerActive", false)
            compare(rows().length, 0)
            verify(!CollectionsTest.diskState(false).fileExists)
            tryVerify(function() { return control("newCollectionButton").activeFocus })
            createEmpty("first shelf")
            assertDiskMatches()
            var before = snapshot()
            press("newCollectionButton")
            typeText("collectionNameField", "first shelf")
            press("createAndAddButton")
            verify(control("pickerValidationError").text.length > 0)
            compare(snapshot(), before)
            press("pickerCancelCreate")
            assertDiskMatches()
            compare(opened.count, 0)
        }
        function test_02_multipleMembershipsCancelEscapeAndSave() {
            var first = createEmpty("first shelf")
            var second = createEmpty("second shelf")
            var before = snapshot()
            titlePicker(0)
            press("membership_" + first)
            press("membership_" + second)
            press("pickerCancel")
            tryCompare(panel, "pickerActive", false)
            compare(snapshot(), before)
            tryVerify(function() { return control("fixtureTitleCollectionsAction").activeFocus })
            titlePicker(0)
            press("membership_" + first)
            press("membership_" + second)
            press("pickerDone")
            tryCompare(panel, "pickerActive", false)
            compare(CollectionsTest.store.memberships(reference(0).key).length, 2)
            compare(byId(first).items.length, 1)
            compare(byId(second).items.length, 1)
            assertDiskMatches()
            var saved = snapshot()
            titlePicker(0)
            press("membership_" + first)
            keyClick(Qt.Key_Escape)
            tryCompare(panel, "pickerActive", false)
            compare(snapshot(), saved)
            tryVerify(function() { return control("fixtureTitleCollectionsAction").activeFocus })
            titlePicker(0)
            press("pickerDone")
            compare(snapshot(), saved)
            assertDiskMatches()
        }
        function test_03_atomicCreateAndAddAndNestedCancel() {
            var existing = createEmpty("existing shelf")
            titlePicker(0)
            press("membership_" + existing)
            press("pickerNewCollection")
            press("createAndAddButton")
            verify(control("pickerValidationError").text.length > 0)
            compare(rows().length, 1)
            compare(byId(existing).items.length, 0)
            typeText("collectionNameField", "new title shelf")
            press("createAndAddButton")
            tryCompare(panel, "pickerActive", false)
            compare(rows().length, 2)
            var made = byName("new title shelf")
            compare(made.items.length, 1)
            compare(made.items[0], reference(0).key)
            compare(byId(existing).items[0], reference(0).key)
            assertDiskMatches()
            var before = snapshot()
            titlePicker(1)
            press("membership_" + existing)
            press("pickerNewCollection")
            typeText("collectionNameField", "discard this collection")
            press("pickerCancelCreate")
            verify(panel.pickerActive)
            press("pickerCancel")
            compare(snapshot(), before)
            assertDiskMatches()
        }
        function test_04_nativeDraftRenameNotesOrderSaveAndCancel() {
            var id = createEmpty("personal shelf")
            addTo(0, id)
            addTo(1, id)
            overview()
            press("collectionCard_" + id)
            compare(panel.collectionId, id)
            compare(opened.count, 0, "Navigation never opens a title automatically")
            var before = snapshot()
            var film = reference(0).key
            var manga = reference(1).key
            press("editCollectionButton")
            tryCompare(panel, "pendingChanges", true)
            typeText("collectionNameInput", "renamed shelf")
            typeText("noteInput_" + film, "a personal note")
            press("moveLater_" + film)
            compare(snapshot(), before, "Draft does not mutate the native saved rows")
            press("saveCollectionButton")
            tryCompare(panel, "pendingChanges", false)
            compare(byId(id).name, "renamed shelf")
            compare(byId(id).notes[film], "a personal note")
            compare(byId(id).items[0], manga)
            compare(byId(id).items[1], film)
            assertDiskMatches()
            var saved = snapshot()
            press("editCollectionButton")
            typeText("collectionNameInput", "unsaved shelf")
            typeText("noteInput_" + film, "discard this note")
            press("moveEarlier_" + film)
            press("cancelCollectionEdit")
            tryCompare(panel, "pendingChanges", false)
            compare(snapshot(), saved)
            assertDiskMatches()
            compare(opened.count, 0)
            press("mediaCard_" + film)
            compare(opened.count, 1)
            compare(opened.signalArguments[0][0].key, film)
            compare(opened.signalArguments[0][0].sourceId, reference(0).sourceId)
            compare(opened.signalArguments[0][0].route, "theatre-detail")
        }
        function test_05_deleteCancelThenConfirm() {
            var id = createEmpty("delete this shelf")
            press("collectionCard_" + id)
            var before = snapshot()
            press("deleteCollectionButton")
            tryCompare(panel, "deleteActive", true)
            tryVerify(function() { return control("cancelDeleteCollection").activeFocus })
            press("cancelDeleteCollection")
            tryCompare(panel, "deleteActive", false)
            compare(snapshot(), before)
            tryVerify(function() { return control("deleteCollectionButton").activeFocus })
            press("deleteCollectionButton")
            press("confirmDeleteCollection")
            tryCompare(panel, "deleteActive", false)
            compare(rows().length, 0)
            compare(panel.collectionId, "")
            verify(control("overviewEmptyAction").visible)
            assertDiskMatches()
        }
        function test_06_profileSwitchDropsDraftAndModal() {
            var id = createEmpty("first profile shelf")
            addTo(0, id)
            overview()
            press("collectionCard_" + id)
            press("editCollectionButton")
            typeText("collectionNameInput", "unsaved old profile name")
            var saved = snapshot()
            verify(panel.pendingChanges)
            verify(CollectionsTest.reset(), CollectionsTest.setupError)
            tryCompare(panel, "showing", false)
            tryCompare(panel, "pendingChanges", false)
            compare(panel.pickerActive, false)
            compare(rows().length, 0)
            compare(JSON.stringify(CollectionsTest.diskState(true).collections), saved)
            tryVerify(function() {
                var stale = visualControl("collectionNameInput")
                return stale === null || stale === undefined
            }, 3000, "Previous profile editor is destroyed after the profile switch")
            overview()
            var newId = createEmpty("second profile shelf")
            var secondSaved = snapshot()
            titlePicker(1)
            press("membership_" + newId)
            verify(panel.pickerActive)
            verify(CollectionsTest.reset(), CollectionsTest.setupError)
            tryCompare(panel, "pickerActive", false)
            compare(panel.pendingChanges, false)
            compare(panel.showing, false)
            compare(panel.pickerOpener, null)
            compare(rows().length, 0)
            compare(JSON.stringify(CollectionsTest.diskState(true).collections), secondSaved)
            tryVerify(function() {
                var stale = visualControl("pickerSearch")
                return stale === null || stale === undefined
            }, 3000, "Previous profile picker is destroyed after the profile switch")
            compare(opened.count, 0)
        }

        function test_08_editAndLeaveControlsStayInViewport() {
            var id = createEmpty("visible edit controls")
            addTo(0, id)
            overview()
            press("collectionCard_" + id)
            press("editCollectionButton")
            tryCompare(panel, "pendingChanges", true)
            var viewport = control("collectionScroller")
            var name = control("collectionNameInput")
            var namePoint = name.mapToItem(viewport, 0, 0)
            console.info("COLLECTIONS_HEADER_GEOMETRY " + JSON.stringify({contentY:viewport.contentY,
                originY:viewport.originY,contentHeight:viewport.contentHeight,headerY:viewport.headerItem.y,
                headerHeight:viewport.headerItem.height,topMargin:viewport.topMargin,bottomMargin:viewport.bottomMargin}))
            verify(namePoint.y >= -1 && namePoint.y + name.height <= viewport.height + 1,
                "Starting an edit reveals its focused name field without manual scrolling: " + namePoint.y)
            control("collectionPage").requestBack()
            wait(60)
            var stay = control("stayEditingButton")
            var stayPoint = stay.mapToItem(viewport, 0, 0)
            verify(stayPoint.y >= -1 && stayPoint.y + stay.height <= viewport.height + 1,
                "Back reveals its focused leave decision without manual scrolling: " + stayPoint.y)
            press("stayEditingButton")
            press("cancelCollectionEdit")
        }

        function repeatChar(ch, count) {
            var out = ""
            for (var i = 0; i < count; ++i) out += ch
            return out
        }
        // U+1F600 as its UTF-16 surrogate pair; typed fixtures stay ASCII, so
        // Unicode-boundary fixtures are assigned directly to the real editors.
        function emoji() { return "\uD83D\uDE00" }
        function test_07_unicodeSafeNoteAndDescriptionLimits() {
            var id = createEmpty("unicode shelf")
            addTo(0, id)
            overview()
            press("collectionCard_" + id)
            var film = reference(0).key
            press("editCollectionButton")
            tryCompare(panel, "pendingChanges", true)
            var note = control("noteInput_" + film)
            var description = control("collectionDescriptionInput")

            // 499 ASCII + one emoji = 501 UTF-16 units. Truncating at raw code
            // units keeps a lone high surrogate and the real native store
            // rejects the whole save; the cut must back up to a whole scalar.
            note.text = repeatChar("a", 499) + emoji()
            compare(note.text, repeatChar("a", 499),
                "Note truncates on a complete Unicode boundary")
            description.text = repeatChar("b", 299) + emoji()
            compare(description.text, repeatChar("b", 299),
                "Description truncates on a complete Unicode boundary")
            press("saveCollectionButton")
            tryCompare(panel, "pendingChanges", false)
            compare(byId(id).notes[film], repeatChar("a", 499))
            compare(byId(id).description, repeatChar("b", 299))
            assertDiskMatches()

            press("editCollectionButton")
            tryCompare(panel, "pendingChanges", true)
            note = control("noteInput_" + film)
            description = control("collectionDescriptionInput")
            // Exactly at the native limits with a complete trailing emoji:
            // valid input must survive the editors untouched.
            note.text = repeatChar("c", 498) + emoji()
            compare(note.text, repeatChar("c", 498) + emoji())
            description.text = repeatChar("d", 298) + emoji()
            compare(description.text, repeatChar("d", 298) + emoji())
            press("saveCollectionButton")
            tryCompare(panel, "pendingChanges", false)
            var savedNote = byId(id).notes[film]
            compare(savedNote.length, 500)
            verify(savedNote.charCodeAt(498) >= 0xD800 && savedNote.charCodeAt(498) <= 0xDBFF,
                "Saved note keeps its high surrogate")
            verify(savedNote.charCodeAt(499) >= 0xDC00 && savedNote.charCodeAt(499) <= 0xDFFF,
                "Saved note keeps its low surrogate")
            compare(byId(id).description.length, 300)
            assertDiskMatches()

            press("editCollectionButton")
            tryCompare(panel, "pendingChanges", true)
            note = control("noteInput_" + film)
            description = control("collectionDescriptionInput")
            // Full ASCII at the native limits is still valid; clamping must
            // not lower the limits themselves.
            note.text = repeatChar("e", 500)
            compare(note.text, repeatChar("e", 500))
            description.text = repeatChar("f", 300)
            compare(description.text, repeatChar("f", 300))
            press("saveCollectionButton")
            tryCompare(panel, "pendingChanges", false)
            compare(byId(id).notes[film], repeatChar("e", 500))
            compare(byId(id).description, repeatChar("f", 300))
            assertDiskMatches()
        }
    }
}
