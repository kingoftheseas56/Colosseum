import QtQuick
import QtQuick.Window
import QtTest
import "../../../qml" as Colosseum
import "../../../qml/ratingsreviews" as RatingsReviews

TestCase {
    id: testCase
    name: "RatingConversionEditor"
    when: windowShown

    Window {
        id: testWindow
        width: 1180
        height: 900
        visible: true
    }

    property var legalHalfPoints: [
        0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5,
        5.5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5, 10
    ]
    property var positiveHalfPoints: [
        0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5,
        5.5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5, 10
    ]

    function recommendationNoZero() {
        var result = [null]
        for (var i = 0; i < positiveHalfPoints.length; ++i)
            result.push(positiveHalfPoints[i])
        return result
    }

    function halfPointDescriptors() {
        return [
            {
                providerId: "fixture-a",
                domainId: "fixture-no-zero-v1",
                domainVersion: 1,
                legalValues: positiveHalfPoints.slice(),
                recommendation: recommendationNoZero(),
                recommendedDigest: "fixture-no-zero-digest"
            },
            {
                providerId: "fixture-b",
                domainId: "fixture-halfpoint-v1",
                domainVersion: 1,
                legalValues: legalHalfPoints.slice(),
                recommendation: legalHalfPoints.slice(),
                recommendedDigest: "fixture-halfpoint-digest"
            }
        ]
    }

    Component {
        id: fakePreferencesComponent
        QtObject {
            property var stored: ({})
            property int saveCalls: 0
            property int resetCalls: 0
            property int providerSendCalls: 0
            property int canonicalMutationCalls: 0
            property bool failNext: false

            function conversionMap(providerId) {
                return stored[providerId] || null
            }

            function saveConversionMap(
                    providerId, domainId, domainVersion, outputs) {
                saveCalls++
                if (failNext) {
                    failNext = false
                    return { ok: false, error: "fixture save rejected" }
                }
                var digest = "saved-" + providerId + "-" + String(outputs[20])
                stored[providerId] = {
                    providerId: providerId,
                    domainId: domainId,
                    domainVersion: domainVersion,
                    outputs: outputs.slice(),
                    digest: digest
                }
                return { ok: true, digest: digest }
            }

            function resetConversionMap(
                    providerId, domainId, domainVersion, outputs) {
                resetCalls++
                if (failNext) {
                    failNext = false
                    return { ok: false, error: "fixture reset rejected" }
                }
                var digest = providerId === "fixture-a"
                    ? "fixture-no-zero-digest" : "fixture-halfpoint-digest"
                stored[providerId] = {
                    providerId: providerId,
                    domainId: domainId,
                    domainVersion: domainVersion,
                    outputs: outputs.slice(),
                    digest: digest
                }
                return {
                    ok: true,
                    digest: digest,
                    outputs: outputs.slice()
                }
            }
        }
    }

    Component {
        id: editorComponent
        RatingsReviews.RatingConversionEditor {}
    }

    property var preferences: null
    property var editor: null

    function createEditor() {
        preferences = fakePreferencesComponent.createObject(testWindow)
        verify(preferences !== null)
        editor = editorComponent.createObject(testWindow, {
            ratingPreferences: preferences,
            domainDescriptors: halfPointDescriptors(),
            width: 900,
            height: 800
        })
        verify(editor !== null)
        tryVerify(function() { return editor.workingOutputs.length === 21 })
        return editor
    }

    function cleanup() {
        if (editor)
            editor.destroy()
        if (preferences)
            preferences.destroy()
        editor = null
        preferences = null
    }

    function test_syntheticDomainShowsTwentyOneRowsAndUnavailableZero() {
        createEditor()
        compare(editor.workingOutputs.length, 21)
        compare(editor.workingOutputs[0], null)
        compare(editor.previewIndex, 0)
        compare(editor.previewText, "Unavailable")

        var first = findChild(editor, "settingsRatingConversionOutput_0")
        var last = findChild(editor, "settingsRatingConversionOutput_20")
        var reset = findChild(editor, "settingsRatingConversionReset")
        verify(first !== null)
        verify(last !== null)
        verify(reset !== null)
        verify(first.activeFocusOnTab)
        verify(reset.activeFocusOnTab)
        first.forceActiveFocus(Qt.TabFocusReason)
        verify(first.focus)
    }

    function test_nonMonotonicEditBlocksSaveWithoutSideEffects() {
        createEditor()
        verify(!editor.setOutputAt(10, 6))
        verify(editor.validationError.indexOf("monotonic") >= 0)
        verify(editor.dirty)

        var save = findChild(editor, "settingsRatingConversionSave")
        verify(save !== null)
        verify(!save.enabled)
        compare(preferences.saveCalls, 0)
        compare(preferences.providerSendCalls, 0)
        compare(preferences.canonicalMutationCalls, 0)
    }

    function test_saveResetProviderSwitchAndResetFailureAreDeterministic() {
        createEditor()

        verify(editor.setOutputAt(20, null))
        verify(editor.saveMap())
        compare(preferences.saveCalls, 1)
        verify(editor.mapDigest.indexOf("saved-fixture-a") === 0)
        verify(!editor.dirty)
        compare(preferences.providerSendCalls, 0)
        compare(preferences.canonicalMutationCalls, 0)

        verify(editor.resetMap())
        compare(preferences.resetCalls, 1)
        compare(editor.workingOutputs[0], null)
        compare(editor.workingOutputs[20], 10)
        compare(editor.mapDigest, "fixture-no-zero-digest")

        editor.selectedProviderIndex = 1
        tryCompare(editor, "mapDigest", "fixture-halfpoint-digest")
        compare(editor.selectedDomain.providerId, "fixture-b")
        compare(editor.workingOutputs[0], 0)
        compare(editor.workingOutputs[20], 10)

        verify(editor.setOutputAt(20, null))
        var before = editor.workingOutputs.slice()
        preferences.failNext = true
        verify(!editor.resetMap())
        compare(editor.workingOutputs.length, before.length)
        for (var i = 0; i < before.length; ++i)
            compare(editor.workingOutputs[i], before[i])
        verify(editor.validationError.indexOf("rejected") >= 0)
        compare(preferences.providerSendCalls, 0)
        compare(preferences.canonicalMutationCalls, 0)
    }
}
