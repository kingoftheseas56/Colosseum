// TheatreSeeAllPage — one shelf's infinite grid. Back + title + factual source header, the
// shared CataloguePosterGrid, skeletons, incremental loading, a retryable error state, and an
// honest "no longer available" state that NAMES the provider when a pinned extension is gone.
// It NEVER ranks in QML — it pages TheatreApi.loadRowPage and renders what comes back. The
// loader is injectable (pageLoader) so the offscreen harness can drive paging deterministically.
import QtQuick
import "TheatreApi.js" as TheatreApi

Item {
    id: seeAll

    property var pin: null
    property var malCatalog: null
    property var imdbCatalog: null
    property bool showExplicit: false
    property var explicitFilter: null
    property var pageLoader: null            // (pin, offset, limit, options, done) — defaults to the API
    signal itemRequested(var item)
    signal backRequested()

    property var items: []
    property bool loading: false
    property bool loadingMore: false
    property bool hasMore: true
    property string errorText: ""
    property string warning: ""
    property bool missing: false
    property string providerName: ""
    property int generation: 0
    property var pendingOffset: -1
    property var nextCursor: 0
    property var _retryCursor: null
    property int _retryBaseCount: -1
    property var _failedFactsIds: []
    property var _semanticCancel: null
    function cancelPage() { if (typeof _semanticCancel === "function") _semanticCancel(); _semanticCancel = null; }
    Component.onDestruction: cancelPage()

    readonly property string titleText: seeAll.pin ? (seeAll.pin.title || "") : ""
    readonly property string sourceLabel: seeAll.pin ? (seeAll.pin.extName || seeAll.pin.sourceLabel || "") : ""
    // Theatre See-all renders the approved gallery poster profile (harness-observable).
    readonly property string posterProfile: wall.visualProfile
    readonly property bool isExtension: seeAll.pin
        && (seeAll.pin.sourceKind === "extension" || seeAll.pin.sourceKind === "service-extension")

    Theme { id: theme }

    function _loader() { return seeAll.pageLoader ? seeAll.pageLoader : TheatreApi.loadRowPage; }

    onPinChanged: seeAll.reset()

    function reset() {
        seeAll.cancelPage();
        seeAll.items = []; seeAll.hasMore = true; seeAll.errorText = ""; seeAll.warning = "";
        seeAll.missing = false; seeAll.providerName = "";
        seeAll.loading = false; seeAll.loadingMore = false; seeAll.pendingOffset = -1;
        seeAll.nextCursor = 0; seeAll._retryCursor = null; seeAll._retryBaseCount = -1;
        seeAll._failedFactsIds = [];
        seeAll.generation += 1;
        if (seeAll.pin) seeAll.loadPage(0);
    }

    function loadPage(offset) {
        if (!seeAll.pin) return;
        if (seeAll.pendingOffset === offset) return;      // coalesce an identical in-flight offset
        if (offset !== 0 && !seeAll.hasMore && seeAll._retryCursor === null) return;
        // The retry starts at the original raw/ranked cursor, not after a
        // provisional validated prefix shown while the failed batch was pending.
        var baseCount = seeAll._retryCursor !== null && seeAll._retryBaseCount >= 0
                      ? seeAll._retryBaseCount : (offset === 0 ? 0 : seeAll.items.length);
        seeAll.pendingOffset = offset;
        if (offset === 0) seeAll.loading = true; else seeAll.loadingMore = true;
        var gen = seeAll.generation;
        var opts = { generation: gen, malCatalog: seeAll.malCatalog, imdbCatalog: seeAll.imdbCatalog,
                     showExplicit: seeAll.showExplicit, explicitFilter: seeAll.explicitFilter };
        seeAll.cancelPage();
        seeAll._semanticCancel = seeAll._loader()(seeAll.pin, offset, 40, opts, function(res) {
            if (gen !== seeAll.generation) return;         // stale: the pin changed under us
            seeAll.pendingOffset = -1;
            seeAll.loading = false; seeAll.loadingMore = false;
            if (res.missing) {
                seeAll.missing = true; seeAll.providerName = res.extName || "";
                seeAll.hasMore = false; return;
            }
            if (res.error) {
                seeAll.errorText = res.error;
                seeAll._retryCursor = offset;
                seeAll._retryBaseCount = baseCount;
                seeAll._failedFactsIds = res.failedFactsIds || [];
                // A failed later page can also contain validated cards. On every
                // retry replace only that attempted page, never append it twice.
                if (res.items && res.items.length)
                    seeAll.items = seeAll.items.slice(0, baseCount).concat(res.items);
                return;
            }
            seeAll._retryCursor = null; seeAll._retryBaseCount = -1;
            seeAll._failedFactsIds = [];
            seeAll.warning = res.warning || "";
            seeAll.items = (offset === 0) ? (res.items || [])
                            : seeAll.items.slice(0, baseCount).concat(res.items || []);
            seeAll.hasMore = res.hasMore === true;
            seeAll.nextCursor = res.nextCursor !== undefined ? res.nextCursor : seeAll.items.length;
        });
    }
    function requestMore() {
        if (seeAll.hasMore && !seeAll.loadingMore && !seeAll.loading && !seeAll.errorText)
            seeAll.loadPage(seeAll.nextCursor);
    }
    function retry() {
        var offset = seeAll._retryCursor !== null ? seeAll._retryCursor : seeAll.nextCursor;
        var opts = { generation: seeAll.generation, malCatalog: seeAll.malCatalog, imdbCatalog: seeAll.imdbCatalog,
                     showExplicit: seeAll.showExplicit, explicitFilter: seeAll.explicitFilter,
                     failedFactsIds: seeAll._failedFactsIds };
        if (!seeAll.pageLoader && typeof TheatreApi.retryRowPage === "function")
            TheatreApi.retryRowPage(seeAll.pin, offset, 40, opts);
        seeAll.errorText = "";
        seeAll.loadPage(offset);
    }

    // ── header: back · title · factual source attribution ──
    Item {
        id: header
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 64

        Item {
            id: backRow
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: chev.width + backLbl.width + 8
            height: 40
            Text {
                id: chev
                text: "‹"                       // ‹ chevron (glyph, not emoji)
                color: backMa.containsMouse ? theme.ink : theme.inkDim
                font.family: theme.ui; font.pixelSize: 26; font.weight: Font.DemiBold
                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                id: backLbl
                text: "Back"
                color: backMa.containsMouse ? theme.ink : theme.inkDim
                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                anchors.left: chev.right; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter
            }
            MouseArea {
                id: backMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: seeAll.backRequested()
            }
            KeyboardAction {
                id: backKeyboard; anchors.fill: parent; pointerEnabled: false
                accessibleName: "Back"; focusRadius: 8; onTriggered: seeAll.backRequested()
            }
        }

        Column {
            anchors.left: backRow.right; anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                text: seeAll.titleText
                color: theme.ink
                font.family: theme.display; font.pixelSize: 22; font.weight: Font.DemiBold
            }
            Text {
                visible: seeAll.isExtension && seeAll.sourceLabel.length > 0
                text: "via " + seeAll.sourceLabel
                color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 12
            }
        }
    }

    // ── the poster wall ──
    CataloguePosterGrid {
        id: wall
        objectName: "theatreSeeAllPosterWall"
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: seeAll.errorText.length > 0 && seeAll.items.length > 0 && !seeAll.missing
                     ? retryColumn.bottom : header.bottom
        anchors.topMargin: seeAll.errorText.length > 0 && seeAll.items.length > 0 ? 8 : 0
        anchors.bottom: parent.bottom
        anchors.leftMargin: 6; anchors.rightMargin: 6
        visualProfile: "gallery"
        visible: !seeAll.missing
        items: seeAll.items
        loading: seeAll.loading
        loadingMore: seeAll.loadingMore
        hasMore: seeAll.hasMore
        emptyMessage: seeAll.errorText.length > 0 ? "" : "This shelf answered with nothing."
        onRequestMore: seeAll.requestMore()
        onItemRequested: (it) => seeAll.itemRequested(it)
    }

    // ── retryable error state ──
    Column {
        id: retryColumn
        objectName: "theatreSeeAllRetryColumn"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: seeAll.items.length === 0 ? parent.verticalCenter : undefined
        anchors.top: seeAll.items.length > 0 ? header.bottom : undefined
        anchors.topMargin: seeAll.items.length > 0 ? 6 : 0
        visible: seeAll.errorText.length > 0 && !seeAll.missing
        spacing: 12
        Text {
            text: seeAll.errorText
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Rectangle {
            width: 120; height: 36; radius: 8
            anchors.horizontalCenter: parent.horizontalCenter
            color: retryMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
            border.width: 1; border.color: theme.edge
            Text {
                anchors.centerIn: parent; text: "Retry"
                color: theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
            }
            MouseArea { id: retryMa; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor; onClicked: seeAll.retry() }
            KeyboardAction {
                id: retryKeyboard; objectName: "theatreSeeAllRetryAction"
                anchors.fill: parent; pointerEnabled: false
                accessibleName: "Retry"; focusRadius: parent.radius; onTriggered: seeAll.retry()
            }
        }
    }

    // ── honest "extension gone" state, naming the provider ──
    Text {
        anchors.centerIn: parent
        visible: seeAll.missing
        width: parent.width * 0.7
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: (seeAll.providerName.length > 0 ? seeAll.providerName : "This catalogue")
              + " is no longer available. It may have been removed or disabled in Extensions."
        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
    }
}
