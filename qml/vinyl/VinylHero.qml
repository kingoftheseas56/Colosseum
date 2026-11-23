import QtQuick
import QtQuick.Effects
import ".."
import "VinylCardPresentation.js" as Presentation

// The caller supplies complete chart/pool groups and owns all actions and replacement.
Item {
    id: hero
    objectName: "vinylHero"
    property var slides: []
    property alias slideIndex: carousel.index
    readonly property int slideCount: carousel.slides.length
    readonly property var currentSlide: carouselData[slideIndex] || ({})
    readonly property string slideKind: String(currentSlide.kind || "")
    readonly property string itemId: String(currentSlide.item && currentSlide.item.id || "")
    readonly property bool hovered: hover.hovered
    property var carouselSlides: []
    property string selectedKind: ""
    property bool replacingSlides: false
    property int modelRevision: 0
    readonly property bool narrow: width < 760
    readonly property bool compact: !narrow && height<160
    property bool componentReady: false
    implicitHeight: narrow ? (width < 480 ? 440 : 390) : 300
    signal playRequested(var item)
    signal radioRequested(var item)
    signal openRequested(var item)
    signal saveRequested(var item)
    signal hideRequested(var item)

    function artwork(item) { return String(item && (item.cover || item.image) || ""); }
    function genre(item) { var tag=item.genre || (item.genres || item.tags || [])[0] || ""; return String(tag.name || tag); }
    function meta(slide) {
        var item=slide.item || {}, kind=Presentation.kind(item), record=item.record || (item.song || {}).album || {};
        var artist=Presentation.artist(item), year=String(item.year || record.year || (item.releaseDate || record.releaseDate || "").slice(0,4));
        var album=String(item.albumTitle || (kind==="songs" ? record.title : "") || "");
        var count=item.trackCount || item.songCount || (record.tracks || []).length;
        return (kind==="artists" ? [genre(item)] : kind==="songs" ? [artist,album ? qsTr("from %1").arg(album) : ""]
            : [artist,year,slide.kind==="forYou" ? qsTr("album") : count ? qsTr("%1 songs").arg(count) : ""]).filter(Boolean).join(" · ");
    }
    function story(slide) {
        if(slide.kind==="topAlbum")return qsTr("The most-played album across the charts this week. Start it from the top.");
        if(slide.kind==="topSong")return qsTr("Top of the global song chart. Play it, or let it start a station that keeps going.");
        if(slide.kind==="topArtist")return qsTr("The most-listened artist across the charts this week. Start the radio, or dive into the records.");
        return "";
    }
    function kindLabel(item) {
        var kind = Presentation.kind(item);
        return kind === "songs" ? qsTr("Song") : kind === "albums" ? qsTr("Album")
            : kind === "artists" ? qsTr("Artist") : kind === "mixes" ? qsTr("Mix")
            : kind === "radio" ? qsTr("Station") : "";
    }
    function play(item) {
        if (Presentation.kind(item) === "artists") radioRequested(item);
        else playRequested(item);
    }
    function actions(slide) {
        if (slide.kind === "topAlbum") return [
            {key:"Play", label:qsTr("Play album")}, {key:"Open", label:qsTr("Open album")}, {key:"Save", label:qsTr("Save")}];
        if (slide.kind === "topSong") return [
            {key:"Play", label:qsTr("Play song")}, {key:"Radio", label:qsTr("Start radio")}];
        if (slide.kind === "topArtist") return [
            {key:"Radio", label:qsTr("Artist radio")}, {key:"Open", label:qsTr("Open artist")}];
        return [{key:"Play", label:qsTr("Play")}, {key:"Open", label:qsTr("Open")}, {key:"NotForMe", label:qsTr("Not for me")}];
    }
    function act(key, item) {
        if (key === "Play") play(item);
        else if (key === "Radio") radioRequested(item);
        else if (key === "Open") openRequested(item);
        else if (key === "Save") saveRequested(item);
        else if (key === "NotForMe") hideRequested(item);
    }
    property string slidesSignature: ""
    // Full slide content. Delegates read it by index, so a republish with the
    // same number of slides updates them in place: swapping the SwipeView's
    // Repeater model can leave slides stacked over each other after a hide or
    // resize (blank or half-shifted hero).
    property var carouselData: []
    function pickIndex(list, id, kind, fallback) {
        var i;
        for (i = 0; id && i < list.length; ++i) if (list[i].id === id) return i;
        for (i = 0; kind && i < list.length; ++i) if (list[i].kind === kind) return i;
        return Math.max(0, Math.min(fallback, list.length - 1));
    }
    function syncSlides() {
        var incoming = slides.map(function(slide) {
            return Object.assign({id:String(slide.item && slide.item.id || "")}, slide);
        });
        // Live Home republishes on every pool rebuild; most carry identical slides.
        var signature = JSON.stringify(incoming);
        if (!replacingSlides && signature === slidesSignature) return;
        slidesSignature = signature;
        var keepId = replacingSlides ? "" : itemId;
        var keepKind = replacingSlides ? selectedKind : slideKind;
        var keepIndex = Math.max(0, slideIndex);
        if (componentReady && !replacingSlides && incoming.length > 0
                && incoming.length === carouselSlides.length) {
            carouselData = incoming;
            // Keep the same item on screen, else the same group, else the same position.
            var same = pickIndex(incoming, keepId, keepKind, keepIndex);
            if (same !== slideIndex) carousel.index = same;
            selectedKind = slideKind;
            return;
        }
        selectedKind = keepKind;
        replacingSlides = true;
        var revision = ++modelRevision;
        carouselData = incoming;
        carouselSlides = incoming.map(function(slide) { return {id:slide.id, kind:slide.kind}; });
        Qt.callLater(function() {
            if (!hero || revision !== hero.modelRevision) return;
            carousel.index = hero.slideCount ? hero.pickIndex(incoming, keepId, hero.selectedKind, keepIndex) : -1;
            hero.replacingSlides = false;
            hero.selectedKind = hero.slideKind;
        });
    }
    onSlidesChanged: syncSlides()
    onSlideIndexChanged: {
        if (!componentReady || replacingSlides) return;
        selectedKind = slideKind;
        if (advance.running) advance.restart();
    }
    Component.onCompleted: { componentReady = true; syncSlides(); }
    Theme { id: theme }
    HoverHandler { id: hover }
    Timer {
        id: advance
        objectName: "vinylHeroTimer"
        interval: 7000
        repeat: true
        running: hero.visible && hero.slideCount > 1 && !hero.hovered
        onTriggered: carousel.index = (carousel.index + 1) % hero.slideCount
    }
    FeaturedCarousel {
        id: carousel
        anchors.fill: parent
        slides: hero.carouselSlides
        slideDelegate: vinylSlide
        automationId: "vinylHeroCarousel"
        dotAutomationPrefix: "vinylHeroDot"
        dotsBottomMargin: hero.narrow ? 4 : 6.5
        dotsRightMargin: 16
        dotsLeftMargin: hero.narrow ? -1 : 19
        kicker: qsTr("Vinyl highlights")
        onPrimaryClicked: index => hero.play(slides[index].item)
    }
    Component {
        id: vinylSlide
        Item {
            id: face
            required property var modelData
            required property int index
            readonly property bool current: index === hero.slideIndex
            readonly property var slideData: hero.carouselData[index] || modelData
            readonly property var item: slideData.item || ({})
            readonly property real edgeMargin: 30
            readonly property real mainHeight: hero.narrow ? (hero.width < 480 ? 210 : 180) : height
            readonly property real artSize: hero.narrow ? Math.min(132, width * 0.27) : Math.min(236, height - 64)
            readonly property real sideWidth: hero.narrow ? width - 32 : Math.min(300, width * 0.28)
            readonly property real sideRowHeight: hero.narrow ? 30 : Math.max(30, Math.min(48, (height - 48 - 52) / 4))
            readonly property bool hasArt: hero.artwork(face.item).length > 0
            objectName: current ? "vinylHeroSlide" : "vinylHeroHiddenSlide_" + index
            Rectangle {
                anchors.fill: parent; radius: hero.narrow ? 18 : 22
                border.color: theme.edge
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#1c2536" }
                    GradientStop { position: 0.5; color: "#2a2320" }
                    GradientStop { position: 1.0; color: "#0d151d" }
                }
            }
            Image {
                id: ownArt
                anchors.fill: parent
                source: hero.artwork(face.item)
                sourceSize.width: Math.min(1200, Math.ceil(width * 2))
                sourceSize.height: Math.min(800, Math.ceil(height * 2))
                asynchronous: true; cache: true; fillMode: Image.PreserveAspectCrop
                visible: false
            }
            Item {
                id: mask
                anchors.fill: parent; visible: false; layer.enabled: true
                Rectangle { anchors.fill: parent; radius: hero.narrow ? 18 : 22; color: "white" }
            }
            MultiEffect {
                anchors.fill: parent
                source: ownArt
                visible: ownArt.status === Image.Ready
                blurEnabled: true; blurMax: 38; blur: 1
                brightness: -0.45; saturation: 0.3
                autoPaddingEnabled: false
                maskEnabled: true; maskSource: mask
            }
            Rectangle {
                anchors.fill: parent; radius: hero.narrow ? 18 : 22
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: ownArt.status === Image.Ready ? "#4005060a" : "#00050609" }
                    GradientStop { position: 0.6; color: ownArt.status === Image.Ready ? "#8c05060a" : "#2005060a" }
                    GradientStop { position: 1.0; color: ownArt.status === Image.Ready ? "#bf05060a" : "#4005060a" }
                }
            }
            Rectangle {
                id: coverPlaceholder
                x: cover.x; y: cover.y; width: cover.width; height: cover.height
                radius: cover.radius
                visible: !face.hasArt
                border.width: 1; border.color: "#30ffffff"
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#2e3446" }
                    GradientStop { position: 1.0; color: "#14171f" }
                }
                Text {
                    anchors.centerIn: parent
                    text: String(face.item.title || "").charAt(0).toUpperCase() || "♪"
                    textFormat: Text.PlainText; color: "#55ffffff"
                    font.family: theme.display; font.pixelSize: Math.round(parent.width * 0.38)
                }
            }
            RectangularShadow {
                x: cover.x; y: cover.y; width: cover.width; height: cover.height
                radius: cover.radius; blur: 50; offset: Qt.vector2d(0,24); color: "#8c000000"
            }
            RoundedPosterImage {
                id: cover
                x: hero.narrow ? 16 : face.edgeMargin; y: hero.narrow ? 16 : (face.height-height)/2
                width: face.artSize; height: width
                sources: face.hasArt ? [hero.artwork(face.item)] : []
                radius: Presentation.kind(face.item) === "artists" ? width / 2 : (hero.narrow ? 8 : 14)
            }
            Column {
                id: copy
                x: cover.x + cover.width + (hero.narrow ? 16 : face.edgeMargin)
                y: hero.narrow ? 16 : Math.max(12, (face.height - height) / 2)
                width: Math.max(0,hero.narrow ? face.width - x - 16 : sidePanel.x - x - face.edgeMargin)
                spacing: hero.narrow ? 5 : 0
                Text {
                    width: parent.width; height: hero.narrow ? 15 : 16
                    text: face.slideData.kind==="forYou" ? qsTr("For you · picked today") : String(face.slideData.kicker || "")
                    textFormat: Text.PlainText; color: theme.gold
                    font.family: theme.ui; font.pixelSize: hero.narrow ? 10 : 11; font.weight: 650
                    font.letterSpacing: hero.narrow ? 1 : 2.6
                    font.capitalization: hero.narrow ? Font.MixedCase : Font.AllUppercase
                    elide: Text.ElideRight
                }
                Item { width:1; height:10; visible:!hero.narrow }
                Text {
                    width: parent.width
                    height: hero.narrow ? (hero.width < 480 ? 56 : 38) : 46
                    text: String(face.item.title || "")
                    textFormat: Text.PlainText; color: theme.ink
                    font.family: theme.display; font.pixelSize: hero.narrow ? 25 : 46; font.weight: Font.Normal; lineHeight:1
                    wrapMode: Text.WordWrap; maximumLineCount: hero.narrow && hero.width < 480 ? 2 : 1; elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                Item { width:1; height:6; visible:!hero.narrow && metaLine.visible }
                Text {
                    id: metaLine
                    width: parent.width; height: hero.narrow ? 17 : 18
                    text: hero.meta(face.slideData)
                    visible: text.length>0
                    textFormat: Text.PlainText; color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: hero.narrow ? 12 : 13; elide: Text.ElideRight
                }
                Item { width:1; height:12; visible:!hero.narrow && storyLine.visible }
                Text {
                    id: storyLine
                    objectName: face.current ? "vinylHeroStory" : "vinylHeroHiddenStory_" + face.index
                    width: hero.narrow ? parent.width : Math.min(parent.width, 520)
                    visible: text.length > 0
                    text: hero.story(face.slideData)
                    textFormat: Text.PlainText; color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: hero.narrow ? 12 : 13
                    wrapMode: hero.narrow ? Text.NoWrap : Text.WordWrap
                    maximumLineCount: hero.narrow ? 1 : 2; lineHeight: 1.5
                    elide: Text.ElideRight
                }
                Item { width: 1; height: 18; visible: !hero.narrow }
                Flow {
                    width: parent.width; spacing: hero.narrow ? 8 : 10
                    Repeater {
                        model: hero.actions(face.slideData)
                        delegate: KeyboardAction {
                            id: action
                            required property var modelData
                            required property int index
                            objectName: face.current ? "vinylHero" + modelData.key : "vinylHeroHiddenAction_" + face.index + "_" + modelData.key
                            width: label.implicitWidth + (hero.narrow ? 22 : 36); height: hero.narrow ? 30 : 40
                            pointerEnabled: true; focusPrimary: index === 0
                            accessibleName: modelData.label
                            focusRadius: hero.narrow ? 7 : 11
                            onTriggered: hero.act(modelData.key, face.item)
                            Rectangle {
                                anchors.fill: parent; radius: hero.narrow ? 7 : 11
                                color: action.index === 0 ? theme.gold : "#14ffffff"
                                border.width: action.index===0 ? 0 : 1; border.color: theme.edge
                            }
                            Text {
                                id: label
                                anchors.centerIn: parent
                                text: (action.index===0 ? "▶ " : "")+action.modelData.label; textFormat: Text.PlainText
                                font.family: theme.ui; font.pixelSize: hero.narrow ? 12 : 13; font.weight: 650
                                color: action.index === 0 ? "#17120a" : theme.ink
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: sidePanel
                visible: !hero.narrow
                x: face.width - face.edgeMargin - face.sideWidth; y: 24
                width: face.sideWidth; height: face.height - 48
                radius: 16; color: "#47000000"; border.width: 1; border.color: "#1affffff"
            }
            Column {
                id: side
                x: hero.narrow ? 16 : sidePanel.x + 16
                y: hero.narrow ? face.mainHeight + 12 : sidePanel.y + 16
                width: hero.narrow ? face.sideWidth : face.sideWidth - 32
                spacing: hero.narrow ? 4 : 2
                Text {
                    width: parent.width; height: hero.narrow ? 16 : 20
                    text: face.slideData.kind === "topAlbum" ? qsTr("Also charting")
                        : face.slideData.kind === "topSong" ? qsTr("Top songs")
                        : face.slideData.kind === "topArtist" ? qsTr("Top artists") : qsTr("Also for you")
                    textFormat: Text.PlainText; color: hero.narrow ? theme.gold : theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold
                    font.letterSpacing: hero.narrow ? 0 : 2
                    font.capitalization: hero.narrow ? Font.MixedCase : Font.AllUppercase
                }
                Repeater {
                    model: (face.slideData.sideItems || []).slice(0, 4)
                    delegate: KeyboardAction {
                        id: sideRow
                        required property var modelData
                        required property int index
                        readonly property var item: modelData
                        readonly property string kindLabel: hero.kindLabel(item)
                        readonly property real coverSize: hero.narrow ? 28 : Math.min(34, face.sideRowHeight - 8)
                        readonly property real textX: hero.narrow ? 38 : 6+(face.slideData.kind==="forYou" ? 0 : 33)+coverSize+11
                        objectName: face.current ? "vinylHeroSideRow_" + index : "vinylHeroHiddenSideRow_" + face.index + "_" + index
                        width: side.width; height: face.sideRowHeight
                        pointerEnabled: true; focusRadius: hero.narrow ? 5 : 9
                        accessibleName: String(item.title || "")
                        onTriggered: hero.openRequested(item)
                        Text {
                            visible: !hero.narrow && face.slideData.kind!=="forYou"
                            x: 6; width: 22; anchors.verticalCenter: parent.verticalCenter
                            text: sideRow.item.rank ? String(sideRow.item.rank) : ""
                            textFormat: Text.PlainText; color: theme.inkDimmer
                            font.family: theme.display; font.pixelSize: 17
                        }
                        RoundedPosterImage {
                            x: hero.narrow ? 0 : face.slideData.kind==="forYou" ? 6 : 39; y: hero.narrow ? 1 : (parent.height - height) / 2
                            width: sideRow.coverSize; height: width
                            sources: hero.artwork(sideRow.item) ? [hero.artwork(sideRow.item)] : []
                            radius: Presentation.kind(sideRow.item) === "artists" ? width / 2 : (hero.narrow ? 3 : 6)
                        }
                        Text {
                            x: sideRow.textX; width: parent.width - x
                            y: hero.narrow ? 0 : parent.height / 2 - 16; height: 15
                            text: (hero.narrow && sideRow.item.rank ? "#" + sideRow.item.rank + " · " : "") + String(sideRow.item.title || "")
                            textFormat: Text.PlainText; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold; elide: Text.ElideRight
                        }
                        Text {
                            objectName: face.current ? "vinylHeroSideSubtitle_" + sideRow.index : "vinylHeroHiddenSideSubtitle_" + face.index + "_" + sideRow.index
                            x: sideRow.textX; y: hero.narrow ? 16 : parent.height / 2 + 1; width: parent.width - x; height: 14
                            text: face.slideData.kind === "forYou" ? [sideRow.kindLabel,Presentation.kind(sideRow.item)==="artists"
                                    ? hero.genre(sideRow.item) : Presentation.artist(sideRow.item)].filter(Boolean).join(" · ")
                                : face.slideData.kind==="topArtist" ? hero.genre(sideRow.item) : Presentation.artist(sideRow.item)
                            textFormat: Text.PlainText; color: hero.narrow ? theme.inkDim : theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 10; elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
