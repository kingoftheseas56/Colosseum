import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3

TestCase {
    id: suite
    name: "ReaderAudioOffer"
    when: windowShown

    Window {
        id: stage
        width: 640
        height: 520
        color: "#111013"
        visible: true
    }

    Arc64Fonts { id: fonts }
    function initTestCase() { tryCompare(fonts, "ready", true) }
    Rectangle { id: frame; parent: stage.contentItem; x:20; y:20; color:"#111013"
        width: suite.subject ? suite.subject.width : 290
        height: suite.subject ? suite.subject.height : 158
    }
    property var subject: null
    property var sourceComponent: null

    function componentFor(path) {
        var component = Qt.createComponent(path)
        compare(component.status, Component.Ready, component.errorString())
        return component
    }

    function makeCard(properties) {
        sourceComponent = componentFor("../../qml/reader2/AudioOfferCard.qml")
        subject = sourceComponent.createObject(frame, properties || {})
        verify(subject !== null, sourceComponent.errorString())
        subject.x = 0
        subject.y = 0
        stage.requestActivate()
        wait(0)
        return subject
    }

    function makeChip(properties) {
        sourceComponent = componentFor("../../qml/reader2/AudioOfferChip.qml")
        subject = sourceComponent.createObject(frame, properties || {})
        verify(subject !== null, sourceComponent.errorString())
        subject.x = 0
        subject.y = 0
        stage.requestActivate()
        wait(0)
        return subject
    }

    function findNamed(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; ++i) {
            var found = findNamed(kids[i], name)
            if (found)
                return found
        }
        return null
    }

    function cleanup() {
        if (subject)
            subject.destroy()
        subject = null
        sourceComponent = null
        wait(0)
    }

    function sampleVersions() {
        return [
            { title: "Moby-Dick", format: "M4B", language: "English", sizeText: "412 MB" },
            { title: "Moby-Dick", format: "MP3", language: "English", sizeText: "398 MB" },
            { title: "Moby Dick", format: "MP3", language: "English", sizeText: "405 MB" }
        ]
    }

    function test_visualEvidence() {
        if (typeof arc64EvidenceOutputDirectory === "undefined" || !arc64EvidenceOutputDirectory)
            skip("Set ARC64_EVIDENCE_DIR and use the optional Arc64 capture runner to save images")
        var states=["available","getting","choose"]
        for(var i=0;i<states.length;i++) {
            makeCard({offerState:states[i],label:states[i]==="getting"?"Getting audiobook · 37%":states[i]==="choose"?"Choose audiobook":"Get audiobook",meta:"M4B · English · 412 MB",progress:0.37,versions:sampleVersions()})
            waitForRendering(frame); grabImage(frame).save(arc64EvidenceOutputDirectory + "/P2/card-"+states[i]+".png"); cleanup()
        }
        makeChip(); waitForRendering(frame); grabImage(frame).save(arc64EvidenceOutputDirectory + "/P2/chip.png")
    }

    function test_available_state_renders() {
        var card = makeCard({
            offerState: "available",
            label: "Get audiobook",
            meta: "M4B · English · 412 MB",
            versions: sampleVersions()
        })
        compare(card.visible, true)
        compare(card.width, 290)
        compare(card.height, 158)
        compare(card.radius, 14)
        compare(findNamed(card, "reader2AudioOfferHeaderText").text, "Audiobook available")
        compare(findNamed(card, "reader2AudioOfferMeta").text, "M4B · English · 412 MB")
        compare(findNamed(card, "reader2AudioOfferGetButton").height, 40)
        verify(findNamed(card, "reader2AudioOfferGetButton").visible)
        verify(findNamed(card, "reader2AudioOfferOtherVersions").visible)
    }

    function test_getting_state_progress_binding() {
        var card = makeCard({
            offerState: "getting",
            label: "Getting audiobook · 37%",
            meta: "M4B · English · 412 MB",
            progress: 0.37,
            versions: sampleVersions()
        })
        var track = findNamed(card, "reader2AudioOfferProgressTrack")
        var fill = findNamed(card, "reader2AudioOfferProgressFill")
        verify(track !== null && track.visible)
        verify(fill !== null)
        verify(Math.abs(fill.width - track.width * 0.37) < 0.01,
               "progress fill must stay bound to the supplied progress")
        verify(!findNamed(card, "reader2AudioOfferGetButton").visible)
        verify(!findNamed(card, "reader2AudioOfferOtherVersions").visible)
    }

    function test_choose_state_renders_versions() {
        var rows = sampleVersions()
        var card = makeCard({
            offerState: "choose",
            label: "Choose audiobook",
            meta: "3 possible matches · none certain",
            versions: rows
        })
        compare(card.visible, true)
        verify(card.height > 158)
        compare(findNamed(card, "reader2AudioOfferChooseLabel").text, "Choose audiobook")
        verify(findNamed(card, "reader2AudioOfferVersionRow0") !== null)
        verify(findNamed(card, "reader2AudioOfferVersionRow1") !== null)
        verify(findNamed(card, "reader2AudioOfferVersionRow2") !== null)
        verify(!findNamed(card, "reader2AudioOfferGetButton").visible)
    }

    function test_get_emits_once() {
        var count = 0
        var card = makeCard({
            offerState: "available",
            label: "Get audiobook",
            meta: "M4B · English · 412 MB",
            versions: sampleVersions()
        })
        card.getRequested.connect(function() { count++ })
        mouseClick(findNamed(card, "reader2AudioOfferGetAction"))
        compare(count, 1)
    }

    function test_version_row_emits_that_row() {
        var chosen = null
        var rows = sampleVersions()
        var card = makeCard({
            offerState: "choose",
            label: "Choose audiobook",
            meta: "3 possible matches · none certain",
            versions: rows
        })
        card.versionChosen.connect(function(row) { chosen = row })
        mouseClick(findNamed(card, "reader2AudioOfferVersionAction1"))
        verify(chosen !== null)
        compare(chosen.format, "MP3")
        compare(chosen.sizeText, "398 MB")
    }

    function test_other_versions_emits_once() {
        var count = 0
        var card = makeCard({
            offerState: "available",
            label: "Get audiobook",
            meta: "M4B · English · 412 MB",
            versions: sampleVersions()
        })
        card.otherVersionsRequested.connect(function() { count++ })
        mouseClick(findNamed(card, "reader2AudioOfferOtherVersionsAction"))
        compare(count, 1)
    }

    function test_chip_emits_once() {
        var count = 0
        var chip = makeChip()
        chip.activated.connect(function() { count++ })
        mouseClick(findNamed(chip, "reader2AudioOfferChipAction"))
        compare(count, 1)
        compare(chip.width, 164)
        compare(chip.height, 29)
    }

    function test_click_targets_are_tab_reachable() {
        var card = makeCard({
            offerState: "available",
            label: "Get audiobook",
            meta: "M4B · English · 412 MB",
            versions: sampleVersions()
        })
        var getAction = findNamed(card, "reader2AudioOfferGetAction")
        var otherAction = findNamed(card, "reader2AudioOfferOtherVersionsAction")
        verify(getAction.activeFocusOnTab)
        verify(otherAction.activeFocusOnTab)
        compare(getAction.nextItemInFocusChain(true), otherAction)
        compare(otherAction.nextItemInFocusChain(false), getAction)

        cleanup()
        card = makeCard({
            offerState: "choose",
            label: "Choose audiobook",
            meta: "3 possible matches · none certain",
            versions: sampleVersions()
        })
        var first = findNamed(card, "reader2AudioOfferVersionAction0")
        var second = findNamed(card, "reader2AudioOfferVersionAction1")
        var third = findNamed(card, "reader2AudioOfferVersionAction2")
        verify(first.activeFocusOnTab)
        verify(second.activeFocusOnTab)
        verify(third.activeFocusOnTab)
        compare(first.nextItemInFocusChain(true), second)
        compare(second.nextItemInFocusChain(true), third)
        compare(second.nextItemInFocusChain(false), first)
        compare(third.nextItemInFocusChain(false), second)

        cleanup()
        var chip = makeChip()
        var chipAction = findNamed(chip, "reader2AudioOfferChipAction")
        verify(chipAction.activeFocusOnTab)
        compare(chipAction.nextItemInFocusChain(true), chipAction)
        compare(chipAction.nextItemInFocusChain(false), chipAction)
    }
}
