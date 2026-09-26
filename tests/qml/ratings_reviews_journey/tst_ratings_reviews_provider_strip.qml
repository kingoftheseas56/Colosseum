import QtQuick
import QtQuick.Window
import QtTest
import "../../../qml/ratingsreviews" as RatingsReviews

TestCase {
    id: testCase
    name: "RatingsReviewsProviderStrip"
    when: windowShown

    property var controller: null
    property var strip: null

    Window {
        id: testWindow
        width: 900
        height: 320
        visible: true
    }

    Component {
        id: controllerComponent
        QtObject {
            property int moveCalls: 0
            property int preferenceWrites: 0
            property int expectedRouteGeneration: 41
            property int expectedProfileGeneration: 17

            function displayName(id) {                return id === "imdb" ? "IMDb" : id
            }

            function moveProvider(providerId, direction, visibleOrder,
                                  routeGeneration, profileGeneration) {
                moveCalls++
                if (routeGeneration !== expectedRouteGeneration
                        || profileGeneration !== expectedProfileGeneration)
                    return { changed: false }

                var full = [
                    "mal", "anilist", "trakt", "simkl",
                    "imdb", "tmdb", "rotten_tomatoes", "metacritic"
                ]
                var visible = visibleOrder.slice()
                var from = visible.indexOf(providerId)
                var to = from + (direction < 0 ? -1 : 1)
                if (from < 0 || to < 0 || to >= visible.length) {
                    return {
                        changed: false,
                        savedOrder: full,
                        visibleOrder: visible,
                        announcement: ""
                    }
                }

                var moved = visible.splice(from, 1)[0]
                visible.splice(to, 0, moved)
                var visibleSet = ({})
                for (var i = 0; i < visible.length; ++i)
                    visibleSet[visible[i]] = true
                var vi = 0
                for (var j = 0; j < full.length; ++j) {
                    if (visibleSet[full[j]] === true)
                        full[j] = visible[vi++]
                }
                preferenceWrites++
                return {
                    ok: true,
                    changed: true,
                    savedOrder: full,
                    visibleOrder: visible,
                    providerId: providerId,
                    direction: direction < 0 ? "left" : "right",
                    announcement: displayName(providerId)
                        + " moved to position " + (to + 1)
                        + " of " + visible.length,
                    providerOperationCount: 0
                }
            }
        }
    }

    Component {
        id: stripComponent
        RatingsReviews.RatingsReviewsProviderStrip {}
    }

    function presentation() {
        return {
            savedOrder: [
                "mal", "anilist", "trakt", "simkl",
                "imdb", "tmdb", "rotten_tomatoes", "metacritic"
            ],
            visibleOrder: ["mal", "imdb"],
            aggregates: [
                {
                    providerId: "mal",
                    providerName: "MyAnimeList",
                    state: "ready",
                    scoreDisplay: "8.4 / 10"
                },
                {
                    providerId: "imdb",
                    providerName: "IMDb",
                    state: "ready",
                    scoreDisplay: "8.6 / 10"
                }
            ]
        }
    }

    function createStrip() {        controller = controllerComponent.createObject(testWindow)
        verify(controller !== null)
        strip = stripComponent.createObject(testWindow.contentItem, {
            controller: controller,
            presentation: presentation(),
            routeGeneration: 41,
            profileGeneration: 17,
            width: 700
        })
        verify(strip !== null)
        var state = findChild(strip, "rrProviderOrderState")
        verify(state !== null)
        tryCompare(state, "visibleOrderCsv", "mal,imdb")
        return state
    }

    function init() {
        testWindow.requestActivate()
        wait(20)
    }

    function cleanup() {
        if (strip)
            strip.destroy()
        if (controller)
            controller.destroy()
        strip = null
        controller = null
    }

    function test_keyboardReorderPreservesHiddenSlotsAndFocus() {
        var state = createStrip()
        var imdb = findChild(strip, "rrProvider_imdb")
        verify(imdb !== null)
        imdb.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return imdb.activeFocus })
        keyClick(Qt.Key_Left)

        tryCompare(state, "visibleOrderCsv", "imdb,mal")
        compare(state.savedOrderCsv,
                "imdb,anilist,trakt,simkl,mal,tmdb,rotten_tomatoes,metacritic")
        compare(controller.preferenceWrites, 1)
        compare(state.focusedProviderId, "imdb")
        compare(state.lastMoveProviderId, "imdb")
        compare(state.lastMoveDirection, "left")
        compare(state.lastAnnouncement, "IMDb moved to position 1 of 2")

        var moved = findChild(strip, "rrProvider_imdb")
        verify(moved !== null)
        moved.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Left)
        compare(controller.preferenceWrites, 1)
        compare(state.visibleOrderCsv, "imdb,mal")
    }

    function test_pointerDragUsesSameSingleCommitPath() {
        var state = createStrip()
        var imdb = findChild(strip, "rrProvider_imdb")
        verify(imdb !== null)

        mouseDrag(imdb,
                  imdb.width - 20, imdb.height / 2,
                  -(imdb.width - 30), 0,
                  Qt.LeftButton, Qt.NoModifier, 40)

        compare(controller.moveCalls, 1)
        compare(controller.preferenceWrites, 1)
        tryCompare(state, "visibleOrderCsv", "imdb,mal")
        compare(state.focusedProviderId, "imdb")
        compare(state.lastMoveDirection, "left")
    }
}
