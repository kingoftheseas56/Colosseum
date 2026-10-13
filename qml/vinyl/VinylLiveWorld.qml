import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "vinylLiveWorld"
    property string medium: "Vinyl"
    property bool lifecycleActive: true
    property Item backdrop
    property string page: "search"
    property alias session: session
    signal homeRequested()
    signal mediumSelected(string medium)
    signal minimizeClicked()
    signal fullscreenClicked()
    signal powerClicked()
    signal playbackRequested()
    Theme { id: theme }
    VinylLiveSession {
        id: session
        onAlbumOpened: root.page="album"
        onArtistRequested: name => {query.text=name;root.page="search";search(name);}
        onQueueRequested: queuePopup.open()
        onPlaybackRequested: root.playbackRequested()
    }
    VinylBackdrop {
        id: background
        anchors.fill: parent
        source: root.page==="song" ? session.playingEdition.cover : session.shownEdition.cover
    }
    TopBar {
        id: top
        anchors {left:parent.left;right:parent.right;top:parent.top;margins:theme.margin}
        backdrop: background
        activeMedium: "Vinyl"
        lifecycleActive: root.lifecycleActive
        onHomeRequested: root.homeRequested()
        onMediumSelected: medium => root.mediumSelected(medium)
        onSearchClicked: {root.page="search";query.forceActiveFocus();}
        onMinimizeClicked: root.minimizeClicked()
        onFullscreenClicked: root.fullscreenClicked()
        onPowerClicked: root.powerClicked()
    }
    Row {
        id: navigation
        x:theme.margin;y:100;spacing:10
        Button {text:"Search";onClicked:root.page="search"}
        Button {text:"Library";onClicked:{session.refreshLibrary();root.page="library";}}
        Button {text:"Album";visible:session.shownEditionId!=="";onClicked:root.page="album"}
        Button {text:session.paused ? "Resume listening" : "Now playing";visible:session.playingIndex>=0
            onClicked:{root.page="song";session.paused=false;}}
    }
    Column {
        x:theme.margin;y:160;width:parent.width-2*theme.margin;spacing:16
        visible:root.page==="search"
        Text {text:"Find an album";font.family:theme.display;font.pixelSize:32;color:theme.ink}
        Row {
            spacing:12;width:parent.width
            TextField {
                id:query;objectName:"vinylSearchInput";width:Math.min(640,parent.width-130)
                placeholderText:"Search albums or artists"
                onAccepted:session.search(text)
            }
            Button {objectName:"vinylSearchSubmit";text:"Search";onClicked:session.search(query.text)}
        }
        Text {text:session.browsing ? "Searching…" : session.results.length ? "Choose a release" : "Find an album to start listening."
            color:theme.inkDim;font.family:theme.ui}
    }
    ListView {
        id: results
        objectName:"vinylSearchResults"
        x:theme.margin;y:300;width:parent.width-2*theme.margin;height:bar.y-y-60
        visible:root.page==="search" || root.page==="library"
        clip:true;spacing:8
        model:root.page==="library" ? session.library : session.results
        delegate:KeyboardAction {
            required property var modelData
            required property int index
            objectName:"vinylResult_"+index
            width:results.width;height:84
            accessibleName:modelData.title
            onTriggered:session.openAlbum(modelData.browseId)
            Rectangle {anchors.fill:parent;color:parent.interactionActive ? theme.glassHi : theme.glassTint;radius:12}
            Image {x:8;y:8;width:68;height:68;source:modelData.cover || "";fillMode:Image.PreserveAspectCrop}
            Column {
                x:94;anchors.verticalCenter:parent.verticalCenter;spacing:8
                Text {text:modelData.title;color:theme.ink;font.family:theme.ui;font.pixelSize:17}
                Text {text:(modelData.artist || (modelData.artists || []).join(" · "))+" · "+(modelData.year || "")
                    color:theme.inkDim;font.family:theme.ui;font.pixelSize:12}
            }
        }
        ScrollBar.vertical: ScrollBar {}
    }
    Text {x:theme.margin;y:190;visible:root.page==="library"
        text:session.library.length ? "Saved albums" : "Albums you save will appear here."
        font.family:theme.display;font.pixelSize:28;color:theme.ink}
    VinylAlbumPage {
        x:0;y:150;width:parent.width;height:bar.y-y-50
        visible:root.page==="album";pageData:session;backdrop:background
    }
    VinylSongPage {
        x:0;y:150;width:parent.width;height:bar.y-y-50
        visible:root.page==="song";pageData:session
        onBackRequested:root.page="album"
        onAlbumRequested:{session.showPlayingAlbum();root.page="album";}
    }
    Row {
        x:theme.margin;y:bar.y-45;spacing:12
        Text {text:session.error || session.browseError;color:theme.inkDim;font.family:theme.ui;anchors.verticalCenter:parent.verticalCenter}
        Button {text:"Retry track";visible:session.error!=="";onClicked:session.retry()}
    }
    VinylCreditsPopup {id:creditsPopup;pageData:session;backdrop:background;x:root.width-width-theme.margin;y:bar.y-height-12}
    VinylPlayerBar {
        id:bar;x:theme.margin;y:root.height-height-18;width:root.width-2*theme.margin
        visible:session.playingIndex>=0;pageData:session;backdrop:background
        page:root.page==="song" ? "song" : "album"
        onSongRequested:root.page="song"
        onCreditsRequested:creditsPopup.open()
    }
    Popup {
        id:queuePopup;x:(root.width-width)/2;y:150;width:Math.min(600,root.width-80);height:Math.min(430,root.height-250)
        modal:true;closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        background:Rectangle {color:theme.biblioWashBottom;radius:18;border.color:theme.edge}
        contentItem:ListView {
            clip:true;model:session.queue
            delegate:ItemDelegate {
                required property int modelData
                required property int index
                width:ListView.view.width
                text:(index===session.queueIndex ? "▶ " : "")+(session.playingEdition.discs[0][modelData] || {title:""}).title
                onClicked:{session.snapshot();session.queueIndex=index;session.playingIndex=modelData;session.beginTrack(0);queuePopup.close();}
            }
        }
    }
}
