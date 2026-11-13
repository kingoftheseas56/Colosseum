import QtQuick
import QtQuick.Controls
import ".."
import "VinylCardPresentation.js" as Presentation

KeyboardAction {
    id: root
    focusOnPointer: false
    required property var worldData
    required property var item
    property var recommendationRow: null
    property Flickable pageFlick: null
    property Item exposureRail: null
    property Item backdrop: null
    property bool homePageVisible: false
    property int exposureRevision: 0
    property string feedbackError: ""
    readonly property bool feedbackAvailable: !!recommendationRow
        && (recommendationRow.recommendation === true
            || ((recommendationRow.key === "taste-artists" || recommendationRow.key === "saved-artists")
                && recommendationRow.kind === "artists"))
        && !!item && !!item.ref
        && worldData && typeof worldData.setRecommendationFeedback === "function"
        && typeof worldData.recommendationProfileGeneration === "function"
        && typeof worldData.recommendationArtistRef === "function"
    readonly property bool quickFeedbackAvailable: feedbackAvailable && recommendationRow.recommendation === true
    readonly property bool feedbackRevealed: quickFeedbackAvailable && (hovered || activeFocus
        || likeBadge.hovered || likeBadge.activeFocus || hideBadge.hovered || hideBadge.activeFocus)

    enabled: root.item.unavailable !== true
    readonly property string reason: item.reason || ""
    readonly property string presentationKind: Presentation.kind(item)
    readonly property string artistText: Presentation.artist(item)
    readonly property bool includesTitle: presentationKind === "songs"
    readonly property string secondaryText: Presentation.secondary(item, artistText)
        + (feedbackError ? "\n" + feedbackError : "")
    readonly property string albumText: {
        var record = item.song && item.song.album || item.record || {};
        var name = String(item.albumTitle || record.title || "");
        return name === String(item.title || "") ? "" : name;
    }
    readonly property string captionTitle: presentationKind === "songs"
        ? artistText + (albumText ? qsTr(" · from %1").arg(albumText) : "") : String(item.title || "")
    readonly property string captionDetail: item.unavailable ? qsTr("Unavailable")
        : reason || (presentationKind === "artists" ? qsTr("Artist radio")
            : presentationKind === "mixes" ? String(item.subtitle || qsTr("Made for you"))
            : presentationKind === "radio" ? String(item.subtitle || qsTr("Station"))
            : presentationKind === "albums" ? artistText || String(item.subtitle || "")
            : String(item.subtitle && item.subtitle !== artistText ? item.subtitle : ""))
    readonly property real captionTop: copy.y
    readonly property url artworkSource: worldData && item ? worldData.artwork(item.cover) : ""
    readonly property var mixArtworkSources: (item.artworkCovers || []).slice(0, 3).map(function(cover) {
        return root.worldData ? root.worldData.artwork(cover) : "";
    })
    implicitHeight: art.height + (copy.height > 0 ? 14 + copy.height : 0) + 8
    accessibleName: item.title + (item.unavailable ? ", unavailable" : "")
        + (reason ? ". " + reason : "")
    onTriggered: if(worldData) worldData.activate(item)

    function scheduleExposureCheck() { exposureTimer.restart(); }
    function clipTo(bounds, clipItem) {
        var corner=clipItem.mapToItem(null,0,0);
        bounds.left=Math.max(bounds.left,corner.x);
        bounds.top=Math.max(bounds.top,corner.y);
        bounds.right=Math.min(bounds.right,corner.x+clipItem.width);
        bounds.bottom=Math.min(bounds.bottom,corner.y+clipItem.height);
    }
    function actuallyVisible() {
        if(!root.Window.window || !root.Window.window.visible
            || root.Window.window.visibility===Window.Minimized)return false;
        if(!homePageVisible || !root.visible || !pageFlick || !exposureRail
            || !pageFlick.visible || !exposureRail.visible || !recommendationRow
            || recommendationRow.recommendation !== true || !item || !item.ref)
            return false;
        var origin=root.mapToItem(null,0,0);
        var bounds={left:origin.x,top:origin.y,right:origin.x+root.width,bottom:origin.y+root.height};
        clipTo(bounds,pageFlick);
        clipTo(bounds,exposureRail);

        var ancestor=root.parent;
        while(ancestor) {
            if(ancestor.visible===false || Number(ancestor.opacity)<=0) return false;
            if(ancestor.clip) clipTo(bounds,ancestor);
            if(ancestor===pageFlick) break;
            ancestor=ancestor.parent;
        }
        return bounds.right>bounds.left && bounds.bottom>bounds.top;
    }
    function recordExposureIfVisible() {
        if(!feedbackAvailable || typeof worldData.recordVisibleRecommendation!=="function") return false;
        return worldData.recordVisibleRecommendation(recommendationRow,item,
            recommendationRow.profileGeneration,actuallyVisible());
    }
    function openRecommendationActions() {
        return feedbackAvailable && typeof worldData.requestRecommendationActions==="function"
            && worldData.requestRecommendationActions(item,recommendationRow);
    }
    function submitFeedback(action) {
        if(!quickFeedbackAvailable || ["like","hide"].indexOf(action)<0
            || Number(recommendationRow.profileGeneration)!==worldData.recommendationProfileGeneration()) return false;
        if(worldData.setRecommendationFeedback(item.ref,action,Number(recommendationRow.profileGeneration))) return true;
        feedbackError=worldData.persistenceError || qsTr("Could not save feedback. Try again.");
        return false;
    }

    Theme { id: theme }
    Loader {
        id: art
        objectName: root.objectName + "_cover"
        width: root.width
        height: status === Loader.Ready ? (item as Item).implicitHeight : width
        sourceComponent: root.presentationKind === "songs" ? songArt
            : root.presentationKind === "albums" ? albumArt
            : root.presentationKind === "artists" ? artistArt
            : root.presentationKind === "mixes" ? mixArt
            : root.presentationKind === "radio" ? radioArt : ordinaryArt
    }
    Component { id: songArt; VinylSongWidget {
        artworkSource: root.artworkSource; title: root.item.title; artist: root.artistText
        displayFont: theme.display; uiFont: theme.ui
    } }
    Component { id: albumArt; VinylAlbumWidget { artworkSource: root.artworkSource; title: root.item.title } }
    Component { id: artistArt; VinylArtistWidget {
        artworkSource: root.artworkSource; title: root.item.title; displayFont: theme.display
    } }
    Component { id: mixArt; VinylMixWidget { artworkSources: root.mixArtworkSources; title: root.item.title } }
    Component { id: radioArt; VinylRadioWidget { artworkSource: root.artworkSource; title: root.item.title } }
    Component { id: ordinaryArt; RoundedPosterImage {
        implicitHeight: width / (root.item.aspect || 1)
        radius: root.item.round ? width / 2 : 13
        sources: root.artworkSource ? [root.artworkSource] : []
        hovered: root.interactionActive
    } }
    Column {
        id: copy
        objectName: root.objectName + "_caption"
        y: art.height + 14
        width: parent.width
        spacing: 4
        Text {
            objectName: root.objectName + "_captionTitle"
            width: parent.width
            height: 20
            text: root.captionTitle
            textFormat: Text.PlainText
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 15
            font.weight: Font.DemiBold
            maximumLineCount: 1
            elide: Text.ElideRight
        }
        Text {
            objectName: root.objectName + "_captionDetail"
            width: parent.width
            height: 18
            text: root.captionDetail
            textFormat: Text.PlainText
            color: root.reason ? theme.gold : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 12
            maximumLineCount: 1
            elide: Text.ElideRight
            lineHeight: 1.25
        }
        Text {
            width: parent.width
            visible: root.feedbackError.length > 0
            text: root.feedbackError
            textFormat: Text.PlainText
            color: theme.ink
            font.family: theme.ui; font.pixelSize: 12
            wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
        }
    }

    // The existing recommendation-options destination must be reachable by tap on touch.
    // Desktop retains the original hover feedback affordances unchanged.
    KeyboardAction {
        id: recommendationOptions
        objectName: "vinylRecommendationOptions_" + String(root.item.id || "item")
        visible: Qt.platform.os === "android" && root.feedbackAvailable
        enabled: root.enabled && visible
        x: Math.max(0, art.width - width - 8)
        y: Math.max(0, art.height - height - 8)
        z: 3
        width: 36; height: 36
        focusOnPointer: false
        tooltipText: qsTr("Feedback options")
        androidTooltipPolicy: "longPress"
        accessibleName: qsTr("Feedback options")
        onTriggered: root.openRecommendationActions()
        Rectangle {
            anchors.fill: parent
            radius: 18
            color: Qt.rgba(0.025, 0.025, 0.035, 0.76)
            border.width: 1
            border.color: theme.edge
        }
        Text {
            anchors.centerIn: parent
            text: "⋯"
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 20
        }
    }

    VinylFeedbackBadge {
        id: likeBadge
        objectName: "vinylRecommendationLike_" + String(root.item.id || "item")
        x: 9; y: 9
        z: 2
        visible: root.quickFeedbackAvailable
        enabled: visible && root.enabled
        revealed: root.feedbackRevealed
        symbol: "check"
        accessibleName: qsTr("More like %1").arg(root.item.title)
        onTriggered: root.submitFeedback("like")
    }
    VinylFeedbackBadge {
        id: hideBadge
        objectName: "vinylRecommendationHide_" + String(root.item.id || "item")
        x: art.width-width-9; y: 9
        z: 2
        visible: root.quickFeedbackAvailable
        enabled: visible && root.enabled
        revealed: root.feedbackRevealed
        symbol: "cancel"
        accessibleName: qsTr("Hide %1").arg(root.item.title)
        onTriggered: root.submitFeedback("hide")
    }

    Timer {
        id: exposureTimer
        interval: 120
        repeat: false
        onTriggered: root.recordExposureIfVisible()
    }
    Connections {
        target: root.worldData ? root.worldData.session : null
        ignoreUnknownSignals: true
        function onProfileGenerationChanged() { root.feedbackError=""; root.scheduleExposureCheck() }
    }
    onVisibleChanged: scheduleExposureCheck()
    onXChanged: scheduleExposureCheck()
    onYChanged: scheduleExposureCheck()
    onWidthChanged: scheduleExposureCheck()
    onHeightChanged: scheduleExposureCheck()
    onItemChanged: {feedbackError="";scheduleExposureCheck();}
    onRecommendationRowChanged: scheduleExposureCheck()
    onHomePageVisibleChanged: scheduleExposureCheck()
    onExposureRevisionChanged: scheduleExposureCheck()
    Component.onCompleted: scheduleExposureCheck()
}
