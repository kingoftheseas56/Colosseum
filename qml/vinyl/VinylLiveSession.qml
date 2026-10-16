import QtQuick
import Colosseum.Player

// UI adapter over the existing mpv and profile Progress/Collection owners.
// Fixture presentation data never enters this path.
Item {
    id: root
    objectName: "vinylLiveSession"
    property var emptyEdition: ({id:"", name:"", title:"", artist:"", year:"", cover:"", about:"", discs:[[]], credits:[]})
    property var shownEdition: emptyEdition
    property var playingEdition: emptyEdition
    readonly property string shownEditionId: shownEdition.id
    readonly property string shownAlbumId: shownEdition.id
    readonly property string playingEditionId: playingEdition.id
    readonly property var editions: shownEdition.id ? [shownEdition] : []
    property int shownDisc: 0
    property int playingDisc: 0
    property int playingIndex: -1
    readonly property var tracks: shownEdition.discs[0]
    readonly property var playingTrack: playingEdition.discs[0][playingIndex] || ({title:"", seconds:0})
    property var queue: []
    property int queueIndex: -1
    property var player: null
    property bool loaded: false
    property real resumeSeconds: 0
    property bool seekPending: false
    readonly property real position: loaded && player && !seekPending ? player.position : resumeSeconds
    property real measuredDuration: 0
    readonly property real duration: loaded && player && player.duration > 0 ? player.duration : (measuredDuration > 0 ? measuredDuration : playingTrack.seconds)
    property bool paused: true
    property string sourceState: "idle"
    property string qualityLabel: "Local audio"
    property string error: ""
    property string browseError: ""
    property bool browsing: false
    property var results: []
    property int browseRequest: -1
    property string browseOperation: ""
    property int playRequest: -1
    property var source: typeof VinylLocalSource !== "undefined" ? VinylLocalSource : null
    property string sourceKind: ""
    property string fallbackReason: ""
    property bool audioAdvanced: false
    property int epoch: 0
    // Import callbacks use this profile-only generation; track changes must not invalidate imports.
    property int profileGeneration: 0
    property bool resetting: false
    property bool saved: false
    property var library: []
    readonly property bool hasLyrics: false
    readonly property bool hasNotes: false
    readonly property bool hasVideo: false
    readonly property var credits: []
    readonly property var playingCredits: []
    readonly property var lyrics: []
    readonly property var notes: ({})
    readonly property var videos: []
    readonly property var moreAlbums: []
    readonly property var information: []
    readonly property bool timedLyrics: false
    readonly property int currentLineIndex: -1
    readonly property int activeNoteLine: -1
    property bool lyricsShown: false
    property bool autoNotes: false
    property bool videoShown: false
    signal editionSelected()
    signal albumOpened()
    signal artistRequested(string name)
    signal queueRequested()
    signal playbackRequested()
    function artwork(url) { return url || ""; }
    function formatTime(seconds) {
        seconds = Math.max(0, Math.floor(seconds || 0));
        return Math.floor(seconds / 60) + ":" + ("0" + seconds % 60).slice(-2);
    }
    function totalTracks(e) { return e.discs[0].length; }
    function sectionVisible(key) { return false; }
    function selectDisc(index) {}
    function selectEdition(id) {}
    function trackArtists(index) { return tracks[index].artist !== shownEdition.artist ? tracks[index].artist : ""; }
    function isPlayingTrack(index) { return shownEditionId === playingEditionId && playingIndex === index; }
    function normalize(album) {
        var artists = album.artists || [];
        var tracks = album.tracks || [];
        var seconds = tracks.reduce((n,t) => n + (Number(t.seconds) || 0),0);
        return {id:album.id, albumId:album.id, browseId:album.id, origin:album.origin || "",
            sourceAlbum:album, name:album.title || "Untitled audio", title:album.title || "Untitled audio",
            artist:artists.join(" · "), year:album.year || "", cover:album.cover || "", about:"", credits:[],
            length:seconds > 0 ? formatTime(seconds) : "",
            discs:[tracks.map(t => ({id:t.id, title:t.title || "Untitled track",
                artist:(t.artists || []).join(" · "), seconds:Number(t.seconds) || 0,
                available:t.available !== false, localPath:t.localPath || "", externalIds:t.externalIds || {}}))]};
    }
    function importFiles(paths) {
        if (!source || !source.importFiles) {
            browseError="Local audio import is unavailable.";
            return {imported:0,errors:[browseError]};
        }
        var result=source.importFiles(paths);
        var errors=(result.errors || []).slice();
        var albums=result.albums || [];
        var count=0;
        for (var i=0;i<albums.length;++i) {
            var album=albums[i];
            if (typeof Collection === "undefined" || !Collection || !Collection.add("Vinyl", {
                id:album.id,type:"album",title:album.title,cover:album.cover || "",
                artist:(album.artists || []).join(" · "),browseId:album.id,
                payload:{version:1,album:album}})) {
                errors.push("The local album could not be saved to your library.");
            } else count++;
        }
        refreshLibrary();
        browseError=errors.join("\n");
        return {imported:count,errors:errors};
    }
    function refreshSource() {
        if (!source || !source.setAlbums) return;
        var records=library.slice();
        if (shownEdition.sourceAlbum) records.push(shownEdition.sourceAlbum);
        if (playingEdition.sourceAlbum) records.push(playingEdition.sourceAlbum);
        source.setAlbums(records);
    }
    function cancelBrowse() {
        // Invalidate first: a replaceable adapter may complete synchronously during cancellation.
        browseRequest=-1;browsing=false;browseOperation="";
        if (source) source.cancel("browse");
    }
    function search(query) {
        if (!source) {browseError="Local audio is unavailable.";return;}
        refreshSource();browseError=""; browsing=true; browseOperation="search";
        browseRequest=source.request("browse","search",query);
    }
    function openAlbum(browseId) {
        if (!source) {browseError="Local audio is unavailable.";return;}
        refreshSource();browseError=""; browsing=true; browseOperation="album";
        browseRequest=source.request("browse","album",browseId);
    }
    function refreshLibrary() {
        if (typeof Collection === "undefined" || !Collection) {library=[];saved=false;return;}
        library=Collection.items("Vinyl").filter(e => e.payload && e.payload.album && e.payload.album.origin === "local");
        refreshSource();
        saved=shownEditionId.length > 0 && Collection.has("Vinyl",shownEditionId);
    }
    function toggleSaved() {
        if (!shownEditionId) return;
        var ok=saved ? Collection.remove("Vinyl",shownEditionId) : Collection.add("Vinyl",{
            id:shownEditionId,type:"album",title:shownEdition.name,cover:shownEdition.cover,
            browseId:shownEdition.browseId,artist:shownEdition.artist,
            payload:{version:1,album:shownEdition.sourceAlbum}});
        if (!ok) browseError="The album could not be saved. Try again.";
        refreshLibrary();
    }
    function showPlayingAlbum() { shownEdition=playingEdition; refreshLibrary(); editionSelected(); }
    function playTrack(index) { startQueue(index,false); }
    function shuffle() { startQueue(0,true); }
    function startQueue(index,shuffleOrder) {
        if (index<0 || index>=tracks.length) return;
        snapshot();
        playingEdition=shownEdition;
        var order=tracks.map((t,i) => i);
        if (shuffleOrder) {
            for (var i=order.length-1;i>0;--i) {
                var j=Math.floor(Math.random()*(i+1)); var x=order[i];order[i]=order[j];order[j]=x;
            }
            index=0;
        }
        queue=order; queueIndex=index; playingIndex=queue[queueIndex];
        beginTrack(0);
    }
    function retirePlayer() {
        epoch++;
        var old=player; player=null; loaded=false; seekPending=false;
        if (old) {old.pause=true;old.destroy();}
        if (source) source.cancel("play"); playRequest=-1;
        audioAdvanced=false;
    }
    function beginTrack(seconds) {
        retirePlayer(); measuredDuration=0; resumeSeconds=Math.max(0,seconds); error="";
        sourceState="finding"; qualityLabel="Local audio";fallbackReason="";
        sourceKind=playingEdition.origin || "local";
        playbackRequested();paused=false;
        if (!source || !playingTrack.id || playingTrack.available===false) {
            playbackFailed("This track has no playable local source.");return;
        }
        refreshSource();
        playRequest=source.request("play","resolve",playingTrack.id);
        if (playRequest<0) playbackFailed("The audio source could not be requested.");
    }
    function playbackFailed(message) {
        error=message;sourceState="error";paused=true;
        if (player) player.pause=true;
    }
    function loadAudio(url,headers) {
        player=audioComponent.createObject(root,{generation:epoch});
        player.loadFileWithHeaders(url,headers || {});player.pause=paused;
    }
    function next(delta) {
        if (!queue.length) return;
        snapshot();
        var nextIndex=queueIndex+delta;
        if (nextIndex<0 || nextIndex>=queue.length) {paused=true;return;}
        queueIndex=nextIndex;playingIndex=queue[queueIndex];beginTrack(0);
    }
    function retry() { if (playingIndex>=0) beginTrack(position); }
    function seek(seconds) {
        if (!loaded || !player || sourceState!=="playing") return;
        resumeSeconds=Math.max(0,Math.min(duration,seconds));seekPending=true;
        player.seekExact(resumeSeconds);
    }
    onPausedChanged: {
        if (resetting) return;
        if (!paused && sourceState!=="finding" && playingIndex>=0
                && (!player || sourceState==="error" || sourceState==="ended"))
            beginTrack(sourceState==="ended" ? 0 : position);
        if (player) player.pause=paused;
        snapshot();
    }
    function snapshot() {
        if (resetting || !playingEditionId || playingIndex<0 || typeof Progress === "undefined" || !Progress) return;
        // Only stable source IDs, metadata, queue order and position. No resolved URLs.
        Progress.recordSilent({kind:"music",id:playingEditionId,title:playingTrack.title,
            caption:playingEdition.name,sub:playingTrack.artist,cover:playingEdition.cover,
            progress:duration>0 ? position/duration : 0,
            resume:{version:1,edition:playingEdition,queue:queue,queueIndex:queueIndex,
                position:position,duration:duration,trackId:playingTrack.id,completed:sourceState==="ended"}});
    }
    function restore(entry) {
        if (typeof Progress === "undefined" || !Progress) return;
        var recent=entry ? [entry] : Progress.recent("music",1);
        if (!recent.length) return;
        var r=recent[0].resume;
        if (!r || r.version!==1 || !r.edition || !r.edition.discs || !r.queue || !r.queue.length) return;
        var ts=r.edition.discs[0]; var qi=r.queueIndex;
        if (!ts || qi<0 || qi>=r.queue.length || r.queue.some(i => i<0 || i>=ts.length)
                || ts[r.queue[qi]].id!==r.trackId || !isFinite(r.position)) return;
        resetting=true; retirePlayer(); playingEdition=r.edition;shownEdition=r.edition;
        queue=r.queue;queueIndex=qi;playingIndex=queue[qi];resumeSeconds=r.completed ? 0 : Math.max(0,r.position);
        measuredDuration=Math.max(0,Number(r.duration) || 0);
        paused=true;sourceState="paused";resetting=false;refreshLibrary();editionSelected();
    }
    function clearProfile() {
        profileGeneration++;
        snapshot();resetting=true;retirePlayer();cancelBrowse();
        shownEdition=emptyEdition;playingEdition=emptyEdition;playingIndex=-1;queue=[];queueIndex=-1;
        results=[];library=[];saved=false;browsing=false;error="";browseError="";resumeSeconds=0;measuredDuration=0;
        paused=true;sourceState="idle";resetting=false;
        sourceKind="";fallbackReason="";
        if (source && source.setAlbums) source.setAlbums([]);
    }
    Component {
        id: audioComponent
        MpvItem {
            objectName: "vinylMpvPlayer"
            property int generation: -1
            width:1; height:1; visible:false
            function current() { return root.player===this && generation===root.epoch; }
            onFileLoaded: {
                if (!current()) return;
                root.loaded=true;root.sourceState="playing";
                if (duration>0) root.measuredDuration=duration;
                pause=root.paused;
                if (root.resumeSeconds>0) {root.seekPending=true;seekExact(root.resumeSeconds);}
            }
            onDurationChanged: {
                if (current() && duration>0) root.measuredDuration=duration;
            }
            onPositionChanged: {
                if (!current() || !root.loaded) return;
                if (root.seekPending && Math.abs(position-root.resumeSeconds)<2) root.seekPending=false;
                if (!root.seekPending && audioTracks.length>0 && position>root.resumeSeconds+0.1) root.audioAdvanced=true;
            }
            onEndFile: reason => {
                if (!current() || !root.loaded) return;
                if (reason==="eof") {
                    if (root.queueIndex+1<root.queue.length) root.next(1);
                    else {root.resumeSeconds=root.duration;root.loaded=false;root.sourceState="ended";root.paused=true;root.snapshot();}
                }
                else if (reason==="error") root.playbackFailed("Playback stopped. Retry this track.");
            }
            onPlaybackError: (code,message) => {
                if (!current()) return;
                root.playbackFailed("The audio could not be decoded or played. Retry this track or import another file.");
            }
        }
    }
    Connections {
        target: root.source
        function onCompleted(lane,id,data,failure) {
            if (lane==="browse" && id===root.browseRequest) {
                root.browsing=false;root.browseError=failure;
                if (failure) return;
                if (root.browseOperation==="search") root.results=data;
                else {root.shownEdition=root.normalize(data);root.refreshLibrary();root.editionSelected();root.albumOpened();}
            } else if (lane==="play" && id===root.playRequest) {
                if (failure || !data || data.trackId!==root.playingTrack.id) {
                    root.sourceState="error";root.error=failure || "The source did not match this track.";root.paused=true;return;
                }
                root.qualityLabel=data.qualityLabel;
                root.loadAudio(data.url,data.headers);
            }
        }
        function onProfileLeaving() { root.clearProfile(); }
        function onProfileEntered() { root.restore();root.refreshLibrary(); }
        function onCheckpointRequested() { root.snapshot(); }
        function onShuttingDown() { root.snapshot();root.resetting=true; }
    }
    Timer {interval:5000;repeat:true;running:root.loaded;onTriggered:root.snapshot()}
    Timer {
        interval:15000
        running:root.player!==null && !root.audioAdvanced && !root.paused && root.sourceState!=="error"
        onTriggered:root.playbackFailed("The audio did not start in time. Retry this track.")
    }
    Component.onCompleted: {restore();refreshLibrary();}
    Component.onDestruction: { if (source) source.cancel("browse"); retirePlayer(); }
}
