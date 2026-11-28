import QtQuick
import QtQuick.Controls
import ".."
Item {
    id: root
    required property var worldData
    required property var row
    property bool home: false
    property bool cold: false
    property Item backdrop: null
    property Flickable pageFlick: null
    property bool homePageVisible: false
    property int exposureRevision: 0
    property real cardSize: 196
    // Square, self-contained cassette/radio widgets like every other card (Hemanth 2026-11-28).
    readonly property bool wideListeningRow: false
    readonly property bool songLines: home && ["quick-picks", "continue-listening"].includes(row.homeKey || row.key)
    property real squareSpacing: home ? 18 : 24
    property int previewCount: songLines ? 12 : 8
    property int currentIndex: 0
    property var openRowHandler: null
    property var retryRowHandler: null
    property string statusNote: ""
    // Opt-in stable slot for streamed square shelves; other shelves size to their contents.
    property real reservedCardHeight: 0
    property bool cardsRevealed: true
    property bool showCardPlaceholder: false
    property bool trackCardArtwork: false
    readonly property bool cardArtworkSettled: {
        if (!trackCardArtwork) return true;
        artworkRevision;
        for (var i = 0; i < cardRepeater.count; ++i) {
            var card = cardRepeater.itemAt(i);
            if (!card || !card.artworkSettled) return false;
        }
        return true;
    }
    property int artworkRevision: 0
    readonly property bool reducedMotion: !!(Window.window && Window.window["reducedMotion"] === true)
    readonly property int statusHeight: 36
    readonly property string rowStatus: String(root.row && root.row.status || "")
    readonly property bool showRowStatus: statusNote.length > 0 || (!root.hasCards && root.row
        && (root.row.recommendation === true || root.row.personal === false)
        && ["loading", "stale", "error"].indexOf(rowStatus) >= 0)
    readonly property bool hasCards: !!(root.row && root.row.items && root.row.items.length > 0)
    readonly property bool showRowRetry: showRowStatus && rowStatus !== "loading"
        && root.row.canRetry === true
        && (root.retryRowHandler || (root.worldData && (typeof root.worldData.retryHomeRow === "function"
            || typeof root.worldData.retryHomeRecommendations === "function")))
    readonly property string rowStatusText: {
        if(statusNote) return statusNote;
        if(rowStatus === "loading") return "Loading this shelf…";
        if(rowStatus === "stale") return root.row.loading === true
            ? "Loading this shelf…" : "Could not refresh this shelf.";
        if(rowStatus === "error") return "Could not load this shelf.";
        return "";
    }
    function reveal(index) {
        var item = cardRepeater.itemAt(index);
        if (!item) return;
        var left = item.x;
        var right = item.x + item.width + 2 * cards.x;
        rail.contentX = Math.max(0, Math.min(Math.max(0, rail.contentWidth - rail.width),
            left < rail.contentX ? left : right > rail.contentX + rail.width ? right - rail.width : rail.contentX));
    }
    function exposureGeometryChanged() { exposureRevision++; }
    onRowChanged: exposureGeometryChanged()
    onHomePageVisibleChanged: exposureGeometryChanged()
    function handleKeys(event) {
        if (!songLines || ![Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down].includes(event.key)) {
            nav.handle(event);
            return;
        }
        // Lines are filled down each column in recommendation order.
        const rows = cards.rows;
        const offset = currentIndex % rows;
        let target = -1;
        if (event.key === Qt.Key_Left) target = currentIndex - rows;
        if (event.key === Qt.Key_Right) target = currentIndex + rows;
        if (event.key === Qt.Key_Up && offset > 0) target = currentIndex - 1;
        if (event.key === Qt.Key_Down && offset < rows - 1) target = currentIndex + 1;
        const backward = target < currentIndex;
        event.accepted = target >= 0 && target < nav.count
            && nav.moveTo(target, backward ? Qt.BacktabFocusReason : Qt.TabFocusReason);
    }
    Keys.onPressed: event => root.handleKeys(event)
    KeyboardCollectionController {
        id:nav;view:root;count:root.cardsRevealed ? cardRepeater.count : 0;orientation:"horizontal"
        positionIndexFn:function(index){root.reveal(index);Qt.callLater(function(){var card=cardRepeater.itemAt(index);if(card)card.forceActiveFocus()})}
        onActivated:index=>root.worldData.activate(root.row.items[index])
    }
    // Keep space for the optional status line so stale and failed replies cannot move later shelves.
    implicitHeight: reservedCardHeight > 0 ? header.height + statusHeight + 22 + reservedCardHeight + 2 * cards.y
        : header.height + rowStatusBar.height + (hasCards ? 22 + rail.height : 0)
    Theme { id: theme }
    WidgetHeader {
        id: header
        width: Math.max(0,parent.width-16)
        title: root.row.title
        moreLabel: "See all"
        navigable: root.hasCards && root.cardsRevealed
        automationId: (root.home ? "vinylHomeSeeAll_" : "vinylShelfSeeAll_") + root.row.key
        onMoreClicked: {
            if (root.openRowHandler) root.openRowHandler(root.row);
            else root.worldData.openList(root.row.key, root.home);
        }
    }
    Item {
        id: rowStatusBar
        objectName: "vinylShelfStatusBar_" + root.row.key
        y: header.height
        width: parent.width
        height: root.showRowStatus ? root.statusHeight : 0
        visible: root.showRowStatus

        Text {
            objectName: "vinylShelfStatus_" + root.row.key
            x: 0
            y: (parent.height - height) / 2
            width: Math.max(0, parent.width - (retryRow.visible ? 50 : 0))
            text: root.rowStatusText
            textFormat: Text.PlainText
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 13
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        VinylWorldButton {
            id: retryRow
            objectName: "vinylShelfRetry_" + root.row.key
            x: parent.width - width
            y: 0
            width: 38
            height: 36
            visible: root.showRowRetry
            enabled: visible
            label: "Retry " + String(root.row.title || "shelf")
            symbol: "retry"
            backdrop: root.backdrop
            onTriggered: {
                if (root.retryRowHandler) root.retryRowHandler();
                else if (typeof root.worldData.retryHomeRow === "function")
                    root.worldData.retryHomeRow(root.row);
                else if (typeof root.worldData.retryHomeRecommendations === "function")
                    root.worldData.retryHomeRecommendations();
            }
        }
    }
    Item {
        id: placeholder
        y: rail.y
        width: rail.width
        height: root.reservedCardHeight + 2 * cards.y
        visible: root.showCardPlaceholder && root.reservedCardHeight > 0
        clip: true
        Row {
            x: cards.x; y: cards.y
            spacing: root.squareSpacing
            Repeater {
                model: placeholder.visible ? Math.min(root.previewCount,
                    Math.ceil(rail.width / (root.cardSize + root.squareSpacing))) : 0
                delegate: VinylArtwork {
                    width: root.cardSize; height: width
                    backgroundColor: theme.vaultSurface
                }
            }
        }
    }
    Flickable {
        id: rail
        y: header.height + rowStatusBar.height + 22
        width: parent.width
        height: root.hasCards ? cards.implicitHeight + 2 * cards.y : 0
        contentWidth: cards.implicitWidth + 2 * cards.x
        contentHeight: height
        onContentXChanged: root.exposureGeometryChanged()
        onWidthChanged: root.exposureGeometryChanged()
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        visible: root.hasCards && root.cardsRevealed
        enabled: visible
        opacity: root.reservedCardHeight <= 0 || (root.hasCards && root.cardsRevealed) ? 1 : 0
        Behavior on opacity {
            enabled: root.reservedCardHeight > 0 && !root.reducedMotion && root.cardsRevealed && root.hasCards
            NumberAnimation { duration: theme.vaultMotionDuration; easing.type: Easing.OutCubic }
        }
        Grid {
            id: cards
            x: 3; y: 3
            flow: root.songLines ? Grid.TopToBottom : Grid.LeftToRight
            rows: root.songLines ? Math.max(1, Math.ceil(cardRepeater.count / 3)) : 1
            columns: root.songLines ? 3 : Math.max(1, cardRepeater.count)
            columnSpacing: root.songLines ? 22 : root.wideListeningRow ? 4 : root.squareSpacing
            rowSpacing: 0
            Repeater {
                id: cardRepeater
                model: Math.min(root.previewCount,(root.row.items || []).length)
                onItemAdded: if (root.trackCardArtwork) root.artworkRevision++
                delegate: VinylItemCard {
                    required property int index
                    objectName: "vinylCard_" + (root.cold ? "cold-" : "") + root.row.key + "_" + index
                    worldData: root.worldData
                    item: root.row.items[index]
                    artistRadioAction: root.home && typeof root.worldData.radioHomeItem === "function"
                        ? function(item) { root.worldData.radioHomeItem(item); } : null
                    albumPlayAction: root.home && typeof root.worldData.playHomeItem === "function"
                        ? function(item) { root.worldData.playHomeItem(item); } : null
                    recommendationRow: root.home ? root.row : null
                    pageFlick: root.pageFlick
                    exposureRail: rail
                    backdrop: root.backdrop
                    homePageVisible: root.homePageVisible
                    exposureRevision: root.exposureRevision
                    onActiveFocusChanged: if(activeFocus){root.currentIndex=index;root.reveal(index)}
                    Keys.onPressed: event => {
                        if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Home, Qt.Key_End].includes(event.key)
                            || (root.songLines && [Qt.Key_Up, Qt.Key_Down].includes(event.key)))
                            root.handleKeys(event);
                    }
                    wide: root.wideListeningRow && (presentationKind === "mixes" || presentationKind === "radio")
                    songLine: root.songLines
                    width: songLine ? Math.max(220, (rail.width - 2 * cards.x - 2 * cards.columnSpacing) / 3)
                        : wide ? implicitWidth : root.cardSize
                    height: implicitHeight
                }
            }
        }
    }
    Rectangle {
        visible:root.home && root.hasCards && !root.wideListeningRow && !root.songLines
        x:root.width*0.92; y:rail.y; width:root.width*0.08; height:rail.height
        gradient:Gradient { orientation:Gradient.Horizontal; GradientStop { position:0; color:"transparent" } GradientStop { position:1; color:"#06070b" } }
    }
    Connections {
        target: root.home ? root.pageFlick : null
        ignoreUnknownSignals: true
        function onContentYChanged() { root.exposureGeometryChanged() }
        function onHeightChanged() { root.exposureGeometryChanged() }
        function onVisibleChanged() { root.exposureGeometryChanged() }
    }
}
