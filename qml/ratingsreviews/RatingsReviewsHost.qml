pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

// Ratings & Reviews title page — Arc 49 Slice 2 approved composition:
// artwork hero over the house scrim, a separate read-only overall Colosseum
// rating, your ten-star personal score with a 0–10 half-step field, review
// writing that expands inline (Save privately / Cancel / explicit delete),
// an honest provider strip ("Score unavailable" until a permitted exact-match
// source exists), and the Colosseum community area held for the public service.
// The legacy composer + destination panel below is the frozen fixture/test
// surface: reachable only when the provider presentation is fixture-only.
Item {
    id: root
    objectName: "ratingsReviewsHost"
    anchors.fill: parent
    focus: true

    property var controller: null
    property var routeContext: ({})
    property string currentView: "main"
    property bool editorOpen: false
    property bool dirty: false
    property int routeGeneration: 0
    property int profileGeneration: 0
    property string identityWorld: ""
    property string identityKind: ""
    property string identityMediaId: ""
    property string titleText: ""
    property var canonical: ({})
    property var providerPresentation: ({})
    property var deliveryPresentation: ({})
    property var deliverySelections: ({})
    property var selectedReview: ({})
    property var editorReturnItem: null
    property var subviewReturnItem: null
    property string saveError: ""
    property string ratingFieldError: ""
    property string publishError: ""
    property int providerOperationCount: 0
    property var pendingNavigation: null
    readonly property bool fixtureMode: Boolean(providerPresentation ? providerPresentation.fixtureOnly : false)
    readonly property bool publicWarningFixture: Boolean(providerPresentation ? providerPresentation.publicWarningFixture : false)
    readonly property bool remoteDeleteUnavailableFixture: Boolean(providerPresentation ? providerPresentation.remoteDeleteUnavailableFixture : false)
    readonly property bool canonicalRatingPresent: Boolean(canonical && canonical.hasRating === true)
    readonly property bool canonicalReviewPresent: Boolean(canonical && canonical.hasReview === true)
    readonly property bool savedPrivately: (canonicalRatingPresent || canonicalReviewPresent) && !dirty

    property string draftRating: ""
    property string draftReview: ""
    property bool draftReviewPresent: false
    property bool draftSpoiler: false
    property string committedRating: ""
    property string committedReview: ""
    property bool committedReviewPresent: false
    property bool committedSpoiler: false

    // The identity-bearing provider set mirrored from the frozen native
    // projection order; production renders these badges honestly unavailable.
    readonly property var providerBadges: [
        { id: "mal", name: "MyAnimeList" },
        { id: "anilist", name: "AniList" },
        { id: "trakt", name: "Trakt" },
        { id: "simkl", name: "SIMKL" },
        { id: "imdb", name: "IMDb" },
        { id: "tmdb", name: "TMDb" },
        { id: "rotten_tomatoes", name: "Rotten Tomatoes" },
        { id: "metacritic", name: "Metacritic" }
    ]

    signal closeRequested()

    function beginRoute(context, openResult, generation) {
        routeContext = context || ({})
        routeGeneration = generation
        profileGeneration = Number(openResult.profileGeneration || 0)
        identityWorld = String(routeContext.identity ? routeContext.identity.world : "")
        identityKind = String(routeContext.identity ? routeContext.identity.kind : "")
        identityMediaId = String(routeContext.identity ? routeContext.identity.mediaId : "")
        titleText = String(routeContext.title ? routeContext.title.title : "")
        canonical = openResult.canonical || ({})
        providerPresentation = openResult.providers || ({})
        deliveryPresentation = openResult.delivery || ({})
        resetDeliverySelections()
        hydrateCommitted()
        currentView = "main"
        editorOpen = false
        dirty = false
        saveError = ""
        ratingFieldError = ""
        publishError = ""
        providerOperationCount = Number(openResult.providerOperationCount || 0)
        Qt.callLater(function() { backButton.forceActiveFocus(Qt.TabFocusReason) })
    }
    function hydrateCommitted() {
        var hasRating = canonical && canonical.hasRating === true
        var hasReview = canonical && canonical.hasReview === true
        committedRating = hasRating ? Number(canonical.rating).toFixed(1) : ""
        committedReview = hasReview ? String(canonical.review) : ""
        committedReviewPresent = hasReview
        committedSpoiler = hasReview && canonical.spoiler === true
        draftRating = committedRating
        draftReview = committedReview
        draftReviewPresent = committedReviewPresent
        draftSpoiler = committedSpoiler
    }

    function refreshCanonical() {
        if (!controller || !controller.canonicalProjection)
            return
        canonical = controller.canonicalProjection()
        providerPresentation = controller.providerPresentation()
        if (controller.deliveryPresentation)
            deliveryPresentation = controller.deliveryPresentation()
        hydrateCommitted()
        dirty = false
    }

    function recomputeDirty() {
        dirty = draftRating !== committedRating
             || draftReview !== committedReview
             || draftReviewPresent !== committedReviewPresent
             || draftSpoiler !== committedSpoiler
    }

    function parsedRating() {
        if (draftRating.trim().length === 0)
            return null
        var n = Number(draftRating)
        if (!isFinite(n) || n < 0 || n > 10 || Math.round(n * 2) !== n * 2)
            return undefined
        return n
    }

    // Ten stars carry whole points 1–10; the numeric field adds 0–10 halves.
    // Both edit the pending preview only — Save privately commits.
    function setStar(score) {
        ratingFieldError = ""
        draftRating = score.toFixed(1)
        recomputeDirty()
    }

    function setNumericRating(text) {
        var trimmed = String(text || "").trim()
        if (trimmed.length === 0) {
            ratingFieldError = ""
            return
        }
        var n = Number(trimmed)
        if (!isFinite(n) || n < 0 || n > 10 || Math.round(n * 2) !== n * 2) {
            ratingFieldError = "Rating must be 0 to 10 in half-point steps."
            return
        }
        ratingFieldError = ""
        draftRating = n.toFixed(1)
        recomputeDirty()
    }

    function saveDraft() {
        saveError = ""
        // An invalid numeric entry must not let an older draft save quietly.
        if (ratingFieldError.length > 0) {
            saveError = "Fix the rating before saving."
            return false
        }
        var rating = parsedRating()
        if (rating === undefined) {
            saveError = "Rating must be 0 to 10 in half-point steps."
            return false
        }
        // An emptied editor is never a silent delete; removing a saved review
        // is a named action.
        if (draftReviewPresent && draftReview.trim().length === 0
                && committedReviewPresent) {
            saveError = "Your review text is empty. Use Delete review to remove it, or restore your text."
            return false
        }
        if (!controller || !controller.saveLocal) {
            saveError = "Ratings & Reviews is unavailable."
            return false
        }
        var reviewValue = (draftReviewPresent && draftReview.trim().length > 0)
                          ? draftReview : null
        var result = controller.saveLocal(rating, reviewValue, draftSpoiler,
                                          routeGeneration, profileGeneration)
        providerOperationCount = Number(result && result.providerOperationCount || 0)
        if (!result || result.ok !== true) {
            saveError = String(result && result.errorCode ? result.errorCode : "Local save failed.")
            return false
        }
        canonical = result.canonical || controller.canonicalProjection()
        if (controller.deliveryPresentation)
            deliveryPresentation = controller.deliveryPresentation()
        committedRating = draftRating.trim().length ? Number(rating).toFixed(1) : ""
        committedReview = draftReviewPresent && draftReview.trim().length > 0 ? draftReview : ""
        committedReviewPresent = committedReview.length > 0
        committedSpoiler = draftSpoiler
        draftRating = committedRating
        draftReview = committedReview
        draftReviewPresent = committedReviewPresent
        draftSpoiler = committedSpoiler
        dirty = false
        return true
    }

    function clearRating() {
        if (!controller || !controller.clearRating)
            return
        var result = controller.clearRating(routeGeneration, profileGeneration)
        providerOperationCount = Number(result && result.providerOperationCount || 0)
        if (result && result.ok === true) {
            canonical = result.canonical || controller.canonicalProjection()
            hydrateCommitted()
        }
    }
    function deleteReview() {
        if (!controller || !controller.deleteReview)
            return
        var pendingRating = draftRating
        var ratingWasDirty = draftRating !== committedRating
        var result = controller.deleteReview(routeGeneration, profileGeneration)
        providerOperationCount = Number(result && result.providerOperationCount || 0)
        if (result && result.ok === true) {
            canonical = result.canonical || controller.canonicalProjection()
            hydrateCommitted()
            if (ratingWasDirty) {
                draftRating = pendingRating
                recomputeDirty()
            }
        }
    }

    function openEditor(invoker) {
        editorReturnItem = invoker || null
        hydrateCommitted()
        editorOpen = true
        saveError = ""
        ratingFieldError = ""
        // Scroll-into-view and focus run on the next event-loop pass so the
        // column's polish has settled and the positions are final.
        editorReveal.restart()
    }

    Timer {
        id: editorReveal
        interval: 16
        onTriggered: {
            var editorTop = editorPanel.y
            var maxY = Math.max(0, mainScroll.contentHeight - mainScroll.height)
            // Top-align so the whole editor (Save controls included) sits clear
            // of docked chrome hovering at the screen edge; only fall back to
            // bottom-alignment when the editor cannot fit with that clearance.
            var target = editorPanel.height + 240 <= mainScroll.height
                ? editorTop - 90
                : editorTop + editorPanel.height - mainScroll.height + 170
            mainScroll.contentY = Math.max(0, Math.min(target, maxY))
            editorTextArea.forceActiveFocus(Qt.TabFocusReason)
        }
    }
    // The column's polish lands after openEditor returns; follow the settling
    // geometry so the reveal aligns against final positions, not stale ones.
    Connections {
        target: mainColumn
        function onImplicitHeightChanged() {
            if (root.editorOpen)
                editorReveal.restart()
        }
    }

    function collapseEditor() {
        editorOpen = false
        var item = editorReturnItem
        editorReturnItem = null
        Qt.callLater(function() {
            if (item && item.visible && item.enabled !== false)
                item.forceActiveFocus(Qt.BacktabFocusReason)
            else
                backButton.forceActiveFocus(Qt.BacktabFocusReason)
        })
    }

    // ---- legacy fixture composer (frozen provider-delivery surface) ----
    function resetDeliverySelections() {
        var next = ({})
        var providers = deliveryPresentation && deliveryPresentation.providers
                        ? deliveryPresentation.providers : []
        for (var i = 0; i < providers.length; ++i) {
            var row = providers[i]
            next[String(row.providerId)] = {
                ratingSelected: row.ratingDefault === true && row.ratingEligible === true,
                reviewSelected: row.reviewDefault === true && row.reviewEligible === true,
                shortenedReview: ""
            }
        }
        deliverySelections = next
    }

    function deliverySelection(providerId) {
        return deliverySelections && deliverySelections[providerId]
               ? deliverySelections[providerId]
               : ({ ratingSelected: false, reviewSelected: false, shortenedReview: "" })
    }

    function setDeliverySelection(providerId, field, value) {
        var next = ({})
        for (var key in deliverySelections)
            next[key] = deliverySelections[key]
        var prior = deliverySelection(providerId)
        next[providerId] = {
            ratingSelected: field === "ratingSelected" ? value : prior.ratingSelected === true,
            reviewSelected: field === "reviewSelected" ? value : prior.reviewSelected === true,
            shortenedReview: field === "shortenedReview" ? String(value) : String(prior.shortenedReview || "")
        }
        deliverySelections = next
    }

    function selectedDeliveryDestinations() {
        var values = []
        var providers = deliveryPresentation && deliveryPresentation.providers
                        ? deliveryPresentation.providers : []
        for (var i = 0; i < providers.length; ++i) {
            var providerId = String(providers[i].providerId)
            var selection = deliverySelection(providerId)
            if (selection.ratingSelected === true || selection.reviewSelected === true) {
                var destination = {
                    providerId: providerId,
                    ratingSelected: selection.ratingSelected === true,
                    reviewSelected: selection.reviewSelected === true
                }
                if (selection.reviewSelected === true
                        && String(selection.shortenedReview || "").trim().length > 0) {
                    destination.shortenedReview = String(selection.shortenedReview)
                }
                values.push(destination)
            }
        }
        return values
    }

    function deliveryStateLabel(state) {
        if (state === "succeeded") return "Succeeded"
        if (state === "pending") return "Pending"
        if (state === "inFlight") return "Checking provider"
        if (state === "retrying") return "Retrying"
        if (state === "unknownOutcome") return "Needs checking"
        if (state === "failedTerminal") return "Failed"
        if (state === "needsAttention") return "Needs attention"
        return String(state || "")
    }

    function publishDraft() {
        publishError = ""
        if (dirty && !saveDraft())
            return false
        var destinations = selectedDeliveryDestinations()
        if (destinations.length === 0) {
            publishError = "Choose at least one provider field to publish."
            return false
        }
        if (!controller || !controller.publish) {
            publishError = "Provider publishing is unavailable."
            return false
        }
        var result = controller.publish(destinations, routeGeneration, profileGeneration)
        providerOperationCount = Number(result && result.providerOperationCount || 0)
        if (result && result.delivery)
            deliveryPresentation = result.delivery
        if (!result || result.ok !== true) {
            publishError = String(result && result.errorCode
                                  ? result.errorCode : "Provider publish failed.")
            return false
        }
        resetDeliverySelections()
        return true
    }

    function retryDelivery(operationId) {
        if (!controller || !controller.retryDelivery)
            return
        var result = controller.retryDelivery(operationId, routeGeneration, profileGeneration)
        if (result && result.delivery)
            deliveryPresentation = result.delivery
        publishError = result && result.ok === true ? ""
                     : String(result && result.errorCode || "Retry failed.")
    }

    function reconcileDelivery(operationId) {
        if (!controller || !controller.reconcileDelivery)
            return
        var result = controller.reconcileDelivery(operationId, routeGeneration, profileGeneration)
        if (result && result.delivery)
            deliveryPresentation = result.delivery
        publishError = result && result.ok === true ? ""
                     : String(result && result.errorCode || "Provider check failed.")
    }

    function openComposer(invoker) {
        subviewReturnItem = invoker
        hydrateCommitted()
        if (controller && controller.deliveryPresentation)
            deliveryPresentation = controller.deliveryPresentation()
        resetDeliverySelections()
        publishError = ""
        currentView = "composer"
        Qt.callLater(function() { composerBack.forceActiveFocus(Qt.TabFocusReason) })
    }

    function openProvider(row, invoker) {
        selectedReview = row || ({})
        subviewReturnItem = invoker
        currentView = "provider"
    }

    function restoreSubviewFocus() {
        var item = subviewReturnItem
        subviewReturnItem = null
        Qt.callLater(function() {
            if (item && item.visible && item.enabled !== false)
                item.forceActiveFocus(Qt.BacktabFocusReason)
            else
                backButton.forceActiveFocus(Qt.BacktabFocusReason)
        })
    }

    function finishPendingNavigation() {
        var next = pendingNavigation
        pendingNavigation = null
        dirtyDialog.close()
        if (next === "collapseEditor") {
            collapseEditor()
        } else if (next === "main") {
            currentView = "main"
            restoreSubviewFocus()
        } else if (next === "close") {
            closeRequested()
        }
    }
    function requestNavigation(next) {
        if (currentView === "composer" && dirty) {
            pendingNavigation = next
            dirtyDialog.open()
            Qt.callLater(function() { dirtyStay.forceActiveFocus(Qt.TabFocusReason) })
            return
        }
        if (next === "collapseEditor") {
            if (editorOpen && dirty) {
                pendingNavigation = next
                dirtyDialog.open()
                Qt.callLater(function() { dirtyStay.forceActiveFocus(Qt.TabFocusReason) })
                return
            }
            collapseEditor()
        } else if (next === "main") {
            currentView = "main"
            restoreSubviewFocus()
        } else if (next === "close") {
            if (dirty) {
                pendingNavigation = next
                dirtyDialog.open()
                Qt.callLater(function() { dirtyStay.forceActiveFocus(Qt.TabFocusReason) })
                return
            }
            closeRequested()
        }
    }

    function handleBack() {
        if (dirtyDialog.visible) {
            dirtyDialog.close()
            pendingNavigation = null
            return true
        }
        if (currentView === "provider") {
            currentView = "main"
            restoreSubviewFocus()
            return true
        }
        if (currentView === "composer") {
            requestNavigation("main")
            return true
        }
        if (editorOpen) {
            requestNavigation("collapseEditor")
            return true
        }
        requestNavigation("close")
        return true
    }

    Connections {
        target: root.controller
        function onRouteInvalidated() {
            root.closeRequested()
        }
        function onCanonicalChanged() {
            if ((root.currentView === "composer" || root.editorOpen) && root.dirty)
                return
            root.refreshCanonical()
        }
    }

    Keys.onEscapePressed: function(event) {
        event.accepted = root.handleBack()
    }

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        color: "#060708"
    }
    Item {
        id: mainView
        anchors.fill: parent
        visible: root.currentView === "main"

        BackAction {
            id: backButton
            objectName: "ratingsReviewsBack"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: Math.max(20, Math.min(theme.margin, parent.width * 0.05))
            anchors.topMargin: 18
            z: 5
            label: "Back"
            onTriggered: root.handleBack()
        }

        Flickable {
            id: mainScroll
            objectName: "ratingsReviewsMainScroll"
            anchors.fill: parent
            contentWidth: width
            contentHeight: mainColumn.implicitHeight + 70
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HouseScrollBar { flick: mainScroll }

            Column {
                id: mainColumn
                width: Math.min(parent.width, 1240)
                x: Math.max(0, (parent.width - width) / 2)
                spacing: 0

                // ---- hero: banner artwork under the house scrim ----
                Item {
                    id: hero
                    width: parent.width
                    height: heroArt.isPortrait ? 330 : 300

                    Image {
                        id: heroArt
                        anchors.fill: parent
                        source: root.routeContext && root.routeContext.title
                                ? String(root.routeContext.title.artwork || "") : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        opacity: status === Image.Ready ? 1.0 : 0.0
                        readonly property bool isPortrait:
                            status === Image.Ready && implicitHeight > implicitWidth * 1.15
                        Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                    }
                    // Missing art leaves a dignified dark stage, never a broken frame.
                    Rectangle {
                        anchors.fill: parent
                        visible: heroArt.status !== Image.Ready
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#101215" }
                            GradientStop { position: 1; color: "#07080a" }
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0; color: Qt.rgba(4/255, 5/255, 6/255, 0.15) }
                            GradientStop { position: 0.55; color: Qt.rgba(4/255, 5/255, 6/255, 0.52) }
                            GradientStop { position: 1; color: Qt.rgba(4/255, 5/255, 6/255, 0.96) }
                        }
                    }

                    // Portrait art (book/manga covers) doubles as a framed poster
                    // sharing the copy plane; landscape art stays full-bleed.
                    Rectangle {
                        id: heroPoster
                        visible: heroArt.isPortrait
                        width: 150
                        height: 222
                        radius: 10
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.22)
                        clip: true
                        anchors.left: parent.left
                        anchors.leftMargin: 28
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 26
                        Image {
                            anchors.fill: parent
                            source: heroArt.source
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }

                    Column {
                        id: heroCopy
                        anchors.left: heroPoster.visible ? heroPoster.right : parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: heroPoster.visible ? 26 : 34
                        anchors.rightMargin: 34
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 26
                        spacing: 9

                        Text {
                            text: "RATINGS & REVIEWS"
                            color: theme.gold
                            font.family: theme.ui
                            font.pixelSize: 11
                            font.letterSpacing: 3
                            font.capitalization: Font.AllUppercase
                        }
                        Text {
                            width: parent.width
                            text: root.titleText
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: mainColumn.width < 760 ? 30 : 44
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            style: Text.Raised
                            styleColor: Qt.rgba(0, 0, 0, 0.35)
                        }
                        Text {
                            visible: text.length > 0
                            width: parent.width
                            text: {
                                var parts = []
                                var year = root.routeContext && root.routeContext.title
                                           ? Number(root.routeContext.title.year || 0) : 0
                                if (year > 0)
                                    parts.push(String(year))
                                var sub = root.routeContext && root.routeContext.title
                                          ? String(root.routeContext.title.subtitle || "") : ""
                                if (sub.length)
                                    parts.push(sub)
                                return parts.join("  –  ")
                            }
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                    }
                }

                // ---- three-part composition ----
                Item { width: 1; height: 20 }

                Flow {
                    width: parent.width
                    spacing: 14

                    // overall Colosseum rating — read-only, never the personal score
                    Rectangle {
                        objectName: "ratingsReviewsOverallCard"
                        width: mainColumn.width >= 1000 ? (mainColumn.width - 28) * 0.32 : mainColumn.width
                        height: overallCol.implicitHeight + 44
                        radius: 15
                        color: Qt.rgba(1, 1, 1, 0.045)
                        border.width: 1
                        border.color: Qt.rgba(240/255, 196/255, 74/255, 0.30)
                        Column {
                            id: overallCol
                            anchors.fill: parent
                            anchors.margins: 22
                            spacing: 10
                            Text {
                                text: "COLOSSEUM RATING"
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                                font.letterSpacing: 1.8
                            }
                            Row {
                                spacing: 9
                                Text {
                                    id: overallScore
                                    text: "—"
                                    color: "#e9dfc0"
                                    font.family: theme.display
                                    font.pixelSize: 50
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: "/ 10"
                                    color: theme.inkDimmer
                                    font.pixelSize: 13
                                    anchors.baseline: overallScore.baseline
                                }
                            }
                            Text {
                                text: "Rating unavailable"
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 14
                            }
                            Text {
                                width: parent.width
                                text: "The community average appears when public ratings exist for this title."
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // your rating — ten stars + half-step field
                    Rectangle {
                        objectName: "ratingsReviewsRatingCard"
                        width: mainColumn.width >= 1000 ? (mainColumn.width - 28) * 0.36 : mainColumn.width
                        height: ratingCol.implicitHeight + 44
                        radius: 15
                        color: Qt.rgba(1, 1, 1, 0.045)
                        border.width: 1
                        border.color: theme.edge
                        Column {
                            id: ratingCol
                            anchors.fill: parent
                            anchors.margins: 22
                            spacing: 11
                            Text {
                                text: "YOUR COLOSSEUM RATING"
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                                font.letterSpacing: 1.8
                            }
                            Row {
                                spacing: 9
                                Text {
                                    id: pendingScore
                                    objectName: "ratingsReviewsPendingScore"
                                    text: root.draftRating.length ? Number(root.draftRating).toFixed(1) : "—"
                                    color: theme.ink
                                    font.family: theme.display
                                    font.pixelSize: 50
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: "/ 10"
                                    color: theme.inkDimmer
                                    font.pixelSize: 13
                                    anchors.baseline: pendingScore.baseline
                                }
                            }
                            Row {
                                id: starRow
                                spacing: 2
                                Repeater {
                                    model: 10
                                    delegate: Item {
                                        id: starSlot
                                        required property int index
                                        objectName: "rrStar_" + (index + 1)
                                        width: 26
                                        height: 28
                                        readonly property real fill: {
                                            var value = Number(root.draftRating || 0)
                                            if (!isFinite(value)) return 0
                                            return Math.max(0, Math.min(1, value - index))
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            text: "★"
                                            color: "#565660"
                                            font.family: theme.ui
                                            font.pixelSize: 21
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            text: "★"
                                            color: theme.gold
                                            font.family: theme.ui
                                            font.pixelSize: 21
                                            clip: true
                                            width: parent.width * starSlot.fill
                                        }
                                        KeyboardAction {
                                            anchors.fill: parent
                                            accessibleName: "Rate " + (starSlot.index + 1) + " out of 10"
                                            focusRadius: 9
                                            onTriggered: root.setStar(starSlot.index + 1)
                                        }
                                    }
                                }
                            }
                            Row {
                                spacing: 9
                                TextField {
                                    id: numericField
                                    objectName: "ratingsReviewsRatingField"
                                    width: 66
                                    height: 32
                                    text: root.draftRating
                                    placeholderText: "0–10"
                                    horizontalAlignment: TextInput.AlignHCenter
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    validator: DoubleValidator {
                                        bottom: 0; top: 10; decimals: 1; notation: DoubleValidator.StandardNotation
                                    }
                                    onTextEdited: root.setNumericRating(text)
                                    Keys.onEnterPressed: function(event) { root.setNumericRating(text); event.accepted = true }
                                    Keys.onReturnPressed: function(event) { root.setNumericRating(text); event.accepted = true }
                                    background: Rectangle {
                                        radius: 6
                                        color: "#0c0e10"
                                        border.width: 1
                                        border.color: numericField.activeFocus ? theme.gold : "#ffffff30"
                                    }
                                }
                                Text {
                                    text: "/ 10 · halves"
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            Text {
                                objectName: "ratingsReviewsRatingFieldError"
                                visible: root.ratingFieldError.length > 0
                                text: root.ratingFieldError
                                color: "#d78373"
                                font.family: theme.ui
                                font.pixelSize: 10
                            }
                            Row {
                                spacing: 14
                                Text {
                                    objectName: "ratingsReviewsClearRating"
                                    text: "Clear rating"
                                    color: starClearMouse.containsMouse ? theme.gold : theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    visible: root.canonicalRatingPresent
                                    MouseArea {
                                        id: starClearMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.clearRating()
                                    }
                                }
                            }
                            Rectangle {
                                objectName: "ratingsReviewsSaveRating"
                                width: saveRatingLabel.implicitWidth + 34
                                height: 38
                                radius: 10
                                visible: root.dirty && root.draftRating !== root.committedRating
                                color: saveRatingKey.interactionActive ? "#f5cf6b" : theme.gold
                                Behavior on color { ColorAnimation { duration: 120 } }
                                Text {
                                    id: saveRatingLabel
                                    anchors.centerIn: parent
                                    text: "Save privately"
                                    color: "#1a1306"
                                    font.family: theme.ui
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                KeyboardAction {
                                    id: saveRatingKey
                                    anchors.fill: parent
                                    accessibleName: "Save rating privately"
                                    focusRadius: 10
                                    onTriggered: root.saveDraft()
                                }
                            }
                        }
                    }

                    // your review
                    Rectangle {
                        objectName: "ratingsReviewsReviewCard"
                        width: mainColumn.width >= 1000 ? (mainColumn.width - 28) * 0.32 : mainColumn.width
                        height: reviewCol.implicitHeight + 44
                        radius: 15
                        color: Qt.rgba(1, 1, 1, 0.045)
                        border.width: 1
                        border.color: theme.edge
                        Column {
                            id: reviewCol
                            anchors.fill: parent
                            anchors.margins: 22
                            spacing: 10
                            Text {
                                text: "YOUR COLOSSEUM REVIEW"
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                                font.letterSpacing: 1.8
                            }
                            Text {
                                width: parent.width
                                text: root.canonicalReviewPresent
                                      ? root.committedReview
                                      : (root.dirty && root.draftReviewPresent && root.draftReview.length
                                         ? root.draftReview : "A place for what stayed with you.")
                                color: root.canonicalReviewPresent || (root.dirty && root.draftReviewPresent && root.draftReview.length)
                                      ? theme.inkDim : theme.inkDimmer
                                font.family: theme.display
                                font.pixelSize: root.canonicalReviewPresent ? 16 : 18
                                wrapMode: Text.WordWrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: !root.canonicalReviewPresent
                                text: "Your review is private until you post it."
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                            }
                            Row {
                                spacing: 10
                                Item {
                                    id: savedBadge
                                    objectName: "ratingsReviewsSavedBadge"
                                    visible: root.savedPrivately
                                    width: savedBadgeText.implicitWidth + 20
                                    height: 24
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: height / 2
                                        color: Qt.rgba(131/255, 200/255, 152/255, 0.12)
                                        border.width: 1
                                        border.color: Qt.rgba(131/255, 200/255, 152/255, 0.35)
                                    }
                                    Text {
                                        id: savedBadgeText
                                        anchors.centerIn: parent
                                        text: "✓ Saved privately"
                                        color: "#b9cbb5"
                                        font.family: theme.ui
                                        font.pixelSize: 11
                                    }
                                }
                            }
                            Rectangle {
                                id: writeButton
                                objectName: "ratingsReviewsOpenEditor"
                                width: writeLabel.implicitWidth + 36
                                height: 40
                                radius: 11
                                color: writeKey.interactionActive ? "#f5cf6b" : theme.gold
                                Behavior on color { ColorAnimation { duration: 120 } }
                                Row {
                                    id: writeLabel
                                    anchors.centerIn: parent
                                    spacing: 8
                                    Text {
                                        text: root.canonicalReviewPresent ? "Edit my review" : "Write a review"
                                        color: "#1a1306"
                                        font.family: theme.ui
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text: "→"
                                        color: "#1a1306"
                                        font.family: theme.ui
                                        font.pixelSize: 13
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                                KeyboardAction {
                                    id: writeKey
                                    anchors.fill: parent
                                    accessibleName: root.canonicalReviewPresent
                                        ? "Edit my review" : "Write a review"
                                    focusRadius: 11
                                    onTriggered: root.openEditor(writeButton)
                                }
                            }
                        }
                    }
                }

                // ---- inline editor: expands beneath the cards ----
                Rectangle {
                    id: editorPanel
                    objectName: "ratingsReviewsEditor"
                    visible: root.editorOpen
                    width: parent.width
                    height: root.editorOpen ? editorCol.implicitHeight + 46 : 0
                    radius: 15
                    color: Qt.rgba(1, 1, 1, 0.035)
                    border.width: 1
                    border.color: Qt.rgba(240/255, 196/255, 74/255, 0.32)
                    clip: true

                    Column {
                        id: editorCol
                        x: 23
                        y: 23
                        width: parent.width - 46
                        spacing: 13

                        Item {
                            width: parent.width
                            height: editorHeading.implicitHeight
                            Text {
                                id: editorHeading
                                anchors.left: parent.left
                                text: "YOUR REVIEW"
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                                font.letterSpacing: 1.8
                            }
                            Text {
                                id: editorClose
                                objectName: "ratingsReviewsEditorCancel"
                                anchors.right: parent.right
                                text: "✕"
                                color: editorCloseMouse.containsMouse ? theme.gold : theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 15
                                MouseArea {
                                    id: editorCloseMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.requestNavigation("collapseEditor")
                                }
                            }
                        }
                        Text {
                            text: "What stayed with you?"
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: 24
                        }
                        TextArea {
                            id: editorTextArea
                            objectName: "ratingsReviewsEditorText"
                            width: parent.width
                            height: 150
                            text: root.draftReview
                            placeholderText: "The moment, thought, or feeling you carried away…"
                            wrapMode: TextArea.Wrap
                            selectByMouse: true
                            font.family: theme.ui
                            font.pixelSize: 14
                            color: theme.ink
                            background: Rectangle {
                                radius: 10
                                color: Qt.rgba(4/255, 5/255, 6/255, 0.72)
                                border.width: 1
                                border.color: editorTextArea.activeFocus ? theme.gold : "#ffffff2d"
                            }
                            // No activeFocus guard: text can arrive focused (typing)
                            // or unfocused (drivers, accessibility), and a silent
                            // drop would turn the next Save into a rating-only
                            // commit. Empty text is handled by saveDraft's
                            // named-delete guard, not here.
                            onTextChanged: {
                                root.draftReview = text
                                root.draftReviewPresent = true
                                root.recomputeDirty()
                            }
                            Keys.onShortcutOverride: function(event) {
                                if (event.key === Qt.Key_Return
                                        && (event.modifiers & Qt.ControlModifier) !== 0)
                                    event.accepted = true
                            }
                            Keys.onPressed: function(event) {
                                if (event.key === Qt.Key_Return
                                        && (event.modifiers & Qt.ControlModifier) !== 0) {
                                    if (root.saveDraft())
                                        root.collapseEditor()
                                    event.accepted = true
                                }
                            }
                        }
                        Text {
                            objectName: "ratingsReviewsEditorLength"
                            text: root.draftReview.length + " / 16384"
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 10
                        }
                        Row {
                            spacing: 12
                            CheckBox {
                                id: editorSpoiler
                                objectName: "ratingsReviewsEditorSpoiler"
                                text: "Contains spoilers"
                                checked: root.draftSpoiler
                                font.family: theme.ui
                                font.pixelSize: 12
                                onToggled: {
                                    root.draftSpoiler = checked
                                    root.recomputeDirty()
                                }
                            }
                            Item { width: 8; height: 1 }
                            Text {
                                objectName: "ratingsReviewsDeleteReview"
                                visible: root.canonicalReviewPresent
                                text: "Delete review"
                                color: deleteMouse.containsMouse ? theme.gold : theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                                MouseArea {
                                    id: deleteMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: deleteReviewDialog.open()
                                }
                            }
                        }
                        Text {
                            objectName: "ratingsReviewsSaveError"
                            visible: root.saveError.length > 0
                            width: parent.width
                            text: root.saveError
                            color: "#d78373"
                            font.family: theme.ui
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                        Row {
                            spacing: 10
                            Rectangle {
                                id: editorCancelButton
                                objectName: "ratingsReviewsEditorCancelButton"
                                width: editorCancelLabel.implicitWidth + 32
                                height: 40
                                radius: 11
                                color: editorCancelKey.interactionActive
                                       ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                                border.width: 1
                                border.color: theme.edge
                                Text {
                                    id: editorCancelLabel
                                    anchors.centerIn: parent
                                    text: "Cancel"
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                KeyboardAction {
                                    id: editorCancelKey
                                    anchors.fill: parent
                                    accessibleName: "Cancel editing"
                                    focusRadius: 11
                                    onTriggered: root.requestNavigation("collapseEditor")
                                }
                            }
                            Rectangle {
                                id: editorSaveButton
                                objectName: "ratingsReviewsSavePrivate"
                                width: editorSaveLabel.implicitWidth + 34
                                height: 40
                                radius: 11
                                color: editorSaveKey.interactionActive ? "#f5cf6b" : theme.gold
                                Behavior on color { ColorAnimation { duration: 120 } }
                                Text {
                                    id: editorSaveLabel
                                    anchors.centerIn: parent
                                    text: "Save privately"
                                    color: "#1a1306"
                                    font.family: theme.ui
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.saveDraft())
                                            root.collapseEditor()
                                    }
                                }
                                KeyboardAction {
                                    id: editorSaveKey
                                    anchors.fill: parent
                                    pointerEnabled: false
                                    accessibleName: "Save review privately"
                                    focusRadius: 11
                                    onTriggered: {
                                        if (root.saveDraft())
                                            root.collapseEditor()
                                    }
                                }
                            }
                        }
                        Text {
                            text: "Saving keeps this on your profile. It does not post to the community."
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 10
                        }
                    }
                }

                // ---- provider ratings ----
                // Fixture builds keep the legacy full-page composer reachable for the
                // frozen delivery journey; production never shows it.
                Button {
                    objectName: "ratingsReviewsOpenComposer"
                    visible: root.fixtureMode
                    text: "Legacy composer (fixture)"
                    onClicked: root.openComposer(null)
                }
                Column {
                    width: parent.width
                    spacing: 9
                    Item { width: 1; height: 12 }
                    Text {
                        text: "Provider ratings"
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "Scores appear only when a connected source confirms this exact title."
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 11
                    }
                    RatingsReviewsProviderStrip {
                        width: parent.width
                        visible: !!(root.providerPresentation
                                    && root.providerPresentation.visibleOrder
                                    && root.providerPresentation.visibleOrder.length > 0)
                        controller: root.controller
                        presentation: root.providerPresentation
                        routeGeneration: root.routeGeneration
                        profileGeneration: root.profileGeneration
                    }
                    // Production: persistent honest badges — every card keeps its
                    // name and reserved score space, no vanished strip.
                    Flow {
                        id: providerBadges
                        objectName: "ratingsReviewsProviderBadges"
                        width: parent.width
                        visible: !(root.providerPresentation
                                   && root.providerPresentation.visibleOrder
                                   && root.providerPresentation.visibleOrder.length)
                        spacing: 10
                        Repeater {
                            model: root.providerBadges
                            delegate: Rectangle {
                                required property var modelData
                                id: badgeCard
                                objectName: "rrBadge_" + modelData.id
                                width: 150
                                height: 86
                                radius: 13
                                color: Qt.rgba(1, 1, 1, 0.04)
                                border.width: 1
                                border.color: theme.edge
                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 6
                                    Text {
                                        text: badgeCard.modelData.name
                                        color: theme.inkDim
                                        font.family: theme.ui
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                    Text {
                                        objectName: "rrBadgeScore_" + badgeCard.modelData.id
                                        text: "Score unavailable"
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 13
                                    }
                                    Text {
                                        text: "Awaiting source"
                                        color: "#79818a"
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                    }
                                }
                            }
                        }
                    }
                }

                // ---- Colosseum community (held for the public service) ----
                Column {
                    width: parent.width
                    spacing: 10
                    Item { width: 1; height: 16 }
                    Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.10) }
                    Text {
                        text: "What people carried with them"
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "Colosseum community reviews for this title."
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 11
                    }
                    Rectangle {
                        objectName: "ratingsReviewsCommunityState"
                        width: parent.width
                        height: communityCol.implicitHeight + 44
                        radius: 15
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.16)
                        color: "transparent"
                        Column {
                            id: communityCol
                            anchors.fill: parent
                            anchors.margins: 22
                            spacing: 7
                            Text {
                                text: "Colosseum community reviews aren't available yet."
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 14
                            }
                            Text {
                                width: parent.width
                                text: "They arrive with the Colosseum community service. Your rating and review stay private to your profile until you choose to post."
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                Item { width: 1; height: 34 }
            }
        }
    }
    Item {
        id: composerView
        objectName: "ratingsReviewsComposer"
        anchors.fill: parent
        visible: root.currentView === "composer"

        Rectangle { anchors.fill: parent; color: "#060708" }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 36
            spacing: 16

            Button {
                id: composerBack
                objectName: "ratingsReviewsComposerBack"
                text: "Back to Ratings & Reviews"
                onClicked: root.requestNavigation("main")
            }

            Text {
                Layout.fillWidth: true
                text: "Your " + root.titleText + " review"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 36
                elide: Text.ElideRight
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 14

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 690
                    radius: 17
                    color: Qt.rgba(1, 1, 1, 0.04)
                    border.width: 1
                    border.color: theme.edge

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Colosseum rating"; color: theme.inkDim }
                            TextField {
                                id: ratingField
                                objectName: "ratingsReviewsComposerRating"
                                Layout.preferredWidth: 90
                                text: root.draftRating
                                placeholderText: "0–10"
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                onTextEdited: {
                                    root.draftRating = text
                                    root.recomputeDirty()
                                }
                            }
                            Label { text: "/ 10"; color: theme.inkDimmer }
                            Item { Layout.fillWidth: true }
                            CheckBox {
                                id: spoilerCheck
                                objectName: "ratingsReviewsComposerSpoiler"
                                text: "Spoiler"
                                checked: root.draftSpoiler
                                onToggled: {
                                    root.draftSpoiler = checked
                                    root.recomputeDirty()
                                }
                            }
                        }
                        TextArea {
                            id: reviewEditor
                            objectName: "ratingsReviewsComposerReview"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: root.draftReview
                            placeholderText: "Write your review"
                            wrapMode: TextArea.Wrap
                            selectByMouse: true
                            onTextChanged: {
                                if (activeFocus) {
                                    root.draftReview = text
                                    root.draftReviewPresent = true
                                    root.recomputeDirty()
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: root.saveError.length > 0
                            text: root.saveError
                            color: "#d78373"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button {
                                objectName: "ratingsReviewsDeleteReviewComposer"
                                visible: !!(root.canonical && root.canonical.hasReview === true)
                                text: "Delete review"
                                onClicked: deleteReviewDialog.open()
                            }
                            Button {
                                id: saveButton
                                objectName: "ratingsReviewsSave"
                                text: "Save"
                                enabled: root.dirty
                                onClicked: root.saveDraft()
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 370
                    Layout.fillHeight: true
                    radius: 17
                    color: Qt.rgba(1, 1, 1, 0.04)
                    border.width: 1
                    border.color: theme.edge

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: "Destination"
                                color: theme.inkDimmer
                                font.pixelSize: 10
                            }
                            Text { text: "Rating"; color: theme.inkDimmer; font.pixelSize: 10 }
                            Text { text: "Review"; color: theme.inkDimmer; font.pixelSize: 10 }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 48
                            radius: 9
                            color: "#12171e"
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                Text { Layout.fillWidth: true; text: "Colosseum"; color: theme.ink }
                                Text { text: "ALWAYS"; color: "#d8b56c"; font.pixelSize: 9 }
                                Text { text: "ALWAYS"; color: "#d8b56c"; font.pixelSize: 9 }
                            }
                        }

                        Text {
                            objectName: "ratingsReviewsPublicWarningFixture"
                            Layout.fillWidth: true
                            visible: root.publicWarningFixture
                            text: "This review will be public"
                            color: "#d8b56c"
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            objectName: "ratingsReviewsRemoteDeleteUnavailableFixture"
                            Layout.fillWidth: true
                            visible: root.remoteDeleteUnavailableFixture
                            text: "External review deletion is unavailable in Package 1."
                            color: theme.inkDimmer
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: root.deliveryPresentation
                                   && root.deliveryPresentation.providers
                                   ? root.deliveryPresentation.providers : []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: providerDeliveryColumn.implicitHeight + 16
                                radius: 8
                                color: "#0c1117"
                                ColumnLayout {
                                    id: providerDeliveryColumn
                                    anchors.fill: parent
                                    anchors.margins: 9
                                    spacing: 4
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            Layout.fillWidth: true
                                            text: String(modelData.providerId)
                                            color: theme.inkDim
                                            font.pixelSize: 11
                                        }
                                        CheckBox {
                                            objectName: "ratingsReviewsDeliveryRating_" + String(modelData.providerId)
                                            enabled: modelData.ratingEligible === true
                                            checked: root.deliverySelection(String(modelData.providerId)).ratingSelected === true
                                            onToggled: root.setDeliverySelection(
                                                String(modelData.providerId), "ratingSelected", checked)
                                        }
                                        CheckBox {
                                            id: reviewDestinationCheck
                                            objectName: "ratingsReviewsDeliveryReview_" + String(modelData.providerId)
                                            enabled: modelData.reviewEligible === true
                                            checked: root.deliverySelection(String(modelData.providerId)).reviewSelected === true
                                            onToggled: root.setDeliverySelection(
                                                String(modelData.providerId), "reviewSelected", checked)
                                        }
                                    }
                                    TextField {
                                        objectName: "ratingsReviewsDeliveryShortenedReview_" + String(modelData.providerId)
                                        Layout.fillWidth: true
                                        visible: reviewDestinationCheck.checked
                                                 && modelData.reviewEligible === true
                                        placeholderText: "Optional shorter review for this provider"
                                        text: root.deliverySelection(
                                                  String(modelData.providerId)).shortenedReview
                                        onTextEdited: root.setDeliverySelection(
                                            String(modelData.providerId), "shortenedReview", text)
                                    }
                                    Text {
                                        objectName: "ratingsReviewsDeliveryReason_" + String(modelData.providerId)
                                        Layout.fillWidth: true
                                        visible: String(modelData.reason || "").length > 0
                                        text: String(modelData.reason || "")
                                        color: "#d78373"
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: reviewDestinationCheck.checked
                                                 && modelData.reviewPublic === true
                                        text: "This review will be public"
                                        color: "#d8b56c"
                                        font.pixelSize: 10
                                    }
                                }
                            }
                        }
                        Button {
                            id: publishButton
                            objectName: "ratingsReviewsPublish"
                            Layout.fillWidth: true
                            visible: !!(root.deliveryPresentation
                                        && root.deliveryPresentation.available === true)
                            text: "Publish selected fields"
                            onClicked: root.publishDraft()
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: root.publishError.length > 0
                            text: root.publishError
                            color: "#d78373"
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                        Repeater {
                            model: root.deliveryPresentation
                                   && root.deliveryPresentation.operations
                                   ? root.deliveryPresentation.operations : []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: deliveryOperationRow.implicitHeight + 14
                                radius: 8
                                color: "#12171e"
                                RowLayout {
                                    id: deliveryOperationRow
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 6
                                    Text {
                                        Layout.fillWidth: true
                                        objectName: "ratingsReviewsDeliveryState_"
                                                    + String(modelData.providerId) + "_"
                                                    + String(modelData.operationType)
                                        text: root.deliveryStateLabel(String(modelData.state))
                                        color: String(modelData.state) === "succeeded"
                                               ? "#9acaa0" : theme.inkDim
                                        font.pixelSize: 10
                                    }
                                    Button {
                                        objectName: "ratingsReviewsDeliveryRetry_"
                                                    + String(modelData.providerId) + "_"
                                                    + String(modelData.operationType)
                                        visible: String(modelData.state) === "failedTerminal"
                                                 || String(modelData.state) === "needsAttention"
                                        text: "Retry"
                                        onClicked: root.retryDelivery(String(modelData.operationId || ""))
                                    }
                                    Button {
                                        objectName: "ratingsReviewsDeliveryReconcile_"
                                                    + String(modelData.providerId) + "_"
                                                    + String(modelData.operationType)
                                        visible: String(modelData.state) === "unknownOutcome"
                                        text: "Check provider"
                                        onClicked: root.reconcileDelivery(String(modelData.operationId || ""))
                                    }
                                }
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }
    Item {
        id: providerView
        objectName: "ratingsReviewsProviderPage"
        anchors.fill: parent
        visible: root.currentView === "provider"

        RatingsReviewsProviderDetail {
            anchors.fill: parent
            controller: root.controller
            row: root.selectedReview
            onBackRequested: {
                root.currentView = "main"
                root.restoreSubviewFocus()
            }
        }
    }

    Dialog {
        id: deleteReviewDialog
        objectName: "ratingsReviewsDeleteDialog"
        parent: root
        width: 460
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        modal: true
        title: "Delete private review"
        standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            spacing: 14
            Text {
                Layout.preferredWidth: 380
                text: "Delete your private review for this title? Your rating is preserved."
                color: theme.ink
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Button {
                    objectName: "ratingsReviewsDeleteConfirm"
                    text: "Delete"
                    onClicked: {
                        root.deleteReview()
                        deleteReviewDialog.close()
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "ratingsReviewsDeleteCancel"
                    text: "Cancel"
                    onClicked: deleteReviewDialog.close()
                }
            }
        }
    }

    Dialog {
        id: dirtyDialog
        objectName: "ratingsReviewsDirtyDialog"
        parent: root
        width: 460
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        modal: true
        title: "Unsaved changes"
        closePolicy: Popup.NoAutoClose

        contentItem: ColumnLayout {
            spacing: 14
            Text {
                Layout.preferredWidth: 380
                text: "Save your rating and review privately before leaving?"
                color: theme.ink
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    id: dirtySave
                    objectName: "ratingsReviewsDirtySave"
                    text: "Save privately"
                    onClicked: {
                        if (root.saveDraft())
                            root.finishPendingNavigation()
                    }
                }
                Button {
                    objectName: "ratingsReviewsDirtyDiscard"
                    text: "Discard changes"
                    onClicked: {
                        root.draftRating = root.committedRating
                        root.draftReview = root.committedReview
                        root.draftReviewPresent = root.committedReviewPresent
                        root.draftSpoiler = root.committedSpoiler
                        root.dirty = false
                        root.saveError = ""
                        root.ratingFieldError = ""
                        root.finishPendingNavigation()
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    id: dirtyStay
                    objectName: "ratingsReviewsDirtyStay"
                    text: "Keep writing"
                    onClicked: {
                        dirtyDialog.close()
                        root.pendingNavigation = null
                    }
                }
            }
        }
    }
}
