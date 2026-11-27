import QtQuick
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
    property bool compactCaption: false
    property bool wide: false
    property var artistRadioAction: null
    property var albumPlayAction: null
    property bool keyboardFocused: activeFocus
    property bool reducedMotion: !!(Window.window && Window.window["reducedMotion"] === true)
    readonly property bool albumPlayAvailable: presentationKind === "albums" && albumPlayAction !== null
    readonly property bool artistRadioAvailable: presentationKind === "artists" && artistRadioAction !== null
    readonly property bool pinAvailable: worldData && typeof worldData.pinCrateItem==="function"
        && typeof worldData.isCratePinned==="function" && !!item.id
        && ["songs","albums","artists","mixes","radio"].indexOf(presentationKind)>=0
    readonly property bool pinned: pinAvailable ? worldData.isCratePinned(item) : item.pinned === true
    readonly property bool feedbackAvailable: !!recommendationRow
        && (recommendationRow.recommendation === true
            || ((recommendationRow.key === "taste-artists" || recommendationRow.key === "saved-artists")
                && recommendationRow.kind === "artists"))
        && !!item && !!item.ref
        && worldData && typeof worldData.setRecommendationFeedback === "function"
        && typeof worldData.recommendationProfileGeneration === "function"
        && typeof worldData.recommendationArtistRef === "function"
    readonly property bool quickFeedbackAvailable: feedbackAvailable && recommendationRow.recommendation === true
    readonly property bool feedbackRevealed: quickFeedbackAvailable && (engaged
        || (likeBadge.item && (likeBadge.item.hovered || likeBadge.item.activeFocus))
        || (hideBadge.item && (hideBadge.item.hovered || hideBadge.item.activeFocus)))
    readonly property bool engaged: hovered || keyboardFocused
    property bool hintDismissed: false
    onEngagedChanged: if (!engaged) hintDismissed = false
    Keys.onEscapePressed: event => {
        if (!hint.active) return;
        hintDismissed = true;
        event.accepted = true;
    }

    enabled: root.item.available !== false && root.item.unavailable !== true
    showFocusFrame: false
    spaceActivates: albumPlayAvailable || artistRadioAvailable
    readonly property string reason: item.reason || ""
    readonly property string presentationKind: Presentation.kind(item)
    readonly property string artistText: Presentation.artist(item)
    readonly property string secondaryText: Presentation.secondary(item, artistText)
        + (feedbackError ? "\n" + feedbackError : "")
    readonly property string albumText: {
        var record = item.song && item.song.album || item.record || {};
        var name = String(item.albumTitle || record.title || "");
        return name === String(item.title || "") ? "" : name;
    }
    readonly property string songCredit: !albumText ? artistText
        : artistText ? qsTr("%1 · from %2").arg(artistText).arg(albumText)
        : qsTr("From %1").arg(albumText)
    readonly property bool resumeCaption: compactCaption && item.resumeTrack > 0 && item.totalTracks >= item.resumeTrack
    readonly property bool opensPage: presentationKind === "albums" || presentationKind === "artists"
    readonly property bool playing: presentationKind === "songs" && worldData
        && Presentation.isCurrentSong(item, worldData.session)
    readonly property string captionTitle: String(item.title || "")
    readonly property string captionDetail: item.unavailable ? qsTr("Unavailable")
        : resumeCaption ? reason
        : presentationKind === "artists" ? (compactCaption && reason || qsTr("Artist"))
        : presentationKind === "mixes" ? String(item.subtitle || qsTr("Made for you"))
        : presentationKind === "radio" ? (wide
            ? (item.subtitle && item.subtitle !== "Radio" ? String(item.subtitle) : qsTr("Keeps going"))
            : qsTr("Personal radio"))
        : presentationKind === "albums" ? Presentation.albumDetail(item, artistText)
        : compactCaption ? songCredit : artistText || (presentationKind === "songs" ? "" : String(item.subtitle || ""))
    readonly property real captionTop: copy.y
    readonly property url artworkSource: worldData ? worldData.artwork(item.cover) : ""
    readonly property var mixArtworkSources: presentationKind === "mixes" && worldData
        ? (item.artworkCovers || []).slice(0, wide ? 3 : 4).map(cover => worldData.artwork(cover)) : []
    implicitWidth: wide ? 260 : 196
    implicitHeight: wide ? 124 : width + (compactCaption ? 42 : 55)
    accessibleName: captionTitle + (captionDetail ? ". " + captionDetail : "")
    accessibleDescription: secondaryText === captionDetail ? "" : secondaryText
    onTriggered: if(worldData) worldData.activate(item)

    function scheduleExposureCheck() {
        if (exposure.item) exposure.item.restart();
    }
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
    Rectangle {
        id: tile
        objectName: root.objectName + "_tile"
        anchors.fill: parent
        radius: 15
        color: root.hovered ? theme.glassHi : theme.glassTint
        border.width: 1
        border.color: root.keyboardFocused ? theme.gold : root.hovered ? theme.edgeHi : theme.edge
        transformOrigin: root.transformOrigin
        property real motionScale: root.engaged ? 1.015 : 1
        scale: root.reducedMotion ? 1 : motionScale
        Behavior on motionScale {
            enabled: !root.reducedMotion
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }

        readonly property real artInset: root.wide ? 9 : root.compactCaption ? 4 : 8
        Loader {
            id: art
            width: root.wide ? 106 : Math.max(0, tile.width - 2 * tile.artInset)
            height: width
            sourceComponent: root.wide ? listeningFace : squareArt
        }
        Component {
            id: squareArt
            VinylTileArt {
                objectName: root.objectName + "_cover"
                x: tile.artInset; y: x
                title: root.captionTitle
                artworkSource: root.artworkSource
                artworkSources: root.mixArtworkSources
                circular: root.presentationKind === "artists" || root.item.round === true
                edgeColor: root.hovered ? theme.edgeHi : theme.edge
            }
        }
        Component {
            id: listeningFace
            VinylListeningFace {
                objectName: root.objectName + "_cover"
                x: tile.artInset; y: x
                cassette: root.presentationKind === "mixes"
                title: root.captionTitle
                portrait: root.artworkSource
                covers: root.mixArtworkSources
                displayFont: theme.display
            }
        }
        Item {
            id: copy
            objectName: root.objectName + "_caption"
            x: root.wide ? tile.artInset + art.width + 12 : root.compactCaption ? 7 : 10
            y: tile.artInset + (root.wide ? 6 : art.height + (root.compactCaption ? 4 : 7))
            width: tile.width - x - (root.wide ? 9 : x)
            height: root.wide ? tile.height - y - 12 : root.compactCaption ? 38 : 48

            Loader {
                active: root.wide
                width: parent.width
                height: 12
                sourceComponent: Text {
                    text: root.presentationKind === "mixes" ? qsTr("MIX") : qsTr("((•)) STATION")
                    textFormat: Text.PlainText
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 10
                    font.letterSpacing: 1.8
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Text {
                objectName: root.objectName + "_captionTitle"
                y: root.wide ? 20 : 0
                width: parent.width
                height: root.compactCaption ? 18 : 22
                text: root.captionTitle
                textFormat: Text.PlainText
                color: root.playing ? theme.gold : theme.ink
                font.family: theme.display
                font.pixelSize: root.compactCaption ? 15 : 17
                maximumLineCount: 1
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                objectName: root.objectName + "_captionDetail"
                y: root.wide ? 46 : root.compactCaption ? 18 : 24
                width: Math.max(0, parent.width - (!root.wide && hint.active ? hint.width + 6 : 0))
                height: root.wide ? 14 : root.compactCaption ? 20 : 24
                text: root.feedbackError || ((root.pinned ? "✓  " : "")
                    + (!root.wide && root.presentationKind === "radio" && !root.item.unavailable ? "((•))  " : "")
                    + root.captionDetail)
                textFormat: Text.PlainText
                color: root.resumeCaption ? theme.gold : theme.inkDim
                font.family: theme.ui
                font.pixelSize: 12
                maximumLineCount: 1
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Loader {
                id: hint
                objectName: "vinylTileHint"
                anchors.right: parent.right
                anchors.rightMargin: root.wide ? 3 : 0
                anchors.bottom: parent.bottom
                width: root.compactCaption ? 20 : 24
                height: width
                active: root.enabled && root.engaged && !root.playing && !root.hintDismissed
                sourceComponent: Rectangle {
                    radius: width / 2
                    color: "#3b404a"
                    Text {
                        anchors.centerIn: parent
                        visible: !root.wide
                        text: root.opensPage ? "→" : "▶"
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: root.opensPage ? 15 : 12
                    }
                    Image {
                        anchors.centerIn: parent
                        width: 15; height: 15
                        visible: root.wide
                        source: root.wide ? "../../assets/icons/play.svg" : ""
                    }
                    NumberAnimation on opacity {
                        from: 0; to: 1
                        duration: 180
                        easing.type: Easing.OutCubic
                        running: !root.reducedMotion
                    }
                }
            }
        }
        Loader {
            objectName: "vinylPlayingRail"
            x: copy.x
            y: tile.artInset + art.height + 2
            width: copy.width
            height: 2
            active: root.playing
            sourceComponent: Rectangle {
                radius: 1
                color: "#52555e"
                Rectangle {
                    width: parent.width * 0.37
                    height: parent.height
                    radius: 1
                    color: theme.gold
                }
            }
        }

        Loader {
            x: tile.artInset + (art.width - width) / 2
            y: tile.artInset + (art.height - height) / 2
            z: 2
            active: root.albumPlayAvailable || root.artistRadioAvailable
            sourceComponent: VinylFeedbackBadge {
                objectName: root.objectName + (root.albumPlayAvailable ? "_albumPlay" : "_artistRadio")
                enabled: root.enabled
                revealed: root.engaged || hovered || activeFocus
                symbol: "play"
                accessibleName: !root.albumPlayAvailable ? qsTr("Artist radio")
                    : root.item.resumeTrack > 0 ? qsTr("Resume album") : qsTr("Play album")
                spaceActivates: true
                onTriggered: {
                    if (root.albumPlayAvailable) root.albumPlayAction(root.item);
                    else root.artistRadioAction(root.item);
                }
            }
        }
        Loader {
            id: likeBadge
            x: tile.artInset + 1; y: tile.artInset + 1; z: 2
            active: root.quickFeedbackAvailable
            sourceComponent: VinylFeedbackBadge {
                objectName: "vinylRecommendationLike_" + String(root.item.id || "item")
                enabled: root.enabled
                revealed: root.feedbackRevealed
                symbol: "check"
                accessibleName: qsTr("More like %1").arg(root.item.title)
                onTriggered: root.submitFeedback("like")
            }
        }
        Loader {
            x: tile.artInset + art.width - width
            y: tile.artInset + art.height - height
            z: 2
            active: root.pinAvailable
            sourceComponent: VinylFeedbackBadge {
                objectName: "vinylPin_" + String(root.item.id || "item")
                enabled: root.enabled || root.pinned
                // The idle check lives in the caption to keep even a pinned
                // four-cover mix within the paint budget. Retain the hit target.
                pointerEnabled: revealed || root.pinned
                revealed: root.engaged || hovered || activeFocus
                symbol: root.pinned ? "check" : "playlistAdd"
                accessibleName: (root.pinned ? qsTr("Unpin %1") : qsTr("Pin %1")).arg(root.item.title)
                onTriggered: root.worldData.pinCrateItem(root.item, !root.pinned)
            }
        }
        Loader {
            id: hideBadge
            x: tile.artInset + art.width - width - 1; y: tile.artInset + 1; z: 2
            active: root.quickFeedbackAvailable
            sourceComponent: VinylFeedbackBadge {
                objectName: "vinylRecommendationHide_" + String(root.item.id || "item")
                enabled: root.enabled
                revealed: root.feedbackRevealed
                symbol: "cancel"
                accessibleName: qsTr("Hide %1").arg(root.item.title)
                onTriggered: root.submitFeedback("hide")
            }
        }
    }

    Loader {
        id: exposure
        active: root.quickFeedbackAvailable
        onLoaded: root.scheduleExposureCheck()
        sourceComponent: Timer {
            interval: 120
            onTriggered: root.recordExposureIfVisible()
            property Connections sessionChanges: Connections {
                target: root.worldData ? root.worldData.session || null : null
                ignoreUnknownSignals: true
                function onProfileGenerationChanged() {
                    root.feedbackError = "";
                    root.scheduleExposureCheck();
                }
            }
        }
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
