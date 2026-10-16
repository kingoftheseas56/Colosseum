import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import ".."

// The native local-first entry. Presentation stays in the accepted Album/Song
// components; all library and playback truth belongs to VinylLiveSession.
Item {
    id: root
    objectName: "vinylLiveWorld"
    property string medium: "Vinyl"
    property bool lifecycleActive: true
    property Item backdrop
    property string page: "library"
    property alias session: session
    property string importStatus: ""
    property bool importing: false
    property string songReturnPage: "album"
    // Supplied by the shell's existing Taskbar geometry, not a second dock owner.
    property real shellDockRight: 0
    property real shellDockBottomExtent: 0
    readonly property real footerLeft: Math.max(theme.margin, shellDockRight > 0 ? shellDockRight + 12 : 0)
    readonly property real contentBottom: bar.visible ? bar.y - 54 : height - Math.max(62, shellDockBottomExtent)
    signal homeRequested()
    signal mediumSelected(string medium)
    signal minimizeClicked()
    signal fullscreenClicked()
    signal powerClicked()
    signal playbackRequested()

    function focusLibrary() {
        Qt.callLater(function() {
            if (results.count > 0) {
                results.currentIndex = Math.max(0, Math.min(results.currentIndex, results.count - 1));
                results.forceActiveFocus();
                if (results.currentItem) results.currentItem.forceActiveFocus();
            } else openFiles.forceActiveFocus();
        });
    }
    function showLibrary() {
        session.cancelBrowse();
        session.refreshLibrary();
        page = "library";
        focusLibrary();
    }
    function showSong() {
        if (session.playingIndex < 0) return;
        session.cancelBrowse();
        if (page !== "song") songReturnPage = page;
        page = "song";
        Qt.callLater(function() { song.focusBackControl(); });
    }
    function goBack() {
        if (page === "song") {
            page = songReturnPage === "library" ? "library" : "album";
            if (page === "library") focusLibrary();
            else Qt.callLater(function() { nowPlaying.forceActiveFocus(); });
        } else if (page === "album") showLibrary();
        else if (session.browsing) { session.cancelBrowse(); focusLibrary(); }
        else return false;
        return true;
    }
    function importLocalFiles(paths) {
        if (!paths || !paths.length || importing) return;
        var importGeneration = session.profileGeneration;
        var importSource = session.source;
        importing = true;
        importStatus = "Opening local audio…";
        // Defer one event-loop turn so the busy state can paint before the bounded
        // local inspection. No filename or developer fixture is seeded here.
        Qt.callLater(function() {
            try {
                if (session.profileGeneration !== importGeneration || session.source !== importSource) {
                    importStatus = "Open cancelled because the profile changed. Choose the files again.";
                    return;
                }
                var outcome = session.importFiles(paths);
                importStatus = outcome && outcome.imported > 0 ? "Opened " + outcome.imported + (outcome.imported === 1 ? " local audio file." : " local audio files.") : "No audio files were added.";
                showLibrary();
            } catch (error) {
                importStatus = "Could not open these files. " + String(error);
            } finally {
                importing = false;
            }
        });
    }
    Keys.onEscapePressed: event => { event.accepted = root.goBack(); }
    Theme { id: theme }
    VinylLiveSession {
        id: session
        onAlbumOpened: {
            root.page = "album";
            Qt.callLater(function() { album.focusPrimaryControl(); });
        }
        onPlaybackRequested: root.playbackRequested()
    }
    FileDialog {
        id: audioDialog
        objectName: "vinylOpenFilesDialog"
        title: "Open local audio"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Audio files (*.flac *.mp3 *.m4a *.aac *.ogg *.opus *.wav *.aiff *.aif *.wma *.alac)", "All files (*)"]
        onAccepted: root.importLocalFiles(selectedFiles.map(function(file) { return String(file); }))
        onRejected: openFiles.forceActiveFocus()
    }
    VinylBackdrop {
        id: vinylBackdrop
        anchors.fill: parent
        source: root.page === "song" ? session.playingEdition.cover : root.page === "album" ? session.shownEdition.cover : ""
    }
    TopBar {
        id: top
        objectName: "vinylTopBar"
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: theme.margin }
        backdrop: vinylBackdrop
        activeMedium: "Vinyl"
        lifecycleActive: root.lifecycleActive
        onHomeRequested: root.homeRequested()
        onMediumSelected: medium => root.mediumSelected(medium)
        onSearchClicked: { root.showLibrary(); query.forceActiveFocus(); }
        onMinimizeClicked: root.minimizeClicked()
        onFullscreenClicked: root.fullscreenClicked()
        onPowerClicked: root.powerClicked()
    }
    component LocalAction: KeyboardAction {
        id: control
        property string text
        property bool selected: false
        width: label.implicitWidth + 32
        height: 44
        accessibleName: text
        opacity: enabled ? 1 : 0.35
        focusRadius: 12
        Glass {
            anchors.fill: parent
            backdrop: vinylBackdrop
            radius: 12
            tint: control.interactionActive ? 0.14 : 0.07
            scrim: 0.16
            edge: control.selected ? theme.gold : theme.edge
        }
        Text {
            id: label
            anchors.centerIn: parent
            text: control.text
            color: control.selected ? theme.gold : theme.ink
            font.family: theme.ui
            font.pixelSize: 13
        }
    }
    Flow {
        id: navigation
        objectName: "vinylNavigation"
        x: theme.margin
        y: top.y + top.height + 24
        width: parent.width - 2 * theme.margin
        spacing: 12
        LocalAction {
            objectName: "vinylLibraryButton"
            text: "Library"
            selected: root.page === "library"
            onTriggered: root.showLibrary()
        }
        LocalAction { objectName: "vinylHomeUnavailable"; text: "Home"; enabled: false }
        LocalAction { objectName: "vinylDiscoverUnavailable"; text: "Discover"; enabled: false }
        LocalAction {
            id: openFiles
            objectName: "vinylOpenFiles"
            text: root.importing ? "Opening…" : "Open audio files"
            enabled: !root.importing
            onTriggered: audioDialog.open()
        }
        LocalAction {
            id: nowPlaying
            objectName: "vinylNowPlaying"
            text: "Now playing"
            visible: session.playingIndex >= 0
            onTriggered: root.showSong()
        }
    }
    Item {
        id: libraryHeader
        objectName: "vinylLibraryHeader"
        x: theme.margin; y: navigation.y + navigation.height + 26; width: parent.width - 2 * theme.margin; height: 124
        visible: root.page === "library"
        Text { text: "Your library"; color: theme.ink; font.family: theme.display; font.pixelSize: 32 }
        Text {
            y: 44; width: parent.width
            text: "Local audio · Each file opens as a single-track release."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
            elide: Text.ElideRight
        }
        TextField {
            id: query
            objectName: "vinylSearchInput"
            y: 76; width: Math.min(600, parent.width); height: 44
            color: theme.ink
            placeholderTextColor: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 14
            padding: 14
            selectByMouse: true
            selectionColor: theme.gold
            selectedTextColor: theme.biblioWashBottom
            background: Glass {
                objectName: "vinylSearchGlass"
                backdrop: vinylBackdrop
                radius: 12
                tint: 0.06
                scrim: 0.18
            }
            FocusRing { anchors.fill: parent; shown: query.activeFocus; radius: 12 }
            placeholderText: "Filter your local library"
            onTextChanged: results.currentIndex = 0
            onAccepted: root.focusLibrary()
        }
    }
    ListView {
        id: results
        objectName: "vinylLibraryResults"
        x: theme.margin; y: libraryHeader.y + libraryHeader.height + 10
        width: parent.width - 2 * theme.margin
        height: Math.max(0, root.contentBottom - y)
        visible: root.page === "library"
        clip: true; spacing: 8
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        model: session.library.filter(function(item) {
            var text = (item.title || item.name || "") + " " + (item.artist || (item.artists || []).join(" "));
            return text.toLowerCase().indexOf(query.text.trim().toLowerCase()) >= 0;
        })
        delegate: KeyboardAction {
            required property var modelData
            required property int index
            objectName: "vinylResult_" + index
            width: results.width; height: 84
            accessibleName: modelData.title || modelData.name || "Local audio"
            accessibleDescription: "Open local release"
            onActiveFocusChanged: if (activeFocus) results.currentIndex = index
            onTriggered: { results.currentIndex = index; session.openAlbum(modelData.browseId || modelData.id); }
            Keys.onUpPressed: event => {
                if (index > 0) { results.decrementCurrentIndex(); root.focusLibrary(); }
                else query.forceActiveFocus();
                event.accepted = true;
            }
            Keys.onDownPressed: event => {
                if (index + 1 < results.count) { results.incrementCurrentIndex(); root.focusLibrary(); }
                event.accepted = true;
            }
            Rectangle { anchors.fill: parent; color: parent.interactionActive ? theme.glassHi : theme.glassTint; radius: 12; border.color: theme.edge }
            Rectangle {
                x: 8; y: 8; width: 68; height: 68; radius: 8; color: theme.biblioWashBottom
                Text { anchors.centerIn: parent; text: "♪"; color: theme.gold; font.pixelSize: 27 }
                Image { anchors.fill: parent; source: modelData.cover || ""; fillMode: Image.PreserveAspectCrop }
            }
            Column {
                x: 94; width: parent.width - 112; anchors.verticalCenter: parent.verticalCenter; spacing: 8
                Text { width: parent.width; text: modelData.title || modelData.name || "Local audio"; color: theme.ink; font.family: theme.ui; font.pixelSize: 17; elide: Text.ElideRight }
                Text { width: parent.width; text: (modelData.artist || "Unknown artist") + " · Local audio"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; elide: Text.ElideRight }
            }
        }
        ScrollGlide { flick: results }
    }
    Text {
        objectName: "vinylLibraryEmpty"
        x: theme.margin; y: results.y + 22; width: parent.width - 2 * theme.margin
        visible: root.page === "library" && results.count === 0 && !root.importing
        text: session.library.length ? "No local audio matches this filter." : "Your library is empty. Open audio files to start listening."
        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16; wrapMode: Text.WordWrap
    }
    BackAction {
        id: albumBack
        objectName: "vinylAlbumBack"
        x: theme.margin; y: navigation.y + navigation.height + 20; label: "Library"
        height: 44
        Keys.onReturnPressed: root.showLibrary()
        Keys.onEnterPressed: root.showLibrary()
        visible: root.page === "album"
        onTriggered: root.showLibrary()
    }
    VinylAlbumPage {
        id: album
        x: 0; y: albumBack.y + albumBack.height + 8; width: parent.width; height: Math.max(0, root.contentBottom - y)
        visible: root.page === "album"; pageData: session; backdrop: vinylBackdrop
        artistNavigationEnabled: false
        shuffleEnabled: false
        saveEnabled: true
        scrollbarVisible: false
    }
    VinylSongPage {
        id: song
        x: 0; y: navigation.y + navigation.height + 20; width: parent.width; height: Math.max(0, root.contentBottom - y)
        visible: root.page === "song"; pageData: session
        backLabel: root.songReturnPage === "library" ? "Library" : "Album"
        onBackRequested: root.goBack()
        onAlbumRequested: { session.cancelBrowse(); session.showPlayingAlbum(); root.page = "album"; album.focusPrimaryControl(); }
    }
    Row {
        objectName: "vinylStatusRow"
        x: root.footerLeft; y: root.contentBottom + 8; width: parent.width - x - theme.margin; spacing: 12
        Text {
            objectName: "vinylStatus"
            width: parent.width - (retry.visible ? retry.width + 12 : 0)
            text: session.error || session.browseError || (root.importing ? "Opening local audio…" : session.browsing ? "Opening release…" : root.importStatus)
            color: session.error || session.browseError ? theme.gold : theme.inkDim
            font.family: theme.ui; font.pixelSize: 12; wrapMode: Text.WordWrap
        }
        LocalAction { id: retry; objectName: "vinylRetryTrack"; text: "Retry track"; visible: session.error !== ""; onTriggered: session.retry() }
    }
    VinylCreditsPopup { id: creditsPopup; pageData: session; backdrop: vinylBackdrop; x: root.width - width - theme.margin; y: bar.y - height - 12 }
    VinylPlayerBar {
        id: bar; x: root.footerLeft; y: root.height - height - 18; width: root.width - x - theme.margin
        visible: session.playingIndex >= 0; pageData: session; backdrop: vinylBackdrop
        page: root.page === "song" ? "song" : "album"
        queueEnabled: false
        trackNavigationEnabled: false
        onSongRequested: root.showSong()
        onCreditsRequested: creditsPopup.open()
    }
    Component.onCompleted: { session.refreshLibrary(); root.focusLibrary(); }
}
