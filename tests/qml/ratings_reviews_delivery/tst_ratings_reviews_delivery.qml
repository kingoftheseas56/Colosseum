import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import "../../../qml/ratingsreviews" as RatingsReviews

TestCase {
    id: testCase
    name: "RatingsReviewsDelivery"
    when: windowShown

    property var controller: null
    property var host: null

    Window {
        id: testWindow
        width: 1180
        height: 860
        visible: true
    }

    Component {
        id: fakeControllerComponent
        QtObject {
            property var canonical: ({ hasRating: true, rating: 8.5,
                                       hasReview: true, review: "Exact review",
                                       spoiler: false })
            property var providers: ({ savedOrder: [], visibleOrder: [],
                                       aggregates: [], reviews: [] })
            property var delivery: ({
                available: true,
                providers: [
                    { providerId: "fixture-a", ratingEligible: true,
                      reviewEligible: true, reason: "", reviewPublic: true },
                    { providerId: "fixture-spoiler-blocked", ratingEligible: false,
                      reviewEligible: false,
                      reason: "This provider cannot preserve spoiler protection.",
                      reviewPublic: false }
                ],
                operations: []
            })
            property int saveCalls: 0
            property int publishCalls: 0
            property int retryCalls: 0
            property int reconcileCalls: 0
            property var lastDestinations: []
            signal routeInvalidated()

            function canonicalProjection() { return canonical }
            function providerPresentation() { return providers }
            function deliveryPresentation() { return delivery }
            function saveLocal(rating, review, spoiler, routeGeneration, profileGeneration) {
                saveCalls++
                canonical = {
                    hasRating: rating !== null && rating !== undefined,
                    rating: Number(rating || 0),
                    hasReview: review !== null && review !== undefined,
                    review: String(review || ""),
                    spoiler: spoiler === true
                }
                return { ok: true, canonical: canonical, providerOperationCount: 0 }
            }
            function clearRating() { return { ok: true, canonical: canonical, providerOperationCount: 0 } }
            function deleteReview() { return { ok: true, canonical: canonical, providerOperationCount: 0 } }
            function moveProvider() { return { changed: false } }
            function reviewSourceAvailable() { return false }
            function openReviewSource() { return false }
            function publish(destinations) {
                publishCalls++
                lastDestinations = destinations
                var nextDelivery = {
                    available: true,
                    providers: delivery.providers,
                    operations: [{ providerId: "fixture-a",
                                   operationType: "review.set",
                                   state: "succeeded", staleReason: "",
                                   attemptCount: 1 }]
                }
                delivery = nextDelivery
                return { ok: true, providerOperationCount: 1,
                         delivery: nextDelivery }
            }
            function retryDelivery() {
                retryCalls++
                return { ok: true, providerOperationCount: 0, delivery: delivery }
            }
            function reconcileDelivery() {
                reconcileCalls++
                return { ok: true, providerOperationCount: 0, delivery: delivery }
            }
        }
    }

    Component { id: hostComponent; RatingsReviews.RatingsReviewsHost {} }

    function routeContext() {
        return {
            identity: { world: "theatre", kind: "series", mediaId: "ct1:49f10000-0000-4000-8000-000000000001" },
            title: { title: "Frieren: Beyond Journey's End", subtitle: "Series" },
            origin: "theatre-detail", readIds: {}
        }
    }

    function createHost() {
        controller = fakeControllerComponent.createObject(testWindow)
        verify(controller !== null)
        host = hostComponent.createObject(testWindow.contentItem, { controller: controller })
        verify(host !== null)
        host.beginRoute(routeContext(), {
            ok: true, profileGeneration: 17, canonical: controller.canonical,
            providers: controller.providers, delivery: controller.delivery,
            providerOperationCount: 0
        }, 41)
        host.openComposer(null)
    }

    function cleanup() {
        if (host) host.destroy()
        if (controller) controller.destroy()
        host = null
        controller = null
    }

    function test_dirtyDraftSavesBeforeExplicitFixturePublish() {
        createHost()
        host.draftReview = "Changed exactly once"
        host.recomputeDirty()
        verify(host.dirty)
        var rating = findChild(host, "ratingsReviewsDeliveryRating_fixture-a")
        var review = findChild(host, "ratingsReviewsDeliveryReview_fixture-a")
        var publish = findChild(host, "ratingsReviewsPublish")
        verify(rating !== null)
        verify(review !== null)
        verify(publish !== null)
        rating.click()
        review.click()
        tryVerify(function() { return rating.checked && review.checked })
        publish.click()
        compare(controller.saveCalls, 1)
        compare(controller.publishCalls, 1)
        compare(controller.lastDestinations.length, 1)
        verify(controller.lastDestinations[0].ratingSelected)
        verify(controller.lastDestinations[0].reviewSelected)
        compare(host.providerOperationCount, 1)
        var status = findChild(host, "ratingsReviewsDeliveryState_fixture-a_review.set")
        verify(status !== null)
        compare(status.text, "Succeeded")
    }

    function test_spoilerIncapableFixtureHasNoSelectableField() {
        createHost()
        var rating = findChild(host, "ratingsReviewsDeliveryRating_fixture-spoiler-blocked")
        var review = findChild(host, "ratingsReviewsDeliveryReview_fixture-spoiler-blocked")
        var reason = findChild(host, "ratingsReviewsDeliveryReason_fixture-spoiler-blocked")
        verify(rating !== null)
        verify(review !== null)
        verify(reason !== null)
        verify(!rating.enabled)
        verify(!review.enabled)
        verify(reason.visible)
        compare(reason.text, "This provider cannot preserve spoiler protection.")
    }

    function test_seededDefaultsReturnAfterOnePublishOverride() {
        createHost()
        controller.delivery.providers = [
            { providerId: "fixture-a", ratingEligible: true, reviewEligible: true,
              ratingDefault: true, reviewDefault: true, reason: "", reviewPublic: false },
            { providerId: "fixture-b", ratingEligible: true, reviewEligible: false,
              ratingDefault: true, reviewDefault: false, reason: "", reviewPublic: false }
        ]
        host.deliveryPresentation = controller.deliveryPresentation()
        host.resetDeliverySelections()
        verify(host.deliverySelection("fixture-a").ratingSelected)
        verify(host.deliverySelection("fixture-a").reviewSelected)
        verify(host.deliverySelection("fixture-b").ratingSelected)
        host.setDeliverySelection("fixture-a", "reviewSelected", false)
        verify(host.publishDraft())
        compare(controller.lastDestinations.length, 2)
        verify(!controller.lastDestinations[0].reviewSelected)
        host.openComposer(null)
        verify(host.deliverySelection("fixture-a").reviewSelected)
        verify(host.deliverySelection("fixture-b").ratingSelected)
    }

    function test_clickingSeededReviewDefaultKeepsBothRatingDefaults() {
        createHost()
        controller.delivery.providers = [
            { providerId: "fixture-a", ratingEligible: true, reviewEligible: true,
              ratingDefault: true, reviewDefault: true, reason: "", reviewPublic: false },
            { providerId: "fixture-b", ratingEligible: true, reviewEligible: false,
              ratingDefault: true, reviewDefault: false, reason: "", reviewPublic: false }
        ]
        host.deliveryPresentation = controller.deliveryPresentation()
        host.resetDeliverySelections()
        var review = findChild(host, "ratingsReviewsDeliveryReview_fixture-a")
        verify(review !== null)
        tryVerify(function() { return review.checked })
        review.click()
        tryVerify(function() { return !review.checked })
        var destinations = host.selectedDeliveryDestinations()
        compare(destinations.length, 2)
        compare(destinations[0].providerId, "fixture-a")
        verify(destinations[0].ratingSelected)
        verify(!destinations[0].reviewSelected)
        compare(destinations[1].providerId, "fixture-b")
        verify(destinations[1].ratingSelected)
    }

    function test_unknownAndFailedRowsOfferOnlyTheirLawfulAction() {
        createHost()
        controller.delivery.operations = [
            { providerId: "fixture-a", operationType: "rating.set",
              state: "failedTerminal", staleReason: "", attemptCount: 1 },
            { providerId: "fixture-a", operationType: "review.set",
              state: "unknownOutcome", staleReason: "", attemptCount: 1 }
        ]
        host.deliveryPresentation = {
            available: controller.delivery.available,
            providers: controller.delivery.providers,
            operations: controller.delivery.operations
        }
        var retry = findChild(host, "ratingsReviewsDeliveryRetry_fixture-a_rating.set")
        var check = findChild(host, "ratingsReviewsDeliveryReconcile_fixture-a_review.set")
        verify(retry !== null)
        verify(check !== null)
        retry.click()
        check.click()
        compare(controller.retryCalls, 1)
        compare(controller.reconcileCalls, 1)
    }
}
