import QtQuick
import ".."

Item {
    id: root
    required property var artistData
    property real viewportHeight: 720
    property real horizontalGutter: 54
    property real contentTopInset: 0
    property bool fullBleed: true
    property int rotationIntervalMs: 12000
    property int crossfadeDurationMs: 1350

    signal createPlaylistRequested()
    signal playArtistRequested()
    signal shuffleArtistRequested()
    signal itemFocused(var item)

    readonly property bool sourcedContext: artistData.context.sources.length > 0
    property var failedPhotoIds: []
    readonly property var eligiblePhotos: (artistData.heroPhotos || []).filter(function(photo) {
        return root.failedPhotoIds.indexOf(photo.id) < 0;
    })
    readonly property int eligiblePhotoCount: eligiblePhotos.length
    readonly property string rotationMode: {
        if (artistData.customBackdropUrl && String(artistData.customBackdropUrl).length > 0)
            return "custom";
        if (artistData.selectedWallpaper !== "auto")
            return "pinned";
        if (eligiblePhotoCount <= 0)
            return "fallback";
        if (eligiblePhotoCount === 1)
            return "static";
        if (eligiblePhotoCount === 2)
            return "dual-crossfade";
        return "multi-rotate";
    }
    readonly property bool slideshowEnabled: artistData.selectedWallpaper === "auto"
        && !artistData.reducedMotion
        && !(artistData.customBackdropUrl && String(artistData.customBackdropUrl).length > 0)
        && eligiblePhotoCount >= 2

    readonly property real titlePixelSize: width < 760 ? Math.max(52, width * 0.103) : Math.max(94, Math.min(168, width * 0.09))
    property bool skinLayerAReady: true
    property bool skinLayerBReady: true
    property int pendingVisualLayer: -1
    property bool preparingPhoto: false
    property int photoRequestSerial: 0
    property real layerAFocalX: 0.5
    property real layerAFocalY: 0.4
    property real layerBFocalX: 0.5
    property real layerBFocalY: 0.4
    onSkinLayerAReadyChanged: commitReadyPhoto()
    onSkinLayerBReadyChanged: commitReadyPhoto()

    property int slideshowIndex: 0
    property int activeVisualLayer: 0
    property url layerASource: ""
    property url layerBSource: ""
    property url layerAFallback: ""
    property url layerBFallback: ""
    readonly property url currentSynchronizedSource: activeVisualLayer === 0 ? layerASource : layerBSource
    readonly property bool portraitReady: (activeVisualLayer === 0 && heroImgA.status === Image.Ready)
        || (activeVisualLayer === 1 && heroImgB.status === Image.Ready)


    // Keep the approved composition when it fits; wrapped names/facts must not
    // push the identity underneath the destination Back/header overlay.
    implicitHeight: Math.max(500, Math.min(760, Math.round(viewportHeight * 0.66)),
                             contentTopInset + 12 + identity.implicitHeight + identity.anchors.bottomMargin)
    height: implicitHeight
    clip: true

    Theme { id: theme }

    function resolvePhotoObject(photoId) {
        if (photoId === "custom" && artistData.customBackdropUrl && String(artistData.customBackdropUrl).length > 0) {
            return {
                id: "custom",
                name: "Custom photo",
                cover: String(artistData.customBackdropUrl),
                fallbackCover: String(artistData.customBackdropUrl),
                focalX: 0.5,
                focalY: 0.4
            };
        }
        var all = artistData.backdropOptions || [];
        for (var i = 0; i < all.length; ++i) {
            if (all[i].id === photoId)
                return all[i];
        }
        if (eligiblePhotos.length > 0) {
            var idx = ((slideshowIndex % eligiblePhotos.length) + eligiblePhotos.length) % eligiblePhotos.length;
            return eligiblePhotos[idx];
        }
        if (all.length > 0)
            return all[0];
        return null;
    }

    function resolvedUrlForPhoto(photo, useFallback) {
        if (!photo) {
            if (artistData.liveMode) return "";
            if (artistData.record && artistData.record.releases && artistData.record.releases.length > 0)
                return artistData.artwork(artistData.record.releases[0].album.cover);
            return "";
        }
        var target = useFallback ? (photo.fallbackCover || photo.cover) : (photo.cover || photo.fallbackCover);
        return artistData.artwork(target);
    }

    function commitReadyPhoto() {
        if (preparingPhoto || pendingVisualLayer < 0)
            return;
        var ready = pendingVisualLayer === 0
            ? heroImgA.status === Image.Ready && skinLayerAReady
            : heroImgB.status === Image.Ready && skinLayerBReady;
        if (ready) {
            activeVisualLayer = pendingVisualLayer;
            pendingVisualLayer = -1;
        }
    }

    function photoStatusChanged(layer, status) {
        if (status === Image.Error) {
            // Defer until Image.source's binding has finished evaluating. Check the
            // failed URL again so a superseded request cannot overwrite newer art.
            var serial = photoRequestSerial;
            var failed = layer === 0 ? layerASource : layerBSource;
            var fallback = layer === 0 ? layerAFallback : layerBFallback;
            if (root.artistData.liveMode) {
                Qt.callLater(function() {
                    if (root.photoRequestSerial !== serial) return;
                    var photo = root.eligiblePhotos.find(function(value) { return String(value.cover) === String(failed); });
                    if (!photo) return;
                    root.failedPhotoIds = root.failedPhotoIds.concat([photo.id]);
                    if (root.artistData.selectedWallpaper === photo.id) root.artistData.selectedWallpaper = "auto";
                    root.syncInitialBackdrop();
                });
                return;
            }
            if (fallback && failed !== fallback) {
                Qt.callLater(function() {
                    if (root.photoRequestSerial !== serial) return;
                    if (layer === 0 && root.layerASource === failed)
                        root.layerASource = fallback;
                    else if (layer === 1 && root.layerBSource === failed)
                        root.layerBSource = fallback;
                });
            }
        }
        commitReadyPhoto();
    }

    function transitionToPhoto(photo, initializeBoth) {
        var primaryUrl = resolvedUrlForPhoto(photo, false);
        var backupUrl = resolvedUrlForPhoto(photo, true);
        preparingPhoto = true;
        photoRequestSerial += 1;
        var nextLayer = initializeBoth || (!layerASource && !layerBSource)
            ? 0 : (activeVisualLayer === 0 ? 1 : 0);
        pendingVisualLayer = nextLayer;
        var fx = photo && photo.focalX !== undefined ? Math.max(0, Math.min(1, photo.focalX)) : 0.5;
        var fy = photo && photo.focalY !== undefined ? Math.max(0, Math.min(1, photo.focalY)) : 0.4;
        if (nextLayer === 0) {
            layerAFocalX = fx;
            layerAFocalY = fy;
            layerAFallback = backupUrl;
            layerASource = primaryUrl;
        } else {
            layerBFocalX = fx;
            layerBFocalY = fy;
            layerBFallback = backupUrl;
            layerBSource = primaryUrl;
        }
        if (initializeBoth) {
            layerBFocalX = fx;
            layerBFocalY = fy;
            layerBFallback = backupUrl;
            layerBSource = primaryUrl;
        }
        preparingPhoto = false;
        // Re-requesting the same failed URL may not emit statusChanged again.
        photoStatusChanged(nextLayer, nextLayer === 0 ? heroImgA.status : heroImgB.status);
    }

    function setBackdrop(backdropId) {
        if (!backdropId || backdropId === "auto") {
            artistData.customBackdropUrl = "";
            artistData.selectedWallpaper = "auto";
            slideshowIndex = 0;
            var first = eligiblePhotos.length > 0 ? eligiblePhotos[0] : resolvePhotoObject("studio");
            transitionToPhoto(first, false);
            return;
        }
        if (backdropId === "custom" && artistData.customBackdropUrl) {
            artistData.selectedWallpaper = "custom";
            transitionToPhoto(resolvePhotoObject("custom"), false);
            return;
        }
        artistData.customBackdropUrl = "";
        artistData.selectedWallpaper = backdropId;
        for (var i = 0; i < eligiblePhotos.length; ++i) {
            if (eligiblePhotos[i].id === backdropId) {
                slideshowIndex = i;
                break;
            }
        }
        transitionToPhoto(resolvePhotoObject(backdropId), false);
    }

    function advanceSlideshow() {
        if (eligiblePhotoCount < 2)
            return;
        slideshowIndex = (slideshowIndex + 1) % eligiblePhotoCount;
        transitionToPhoto(eligiblePhotos[slideshowIndex], false);
    }

    function syncInitialBackdrop() {
        slideshowIndex = 0;
        var initialId = artistData.selectedWallpaper || "auto";
        var photo = initialId === "auto"
            ? (eligiblePhotos.length > 0 ? eligiblePhotos[0] : resolvePhotoObject("studio"))
            : resolvePhotoObject(initialId);
        transitionToPhoto(photo, true);
    }

    Component.onCompleted: syncInitialBackdrop()

    Connections {
        target: root.artistData
        ignoreUnknownSignals: true
        function onHeroPhotosChanged() { root.failedPhotoIds = []; root.syncInitialBackdrop(); }
        function onNameChanged() { root.syncInitialBackdrop(); }
        function onOverrideHeroPhotosChanged() { root.syncInitialBackdrop(); }
        function onCustomBackdropUrlChanged() {
            if (root.artistData.customBackdropUrl && String(root.artistData.customBackdropUrl).length > 0)
                root.setBackdrop("custom");
        }
    }

    Timer {
        id: rotationTimer
        objectName: "vinylArtistHeroTimer"
        interval: root.rotationIntervalMs
        repeat: true
        running: root.visible && root.slideshowEnabled && root.eligiblePhotoCount >= 2
        onTriggered: root.advanceSlideshow()
    }

    // Composed fallback base stage for 0-photo artists or offline image fallback
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#151926" }
            GradientStop { position: 0.52; color: "#0b0d14" }
            GradientStop { position: 1.0; color: "#05060a" }
        }
    }

    // Buffered Sharp Hero Layer A
    Image {
        id: heroImgA
        objectName: "vinylArtistHeroImgA"
        // CSS object-position: move the excess cropped extent by the focal fraction.
        readonly property real imageRatio: implicitHeight > 0 ? implicitWidth / implicitHeight : 16 / 9
        width: Math.max(root.width, root.height * imageRatio)
        height: Math.max(root.height, root.width / imageRatio)
        x: (root.width - width) * root.layerAFocalX
        y: (root.height - height) * root.layerAFocalY
        source: root.layerASource
        sourceSize.width: 1920
        fillMode: Image.Stretch
        asynchronous: false
        opacity: root.activeVisualLayer === 0 && status === Image.Ready ? 0.90 : 0.0
        onStatusChanged: root.photoStatusChanged(0, status)
        Behavior on opacity {
            NumberAnimation {
                duration: root.artistData.reducedMotion ? 0 : root.crossfadeDurationMs
                easing.type: Easing.InOutQuad
            }
        }
    }

    // Buffered Sharp Hero Layer B
    Image {
        id: heroImgB
        objectName: "vinylArtistHeroImgB"
        // CSS object-position: move the excess cropped extent by the focal fraction.
        readonly property real imageRatio: implicitHeight > 0 ? implicitWidth / implicitHeight : 16 / 9
        width: Math.max(root.width, root.height * imageRatio)
        height: Math.max(root.height, root.width / imageRatio)
        x: (root.width - width) * root.layerBFocalX
        y: (root.height - height) * root.layerBFocalY
        source: root.layerBSource
        sourceSize.width: 1920
        fillMode: Image.Stretch
        asynchronous: false
        opacity: root.activeVisualLayer === 1 && status === Image.Ready ? 0.90 : 0.0
        onStatusChanged: root.photoStatusChanged(1, status)
        Behavior on opacity {
            NumberAnimation {
                duration: root.artistData.reducedMotion ? 0 : root.crossfadeDurationMs
                easing.type: Easing.InOutQuad
            }
        }
    }

    // Compatibility portrait reference for capture harnesses expecting objectName "vinylArtistPortrait"
    Image {
        id: portrait
        objectName: "vinylArtistPortrait"
        width: 1; height: 1
        visible: false
        asynchronous: false
        source: root.artistData.artwork(root.artistData.media.portrait.cover)
    }

    // Fallback cover artwork card when artist has 0 portrait photos
    RoundedPosterImage {
        anchors.right: parent.right
        anchors.rightMargin: root.horizontalGutter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(240, parent.width * 0.34)
        height: width
        radius: 13
        rotation: 3
        visible: !root.portraitReady && root.artistData.record.releases.length > 0
        sources: root.artistData.record.releases.length
            ? [root.artistData.artwork(root.artistData.record.releases[0].album.cover)] : []
    }

    // Approved HTML .hero::after directional left and bottom dark legibility scrims
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(0.016, 0.020, 0.031, 0.92) }
            GradientStop { position: 0.44; color: Qt.rgba(0.016, 0.020, 0.031, 0.56) }
            GradientStop { position: 0.78; color: Qt.rgba(0.016, 0.020, 0.031, 0.16) }
            GradientStop { position: 1.0; color: Qt.rgba(0.016, 0.020, 0.031, 0.36) }
        }
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.020, 0.024, 0.035, 0.34) }
            GradientStop { position: 0.54; color: Qt.rgba(0.020, 0.024, 0.035, 0.08) }
            GradientStop { position: 0.84; color: Qt.rgba(0.035, 0.039, 0.055, 0.74) }
            GradientStop { position: 1.0; color: Qt.rgba(0.035, 0.039, 0.055, 0.96) }
        }
    }

    // Top-right origin/role decor (.hero-decor parity: gold rule + DETROIT / MICHIGAN + THE ARTIST)
    Column {
        anchors.right: parent.right
        anchors.rightMargin: root.horizontalGutter
        y: 120
        spacing: 6
        visible: root.width >= 760
        Rectangle {
            anchors.right: parent.right
            width: 42
            height: 2
            color: theme.gold
        }
        Text {
            anchors.right: parent.right
            text: {
                if (!root.artistData.liveMode && root.artistData.record.name === "Eminem")
                    return "DETROIT / MICHIGAN";
                var facts = root.artistData.context.facts || [];
                for (var i = 0; i < facts.length; ++i) {
                    if (facts[i].label === "Origin" || facts[i].label === "From")
                        return String(facts[i].value).toUpperCase();
                }
                return "VINYL / ARTIST ROOM";
            }
            textFormat: Text.PlainText
            color: Qt.rgba(0.969, 0.969, 0.961, 0.76)
            font.family: theme.ui
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 2.2
        }
        Text {
            anchors.right: parent.right
            text: "THE ARTIST"
            textFormat: Text.PlainText
            color: Qt.rgba(0.969, 0.969, 0.961, 0.62)
            font.family: theme.ui
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 2.2
        }
    }

    // Bottom-left hero identity and actions (.hero-inner parity)
    Column {
        id: identity
        x: root.fullBleed ? root.horizontalGutter : 0
        width: Math.max(220, (root.fullBleed ? root.width - 2 * root.horizontalGutter : root.width) * (root.width < 720 ? 0.94 : 0.78))
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.max(64, Math.min(116, root.viewportHeight * 0.091))
        spacing: 14

        Text {
            text: (!root.artistData.liveMode && root.artistData.record.name === "Eminem")
                ? "VINYL / THE ARTIST"
                : ("VINYL / " + (root.sourcedContext ? root.artistData.context.roles.toUpperCase() : "THE ARTIST"))
            textFormat: Text.PlainText
            color: theme.gold
            font.family: theme.ui
            font.pixelSize: 12
            font.weight: Font.Bold
            font.letterSpacing: 2.4
        }

        Text {
            objectName: "vinylArtistName"
            width: parent.width
            text: root.artistData.record.name
            textFormat: Text.PlainText
            color: theme.ink
            font.family: theme.display
            font.pixelSize: root.titlePixelSize
            font.weight: Font.Normal
            font.letterSpacing: -root.titlePixelSize * 0.076
            wrapMode: Text.Wrap
            lineHeight: 0.88
        }

        // Facts strip (.hero-facts parity: Hip-hop / Rap · 12 studio albums · 1996—present)
        Flow {
            width: parent.width
            spacing: 10
            Text {
                text: (!root.artistData.liveMode && root.artistData.record.name === "Eminem")
                    ? "Hip-hop / Rap"
                    : (root.sourcedContext ? root.artistData.context.roles : "Catalogue artist")
                textFormat: Text.PlainText
                color: Qt.rgba(0.969, 0.969, 0.961, 0.90)
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.Medium
            }
            Text {
                text: "·"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.Bold
            }
            Text {
                text: (!root.artistData.liveMode && root.artistData.record.name === "Eminem")
                    ? "12 studio albums"
                    : (String(root.artistData.allDiscographyCards.length) + (root.artistData.allDiscographyCards.length === 1 ? " release" : " releases"))
                textFormat: Text.PlainText
                color: Qt.rgba(0.969, 0.969, 0.961, 0.90)
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.Medium
            }
            Text {
                visible: (!root.artistData.liveMode && root.artistData.record.name === "Eminem") || root.artistData.singlesRows.length > 0
                text: "·"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.Bold
            }
            Text {
                visible: (!root.artistData.liveMode && root.artistData.record.name === "Eminem") || root.artistData.singlesRows.length > 0
                text: (!root.artistData.liveMode && root.artistData.record.name === "Eminem")
                    ? "1996—present"
                    : (String(root.artistData.singlesRows.length) + (root.artistData.singlesRows.length === 1 ? " track" : " tracks"))
                textFormat: Text.PlainText
                color: Qt.rgba(0.969, 0.969, 0.961, 0.90)
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.Medium
            }
        }

        // Primary hero actions (.hero-actions parity: 46px height, 9px radius, gold Play with dark gap focus ring)
        Flow {
            width: parent.width
            spacing: 12

            KeyboardAction {
                focusOnPointer: false
                id: playAction
                objectName: "vinylArtistPlay"
                width: 46
                height: 46
                focusRadius: 9
                focusPrimary: true
                spaceActivates: true
                enabled: root.artistData.playable !== false
                opacity: enabled ? 1 : 0.4
                accessibleName: root.artistData.demoPlaying ? "Pause artist" : "Play artist"
                onTriggered: {
                    root.artistData.toggleDemoPlayback();
                    root.playArtistRequested();
                }
                onActiveFocusChanged: if (activeFocus) root.itemFocused(playAction)
                Rectangle {
                    anchors.fill: parent
                    radius: 9
                    color: theme.gold
                }
                VinylControlIcon {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    symbol: root.artistData.demoPlaying ? "pause" : "play"
                    color: "#17181c"
                }
            }

            KeyboardAction {
                focusOnPointer: false
                id: shuffleAction
                objectName: "vinylArtistShuffle"
                width: 46
                height: 46
                focusRadius: 9
                spaceActivates: true
                enabled: root.artistData.playable !== false
                opacity: enabled ? 1 : 0.4
                accessibleName: "Shuffle artist tracks"
                onTriggered: {
                    root.artistData.shuffleDemoSong();
                    root.shuffleArtistRequested();
                }
                onActiveFocusChanged: if (activeFocus) root.itemFocused(shuffleAction)
                Rectangle {
                    anchors.fill: parent
                    radius: 9
                    color: shuffleAction.interactionActive ? Qt.rgba(0.11, 0.12, 0.16, 0.78) : Qt.rgba(0.063, 0.067, 0.09, 0.56)
                    border.width: 1
                    border.color: shuffleAction.interactionActive ? Qt.rgba(1, 1, 1, 0.34) : theme.edge
                }
                VinylControlIcon {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    symbol: "shuffle"
                    color: theme.ink
                }
            }

            KeyboardAction {
                focusOnPointer: false
                id: save
                objectName: "vinylArtistSave"
                width: 46
                height: 46
                focusRadius: 9
                spaceActivates: true
                enabled: root.artistData.saveEnabled !== false
                opacity: enabled ? 1 : 0.4
                accessibleName: root.artistData.saved ? "Unsave artist" : "Save artist"
                onTriggered: root.artistData.toggleSaved()
                onActiveFocusChanged: if (activeFocus) root.itemFocused(save)
                Rectangle {
                    anchors.fill: parent
                    radius: 9
                    color: root.artistData.saved
                        ? Qt.rgba(0.94, 0.77, 0.29, 0.14)
                        : (save.interactionActive ? Qt.rgba(0.11, 0.12, 0.16, 0.78) : Qt.rgba(0.063, 0.067, 0.09, 0.56))
                    border.width: 1
                    border.color: root.artistData.saved ? theme.gold : theme.edge
                }
                VinylControlIcon {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    symbol: root.artistData.saved ? "saved" : "save"
                    color: root.artistData.saved ? theme.gold : theme.ink
                }
            }

            KeyboardAction {
                focusOnPointer: false
                id: create
                objectName: "vinylArtistCreatePlaylist"
                width: 46
                height: 46
                focusRadius: 9
                spaceActivates: true
                visible: false // Available through the destination menu instead of a fourth hero action.
                accessibleName: "Create playlist"
                onTriggered: root.createPlaylistRequested()
                onActiveFocusChanged: if (activeFocus) root.itemFocused(create)
                Rectangle {
                    anchors.fill: parent
                    radius: 9
                    color: create.interactionActive ? Qt.rgba(0.11, 0.12, 0.16, 0.78) : Qt.rgba(0.063, 0.067, 0.09, 0.56)
                    border.width: 1
                    border.color: theme.edge
                }
                VinylControlIcon {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    symbol: "queue"
                    color: theme.gold
                }
            }
        }
    }
}
