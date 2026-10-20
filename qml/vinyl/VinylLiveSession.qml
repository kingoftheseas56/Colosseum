import QtQuick
import Colosseum.Player
import Colosseum.Activity
import Colosseum.Vinyl 1.0

// UI adapter over the existing mpv and profile Progress/Collection owners.
// Fixture presentation data never enters this path.
Item {
    id: root
    objectName: "vinylLiveSession"
    property var emptyEdition: ({id:"", name:"", title:"", artist:"", year:"", cover:"", about:"", discs:[[]], credits:[]})
    property var shownEdition: emptyEdition
    readonly property var activeQueueRow: queueIndex>=0 && queueIndex<queue.length ? queue[queueIndex] : null
    readonly property var playingEdition: activeQueueRow ? activeQueueRow.edition : emptyEdition
    readonly property string shownEditionId: shownEdition.id
    readonly property string shownAlbumId: shownEdition.albumId || shownEdition.id
    readonly property string playingEditionId: playingEdition.id
    readonly property var editions: shownEdition.sourceAlbum ? normalizeEditions(shownEdition.sourceAlbum) : []
    property int shownDisc: 0
    readonly property int playingDisc: activeQueueRow ? activeQueueRow.discIndex : 0
    readonly property int playingIndex: activeQueueRow ? activeQueueRow.trackIndex : -1
    readonly property var tracks: shownEdition.discs[shownDisc] || []
    readonly property var playingTrack: (playingEdition.discs[playingDisc] || [])[playingIndex] || ({title:"", seconds:0})
    readonly property var queue: plainData(backend.rows) || []
    readonly property int queueIndex: backend.activeIndex
    readonly property bool canPrevious: queueIndex>0 || (queueIndex>=0 && repeatMode==="all")
    readonly property bool canNext: (queueIndex>=0 && queueIndex+1<queue.length) || (queueIndex>=0 && repeatMode==="all")
    property int queueSerial: 0
    readonly property bool shuffleEnabled: backend.shuffled
    readonly property string repeatMode: backend.repeatMode
    property var unshuffledIds: []
    property int queueRequest: -1
    property string queueAlbumId: ""
    property bool queueInsertNext: false
    property var pendingEnqueues: []
    readonly property var player: backend.player
    readonly property bool loaded: backend.loaded
    readonly property real resumeSeconds: backend.position
    readonly property bool seekPending: backend.seekPending
    readonly property real position: backend.position
    readonly property real measuredDuration: backend.duration
    readonly property real duration: backend.duration
    property alias paused: backend.paused
    readonly property string sourceState: backend.sourceState
    readonly property string qualityLabel: backend.qualityLabel
    readonly property string error: backend.error
    property string browseError: ""
    property bool browsing: false
    property var results: []
    readonly property bool pagedSearchSupported: !!source && source.cataloguePageVersion === 1
    property string searchQuery: ""
    property bool searchComplete: false
    property int searchGeneration: 0
    property string searchRevision: ""
    property int searchOffset: 0
    property var searchBuffer: []
    property int browseRequest: -1
    property string browseOperation: ""
    property string browseValue: ""
    property int playRequest: -1
    property var source: typeof VinylLocalSource !== "undefined" ? VinylLocalSource : null
    property var collectionStore: typeof Collection !== "undefined" ? Collection : null
    property var progressStore: typeof Progress !== "undefined" ? Progress : null
    property var activitySink: typeof ProfileActivity !== "undefined" ? ProfileActivity : null
    property var activityPreferences: typeof ProfilePreferences !== "undefined" ? ProfilePreferences : null
    VinylBackendSession {
        id: backend
        objectName: "vinylBackendSession"
        source: root.source
        surface: root
        collectionStore: root.collectionStore
        preferences: root.activityPreferences
        profileScope: typeof VinylProfileScope !== "undefined" ? VinylProfileScope : "local-test"
        onPlaybackRequested: {root.manualNoteLine=-1;root.videoShown=false;root.sourceKind=root.playingEdition.origin || "local";root.playbackRequested();}
        onPlaybackEnding: natural => root.activityEnd(natural)
        onDiscontinuity: seconds => root.activityDiscontinuity(seconds)
    }
    onLoadedChanged: { if(loaded) activityBegin(); }
    readonly property bool activityEnabled: profileActive && !shuttingDown && activitySink!==null
        && (!activityPreferences || activityPreferences.keepActivityHistory!==false)
    property bool activityActive: false
    property var activityRecorder: activityTracker
    ActivityPlaybackTracker {
        id: activityTracker
        objectName: "vinylActivityTracker"
        sink: root.activityEnabled ? root.activitySink : null
    }
    function activityBegin() {
        if(activityActive || !activityEnabled || !loaded || !player || playingIndex<0) return;
        try {
            if(!activitySink.newSessionId) return;
            var row=queue[queueIndex];if(!row) return;
            activityRecorder.begin({world:"vinyl",kind:"music",titleKey:row.albumId,
                itemKey:JSON.stringify([row.albumId,row.editionId,row.discIndex,row.trackIndex,row.trackId]),
                title:playingTrack.title,itemLabel:playingEdition.name,cover:"",syncable:false,source:"vinyl_session"},activitySink.newSessionId());
            backend.beginListeningFacts();activityActive=true;activityDiscontinuity(position);
            activityRecorder.playbackStateChanged(!paused,Math.round(position*1000),Math.round(duration*1000));
        } catch(failure) {console.warn("Vinyl Activity begin",failure);activityActive=false;}
    }
    function activitySample(consuming) {
        if(!activityActive || !activityEnabled || !player || !loaded) return;
        var advancing=!seekPending && sourceState==="playing" && (consuming===undefined ? !paused : consuming);
        try {
            var qualified=backend.qualifyListeningSample(advancing);
            activityRecorder.sample(Math.round(position*1000),Math.round(duration*1000),1000,qualified);
        } catch(failure) {console.warn("Vinyl Activity sample",failure);}
    }
    function activityDiscontinuity(seconds) {
        if(!activityActive) return;
        try {activityRecorder.discontinuity(Math.round(seconds*1000),Math.round(duration*1000),1000);} catch(failure) {}
    }
    function activityEnd(natural) {
        if(!activityActive) return;
        activitySample();
        try {
            if(natural) activityRecorder.naturalEof(Math.round(position*1000),Math.round(duration*1000));
            activityRecorder.endSession(Math.round(position*1000),Math.round(duration*1000));
        } catch(failure) {}
        backend.endListeningFacts(natural);
        activityActive=false;
    }
    function scheduleActivityBegin() {
        var sink=activitySink, scope=profileGeneration, generation=epoch;
        Qt.callLater(function() {
            if(root.activitySink===sink && root.profileGeneration===scope && root.epoch===generation)
                root.activityBegin();
        });
    }
    onActivityEnabledChanged: {if(!activityEnabled) activityEnd(false);else scheduleActivityBegin();}
    // The native tracker discards the old sink's buffered identity on rebinding.
    // Never flush an old logical session through a newly bound profile sink.
    onActivitySinkChanged: {activityActive=false;scheduleActivityBegin();}
    Connections {
        target: root.activitySink
        ignoreUnknownSignals: true
        function onRetentionEnabledChanged(enabled) {root.activityActive=false;if(enabled) root.scheduleActivityBegin();}
        function onResetCommitted(generation,atMs) {root.activityActive=false;root.scheduleActivityBegin();}
        function onResetApplied(generation,atMs) {root.activityActive=false;root.scheduleActivityBegin();}
    }
    property string sourceKind: ""
    property string fallbackReason: ""
    property bool audioAdvanced: false
    property int epoch: 0
    // Import callbacks use this profile-only generation; track changes must not invalidate imports.
    property int profileGeneration: 0
    property bool resetting: false
    property bool shuttingDown: false
    property bool profileActive: true
    readonly property string persistenceError: progressStore && progressStore.healthy === false
        ? (progressStore.persistenceError || "Music progress could not be saved.") : ""
    property bool saved: false
    property var library: []
    readonly property bool hasLyrics: lyrics.length > 0
    readonly property bool hasNotes: Object.keys(notes).length > 0
    readonly property bool hasVideo: false
    readonly property var credits: shownEdition.credits || []
    readonly property var playingCredits: creditGroups(playingTrack.credits || playingEdition.credits || [])
    readonly property var lyrics: lyricLines(playingTrack.lyrics)
    readonly property var notes: noteGroups(playingTrack.notes || playingEdition.notes)
    readonly property var videos: []
    readonly property var moreAlbums: []
    readonly property var information: shownEdition.id ? [["source","Source","Local library"],["tracks","Tracks",totalTracks(shownEdition)],["length","Length",shownEdition.length || "Unknown"]] : []
    readonly property bool timedLyrics: lyrics.length > 0 && lyrics.every(l => typeof l.time === "number")
    readonly property int currentLineIndex: {
        if (!timedLyrics) return -1;
        var found=-1; for(var i=0;i<lyrics.length;++i) if(lyrics[i].time<=position) found=i;
        return found;
    }
    property int manualNoteLine: -1
    readonly property int activeNoteLine: {
        if(manualNoteLine>=0 && notes[manualNoteLine]) return manualNoteLine;
        if(!autoNotes || !hasNotes) return -1;
        var keys=Object.keys(notes).map(Number).sort((a,b)=>a-b);
        if(!timedLyrics) return keys[0];
        var found=-1;keys.forEach(index=>{if(index<=currentLineIndex) found=index;});return found;
    }
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
    function totalTracks(e) { return (e.discs || []).reduce((n,d) => n+d.length,0); }
    function sectionVisible(key) {
        return key === "about" ? !!shownEdition.about : key === "credits" ? credits.length>0 :
            key === "editions" ? editions.length>1 : key === "information" ? information.length>0 : false;
    }
    function selectDisc(index) { if (Number.isInteger(index) && index>=0 && index<shownEdition.discs.length) shownDisc=index; }
    function selectEdition(id) {
        var e=editions.find(e => e.id===id); if(!e) return;
        shownEdition=e;shownDisc=0;editionSelected();
    }
    function trackArtists(index) { var t=tracks[index];return t && t.artist !== shownEdition.artist ? t.artist : ""; }
    function isPlayingTrack(index) { return shownEditionId===playingEditionId && shownDisc===playingDisc && playingIndex===index; }
    function creditGroups(value) {
        if (!Array.isArray(value)) return value ? [{role:"Credits",names:[String(value)]}] : [];
        return value.filter(v=>v && (typeof v==="string" || typeof v==="object")).map(v => typeof v === "string" ? {role:"Credits",names:[v]} : {role:v.role || "Credits",names:v.names || [v.name || ""]});
    }
    function lyricLines(value) {
        if (typeof value === "string") return value.split("\n").filter(l => l.trim()).map(l => ({text:l}));
        return Array.isArray(value) ? value.filter(l=>l && typeof l.text==="string").map(l=>{var line={text:l.text};var time=typeof l.time==="number" ? l.time : l.at;if(typeof time==="number" && isFinite(time) && time>=0) line.time=time;return line;}) : [];
    }
    function noteGroups(value) {
        var input=typeof value==="string" ? {0:value} : value, result={};
        if(!input || typeof input!=="object" || Array.isArray(input)) return result;
        Object.keys(input).forEach(key=>{
            if(!/^\d+$/.test(key)) return;
            var values=Array.isArray(input[key]) ? input[key] : [input[key]];
            var notes=values.map(n=>typeof n==="string" ? {text:n} : n).filter(n=>n && typeof n.text==="string" && n.text.length);
            if(notes.length) result[key]=notes;
        });return result;
    }
    function toggleLyrics() { if(hasLyrics) lyricsShown=!lyricsShown; }
    function toggleNotes() { if(hasNotes) {autoNotes=!autoNotes;lyricsShown=true;} }
    function toggleVideo() {}
    function showNotes(index) { if(notes[index]) {manualNoteLine=index;lyricsShown=true;} }
    function seekToLine(index) { if(timedLyrics && lyrics[index]) seek(lyrics[index].time); }
    function noteWindow(index) {
        if(!timedLyrics || !lyrics[index]) return 0;
        for(var i=index+1;i<lyrics.length;++i) if(notes[i]) return Math.max(0,(lyrics[i].time-lyrics[index].time)*1000);
        return Math.max(0,(duration-lyrics[index].time)*1000);
    }
    function plainData(value) {
        // QVariantList/QStringList arrive as Qt sequence wrappers, not JS Arrays.
        // Materialize their JSON data shape once at the adapter boundary; ordinary
        // malformed {0:...,length:...} objects remain objects and fail validation.
        try {return JSON.parse(JSON.stringify(value));} catch(error) {return null;}
    }
    function validAlbum(album) {
        album=plainData(album);
        if(!album || typeof album.id!=="string" || !album.id.length || !Array.isArray(album.tracks)) return false;
        if(album.artists!==undefined && !Array.isArray(album.artists)) return false;
        var es=album.editions===undefined ? [album] : album.editions;
        if(!Array.isArray(es) || !es.length) es=[album];
        return es.every(e=>e && Array.isArray(e.tracks) && e.tracks.every(t=>t && typeof t.id==="string" && t.id.length && (t.artists===undefined || (Array.isArray(t.artists) && t.artists.every(a=>typeof a==="string"))) && (t.disc===undefined || (Number.isInteger(t.disc) && t.disc>0 && t.disc<=1000)) && (t.seconds===undefined || (typeof t.seconds==="number" && isFinite(t.seconds) && t.seconds>=0))));
    }
    function normalizeEditions(album) {
        album=plainData(album);
        if (!album || !album.id) return [];
        var es=Array.isArray(album.editions) && album.editions.length ? album.editions : [{id:album.id,title:album.title,tracks:album.tracks || []}];
        return es.map(e => normalize(album,e));
    }
    function normalize(album,edition) {
        album=plainData(album);edition=edition ? plainData(edition) : null;
        edition=edition || (Array.isArray(album.editions) && album.editions.length ? album.editions[0] : album);
        var artists=album.artists || [], ts=Array.isArray(edition.tracks) ? edition.tracks : [];
        var discNumbers=[];
        ts.forEach(t => {var d=Math.max(1,Number(t.disc)||1);if(discNumbers.indexOf(d)<0) discNumbers.push(d);});
        discNumbers.sort((a,b)=>a-b);if(!discNumbers.length) discNumbers=[1];
        var discs=discNumbers.map(d => ts.filter(t => Math.max(1,Number(t.disc)||1)===d).map(t => ({
            id:t.id,title:t.title || "Untitled track",artist:(t.artists || artists).join(" · "),seconds:Number(t.seconds)||0,
            available:t.available!==false,localPath:t.localPath || "",externalIds:t.externalIds || {},
            disc:d,number:t.number || 0,lyrics:t.lyrics,notes:t.notes,credits:t.credits})));
        var seconds=ts.reduce((n,t)=>n+(Number(t.seconds)||0),0);
        return {id:edition.id || album.id,albumId:album.id,browseId:album.id,origin:album.origin || "local",
            sourceAlbum:album,name:album.title || "Untitled audio",title:edition.title || album.title || "Untitled audio",subtitle:edition.title || album.title || "Untitled audio",
            artist:artists.join(" · "),year:album.year || "",cover:album.cover || "",about:typeof album.about==="string" ? album.about : (typeof album.notes==="string" ? album.notes : ""),
            notes:album.notes || "",credits:creditGroups(edition.credits || album.credits || []),discNumbers:discNumbers,
            length:seconds>0 ? formatTime(seconds) : "",discs:discs};
    }
    property int importIdentityGeneration: 0
    property var pendingIdentityPaths: []
    property var pendingImportCeremony: null
    property var pendingIdentitySource: null
    signal identityDecisionRequested(var ceremony)
    signal importFinished(var outcome)
    function cancelImportIdentity(userCancelled) {
        if(userCancelled && pendingImportCeremony)
            browseError="Import cancelled. Open local music and choose these files again when you are ready to decide their identity.";
        importIdentityGeneration++;pendingIdentityPaths=[];pendingImportCeremony=null;pendingIdentitySource=null;
    }
    function decideImportIdentity(relationship, choice) {
        if(shuttingDown || !profileActive || !pendingImportCeremony
                || pendingImportCeremony.relationship!==relationship) return false;
        if(source!==pendingIdentitySource) {
            browseError="The audio source changed. Open the files again to retry.";cancelImportIdentity();return false;
        }
        if(!source || !source.decideImportIdentity || !source.decideImportIdentity(relationship,choice)) {
            browseError="The identity decision could not be saved. Import the audio again to retry.";
            cancelImportIdentity();return false;
        }
        var paths=pendingIdentityPaths.slice();cancelImportIdentity();
        // The shared dialog closes after emitting its choice. Retry on the next turn
        // so another file's ceremony cannot be hidden by that close.
        var generation=profileGeneration, importGeneration=importIdentityGeneration, importSource=source;
        Qt.callLater(function() {
            if(!root.shuttingDown && root.profileActive && generation===root.profileGeneration
                    && importGeneration===root.importIdentityGeneration && importSource===root.source) root.importFiles(paths);
        });
        return true;
    }
    function importFiles(paths) {
        if(shuttingDown || !profileActive) return {imported:0,errors:[]};
        cancelImportIdentity();
        if (!source || !source.importFiles) {
            browseError="Local audio import is unavailable.";
            return {imported:0,errors:[browseError]};
        }
        var result=plainData(source.importFiles(paths));
        if(!result) {browseError="The local import returned invalid data.";return {imported:0,errors:[browseError]};}
        var errors=(result.errors || []).slice();
        var albums=result.albums || [];
        var count=0, replaced=result.replacedManifestPaths || [], removeIds=[];
        var entries=albums.map(album=>{
            var prior=library.find(entry=>entry.id===album.id);
            return {id:album.id,type:"album",title:album.title,cover:album.cover || "",
                artist:(album.artists || []).join(" · "),browseId:album.id,
                payload:{version:1,album:album,saved:!prior || !prior.payload || prior.payload.saved!==false}};
        });
        library.forEach(entry=>{var old=entry.payload && entry.payload.album;
            if(old && replaced.indexOf(old.manifestPath)>=0 && !albums.some(a=>a.id===old.id)) removeIds.push(entry.id);});
        if(entries.length || removeIds.length) {
            if(collectionStore && typeof collectionStore.replaceItems==="function") {
                if(collectionStore.replaceItems("Vinyl",removeIds,entries)) count=entries.length;
                else errors.push("The local import could not be saved. Your previous library was kept.");
            } else if(replaced.length) errors.push("This import requires atomic library updates, which are unavailable.");
            else entries.forEach(entry=>{
                if(collectionStore && collectionStore.add("Vinyl",entry)) count++;
                else errors.push("The local album could not be saved to your library.");
            });
        }
        refreshLibrary();
        browseError=errors.join("\n");
        var ceremonies=result.identityCeremonies || [];
        if(ceremonies.length) {
            pendingIdentityPaths=Array.prototype.slice.call(paths);
            pendingImportCeremony=ceremonies[0];pendingIdentitySource=source;
            identityDecisionRequested(pendingImportCeremony);
        }
        var outcome={imported:count,errors:errors,pendingIdentity:ceremonies.length>0};
        importFinished(outcome);
        return outcome;
    }
    function refreshSource() {
        if (!source || !source.setAlbums) return;
        // Native setAlbums accumulates track sources across entries, while the last
        // album/track with a canonical ID wins. Retain distinct session snapshots
        // FIRST, then current Collection records LAST: omitted queued recordings
        // remain resolvable without replacing fresh browse metadata or paths.
        var records=[], seen=new Set();
        function retain(album) {if(album && !seen.has(album)) {records.push(album);seen.add(album);}}
        retain(shownEdition.sourceAlbum);retain(playingEdition.sourceAlbum);
        queue.forEach(r=>retain(r.edition.sourceAlbum));
        records=records.concat(library);
        source.setAlbums(records);
    }
    function cancelBrowse() {
        // Invalidate first: a replaceable adapter may complete synchronously during cancellation.
        browseRequest=-1;browsing=false;browseOperation="";
        searchGeneration++;searchComplete=false;searchBuffer=[];searchRevision="";searchOffset=0;
        if (source) source.cancel("browse");
    }
    function search(query) {
        if(shuttingDown || !profileActive) return;
        cancelBrowse();searchQuery=String(query || "").trim();results=[];
        if (!source) {browseError="Local audio is unavailable.";return;}
        refreshSource();browseError="";browsing=true;browseOperation="search";
        requestSearchPage();
    }
    function requestSearchPage() {
        if(pagedSearchSupported)
            browseRequest=source.request("browse","search",searchQuery,
                {pageSize:64,offset:searchOffset,revision:searchRevision});
        else browseRequest=source.request("browse","search",searchQuery);
        if(browseRequest<0) {browsing=false;browseError="The local search could not be requested.";}
    }
    function acceptSearchPage(data) {
        var valid=data && Array.isArray(data.items) && data.items.length<=64
            && typeof data.revision==="string" && data.revision.length>0
            && (!searchRevision || searchRevision===data.revision)
            && typeof data.complete==="boolean" && Number.isInteger(data.nextOffset)
            && (data.complete ? data.nextOffset===-1 : data.items.length>0 && data.nextOffset===searchOffset+data.items.length);
        var seen=new Set(searchBuffer.map(a=>a.id));
        if(valid) for(var i=0;i<data.items.length;i++) {
            var a=data.items[i];
            if(!validAlbum(a) || seen.has(a.id)) {valid=false;break;}
            seen.add(a.id);
        }
        if(!valid) {browsing=false;searchBuffer=[];browseError="The local catalogue page is invalid or changed. Retry the search.";return;}
        searchRevision=data.revision;searchBuffer=searchBuffer.concat(data.items);
        if(data.complete) {results=searchBuffer;searchBuffer=[];searchComplete=true;browsing=false;return;}
        searchOffset=data.nextOffset;
        var generation=searchGeneration, owner=source;
        Qt.callLater(function() {
            if(root.searchGeneration===generation && root.source===owner && root.profileActive
                && !root.shuttingDown && root.browseOperation==="search") root.requestSearchPage();
        });
    }
    function openAlbum(browseId) {
        if(shuttingDown || !profileActive) return;
        cancelBrowse();
        if (!source) {browseError="Local audio is unavailable.";return;}
        refreshSource();browseError=""; browsing=true; browseOperation="album";browseValue=browseId;
        browseRequest=source.request("browse","album",browseId);
    }
    function refreshLibrary() {
        if (!collectionStore) {library=[];saved=false;return;}
        library=(plainData(collectionStore.items("Vinyl")) || []).filter(e => e.payload && e.payload.album && e.payload.album.origin === "local");
        refreshSource();
        var selected=library.find(entry=>entry.id===shownAlbumId);
        saved=!!selected && selected.payload.saved!==false;
    }
    function toggleSaved() {
        if (!shownEditionId || !collectionStore || shuttingDown || !profileActive) return;
        var prior=library.find(entry=>entry.id===shownAlbumId);
        var entry=prior ? Object.assign({},prior) : {
            id:shownAlbumId,type:"album",title:shownEdition.name,cover:shownEdition.cover,
            browseId:shownEdition.browseId,artist:shownEdition.artist,
            payload:{version:1,album:shownEdition.sourceAlbum}};
        // Imported metadata remains the local catalogue; saving is membership,
        // not deletion of the only owned browse/source record.
        var ok=backend.setSavedAlbum(entry,!saved);
        if (!ok) browseError="The album could not be saved. Try again.";
        refreshLibrary();
    }
    function showPlayingAlbum() { shownEdition=playingEdition;shownDisc=playingDisc;refreshLibrary();editionSelected(); }
    function rowFor(e,d,i) {
        var t=e.discs[d][i];return {queueId:"queue:"+(++queueSerial),albumId:e.albumId || e.id,editionId:e.id,
            discNumber:(e.discNumbers || [])[d] || d+1,discIndex:d,trackIndex:i,trackId:t.id,
            title:t.title,artist:t.artist,seconds:t.seconds,cover:e.cover,track:t,edition:e};
    }
    function editionRows(e) {var rows=[];e.discs.forEach((d,di)=>d.forEach((t,i)=>rows.push(rowFor(e,di,i))));return rows;}
    function cancelQueueRequest() {queueRequest=-1;pendingEnqueues=[];if(source) source.cancel("queue");}
    function playTrack(index) { startQueue(index,false); }
    function playAlbum(album,index,editionId) {
        if(shuttingDown || !profileActive) return;
        if(!validAlbum(album)) return;
        var edition=editionId ? normalizeEditions(album).find(e=>e.id===editionId) : normalize(album);
        if(!edition) return;
        var rows=editionRows(edition);index=index===undefined ? 0 : index;
        if(!Number.isInteger(index)||index<0||index>=rows.length||index>=10000) return;
        snapshot();cancelQueueRequest();refreshSource();backend.replaceRows(rows.slice(0,10000),index,false);snapshot();
    }
    function shuffleAlbum(album) {if(!validAlbum(album)) return;playSongs(editionRows(normalize(album)).map(r=>({album:album,track:r.track})),true);}
    function playSongs(songs,shuffleOrder) {
        if(shuttingDown || !profileActive) return;
        if(!Array.isArray(songs)) return;
        var rows=[], cache=new Map();
        songs.slice(0,10000).forEach(song=>{
            if(!song || !song.album || !song.album.id) return;
            var cacheKey=JSON.stringify([song.album.id,song.editionId || ""]), prepared=cache.get(cacheKey);
            if(!prepared) {
                if(!validAlbum(song.album)) return;
                var edition=song.editionId ? normalizeEditions(song.album).find(e=>e.id===song.editionId) : normalize(song.album);
                if(!edition) return;
                var flat=[], byId=new Map();
                edition.discs.forEach((disc,d)=>disc.forEach((track,i)=>{var ref={d:d,i:i};flat.push(ref);if(!byId.has(track.id)) byId.set(track.id,ref);}));
                prepared={edition:edition,flat:flat,byId:byId};cache.set(cacheKey,prepared);
            }
            var ref=Number.isInteger(song.index) ? prepared.flat[song.index] : (song.track && song.track.id ? prepared.byId.get(song.track.id) : null);
            if(ref && song.track && song.track.id && prepared.edition.discs[ref.d][ref.i].id!==song.track.id) return;
            if(ref) rows.push(rowFor(prepared.edition,ref.d,ref.i));
        });
        if(!rows.length) return;
        snapshot();cancelQueueRequest();refreshSource();backend.replaceRows(rows,0,!!shuffleOrder);snapshot();
    }
    function togglePause() {if(playingIndex>=0) paused=!paused;}
    function shuffle() { startQueue(0,true); }
    function startQueue(index,shuffleOrder) {
        if(shuttingDown || !profileActive) return;
        if(!Number.isInteger(index) || index<0 || index>=tracks.length) return;
        var rows=editionRows(shownEdition);
        var selected=rows.findIndex(r=>r.discIndex===shownDisc && r.trackIndex===index);
        if(selected<0 || selected>=10000) {browseError="This track exceeds the queue limit.";return;}
        snapshot();cancelQueueRequest();refreshSource();
        backend.replaceRows(rows.slice(0,10000),selected,!!shuffleOrder);snapshot();
    }
    function enqueueTrack(index,playNext) {
        if(shuttingDown || !profileActive) return;
        if(!Number.isInteger(index) || index<0 || index>=tracks.length) return;
        appendRows([rowFor(shownEdition,shownDisc,index)],playNext);
    }
    function enqueueAlbum(albumId,playNext) {
        if(shuttingDown || !profileActive) return;
        if(typeof albumId!=="string" || !albumId.length) return;
        if(pendingEnqueues.length>=128) {browseError="Please wait for the queued releases to finish loading.";return;}
        pendingEnqueues=pendingEnqueues.concat([{albumId:albumId,playNext:!!playNext,edition:albumId===shownAlbumId ? shownEdition : null}]);
        dispatchNextEnqueue();
    }
    function dispatchNextEnqueue() {
        if(shuttingDown || !profileActive) return;
        if(queueRequest>=0) return;
        while(pendingEnqueues.length) {
            var job=pendingEnqueues[0];pendingEnqueues=pendingEnqueues.slice(1);
            if(job.edition) {appendRows(editionRows(job.edition),job.playNext);continue;}
            if(!source) {browseError="Local audio is unavailable.";continue;}
            queueInsertNext=job.playNext;queueAlbumId=job.albumId;queueRequest=source.request("queue","album",job.albumId);
            if(queueRequest>=0) return;
            browseError="The album could not be queued.";
        }
    }
    function appendRows(rows,playNext) {
        if(!backend.appendRows(rows.slice(0,Math.max(0,10000-queue.length)),playNext))
            browseError="The tracks could not be queued.";
        snapshot();
    }
    function playQueue(index,skipSnapshot) {
        if(!skipSnapshot) snapshot();
        backend.playQueue(index);snapshot();
    }
    function removeQueue(index) {snapshot();backend.removeQueue(index);snapshot();}
    function moveQueue(from,to) {backend.moveQueue(from,to);snapshot();}
    function clearQueue() {snapshot();cancelQueueRequest();backend.clearQueue();snapshot();}
    function setShuffle(enabled) {backend.setShuffle(enabled);snapshot();}
    function cycleRepeat() {backend.cycleRepeat();snapshot();}
    function next(delta) {snapshot();backend.next(delta);snapshot();}
    function retry() {refreshSource();backend.retry();}
    function seek(seconds) {activitySample();backend.seek(seconds);}
    onPausedChanged: {
        if(resetting || shuttingDown || !profileActive) return;
        if(activityActive) {
            if(paused) activitySample(true);
            activityDiscontinuity(position);
            try {activityRecorder.playbackStateChanged(!paused,Math.round(position*1000),Math.round(duration*1000));} catch(failure) {}
        }
        snapshot();
    }
    function snapshot() {
        if(resetting || shuttingDown || !profileActive || !progressStore) return;
        var resume=backend.checkpoint();
        if(!resume.backendState) return;
        if(playingEditionId && playingIndex>=0) progressStore.recordSilent({kind:"music",id:playingEditionId,title:playingTrack.title,
            caption:playingEdition.name,sub:playingTrack.artist,cover:playingEdition.cover,
            progress:duration>0 ? position/duration : 0,resume:resume});
        progressStore.recordSilent({kind:"music-session",id:"vinyl",resume:resume});
    }
    function restore(entry) {
        if(shuttingDown || !profileActive) return;
        if (!progressStore) return;
        var stored=!entry && progressStore.get ? plainData(progressStore.get("music-session","vinyl")) : null;
        var singleton=stored && stored.kind==="music-session" && stored.id==="vinyl";
        var recent=plainData(entry ? [entry] : singleton ? [stored] : progressStore.recent("music",1)) || [];
        if (!recent.length) return;
        var r=recent[0].resume;
        if(r && r.version===3) {
            if(backend.restoreCheckpoint(r)) {
                if(queueIndex>=0) {shownEdition=playingEdition;shownDisc=playingDisc;}
                else if(queue.length) shownEdition=queue[0].edition;
                refreshLibrary();editionSelected();
            }
            return;
        }
        if (!r || [1,2].indexOf(r.version)<0 || !Array.isArray(r.queue) || r.queue.length>10000) return;
        var qi=r.queueIndex, rows=r.queue;
        if(!Number.isInteger(qi)||qi< -1||qi>=rows.length||!Number.isFinite(r.position)) return;
        if(!rows.length) {
            if(!singleton || qi!==-1) return;
            backend.restoreLegacy([],-1,false,[],["off","all","one"].indexOf(r.repeatMode)>=0 ? r.repeatMode : "off",0,0);
            return;
        }
        if(qi===-1 && !singleton) return;
        if(r.version===2 && Array.isArray(r.editionTable)) {
            if(rows.some(row=>!row || !Number.isInteger(row.editionKey) || row.editionKey<0 || row.editionKey>=r.editionTable.length || !Number.isInteger(row.discIndex) || !Number.isInteger(row.trackIndex) || !r.editionTable[row.editionKey] || !Array.isArray(r.editionTable[row.editionKey].discs) || !Array.isArray(r.editionTable[row.editionKey].discs[row.discIndex]) || !r.editionTable[row.editionKey].discs[row.discIndex][row.trackIndex])) return;
            rows=rows.map(row=>{var full=rowFor(r.editionTable[row.editionKey],row.discIndex,row.trackIndex);full.queueId=row.queueId;return full;});
        }
        if(r.version===1) {
            if(!r.edition) return;
            var ts=(r.edition.discs || [])[0];
            if(!ts || rows.some(i=>!Number.isInteger(i)||i<0||i>=ts.length)) return;
            rows=rows.map(i=>rowFor(r.edition,0,i));
        }
        if(rows.some(row=>!row || !row.edition || !row.edition.discs || !Number.isInteger(row.discIndex) || !Number.isInteger(row.trackIndex) || !(row.edition.discs[row.discIndex] || [])[row.trackIndex] || row.trackId!==row.edition.discs[row.discIndex][row.trackIndex].id) || (qi>=0 && rows[qi].trackId!==r.trackId)) return;
        var remapped=Object.create(null);
        var migrated=rows.map(row=>{var copy=Object.assign({},row);copy.queueId="queue:"+(++queueSerial);remapped[row.queueId]=copy.queueId;return copy;});
        var order=Array.isArray(r.unshuffledIds) ? r.unshuffledIds.map(id=>remapped[id]).filter(id=>!!id) : migrated.map(row=>row.queueId);
        if(order.length!==migrated.length) order=migrated.map(row=>row.queueId);
        if(backend.restoreLegacy(migrated,qi,!!r.shuffleEnabled,order,
            ["off","all","one"].indexOf(r.repeatMode)>=0 ? r.repeatMode : "off",
            r.completed ? 0 : Math.max(0,r.position),Math.max(0,Number(r.duration)||0))) {
            shownEdition=qi>=0 ? playingEdition : rows[0].edition;shownDisc=playingDisc;
            refreshLibrary();editionSelected();snapshot();
        }
    }

    function clearProfile() {
        if(shuttingDown) return;
        profileGeneration++;cancelImportIdentity();snapshot();activityEnd(false);
        resetting=true;profileActive=false;backend.clearProfile();cancelBrowse();cancelQueueRequest();
        shownDisc=0;manualNoteLine=-1;lyricsShown=false;autoNotes=false;videoShown=false;
        shownEdition=emptyEdition;results=[];library=[];saved=false;browsing=false;browseError="";
        resetting=false;sourceKind="";fallbackReason="";
        if(source && source.setAlbums) source.setAlbums([]);
    }
    function suspendPlayback(reason) {if(shuttingDown || !profileActive) return;snapshot();backend.suspend();cancelQueueRequest();}
    function shutdown() {
        if(shuttingDown) return;
        snapshot();activityEnd(false);shuttingDown=true;resetting=true;
        browseRequest=-1;queueRequest=-1;pendingEnqueues=[];backend.shutdown();cancelBrowse();cancelQueueRequest();
    }
    Connections {
        target: root.source
        function onCompleted(lane,id,data,failure) {
            if(root.shuttingDown || root.resetting || !root.profileActive) return;
            data=root.plainData(data);
            if (lane==="browse" && id>=0 && id===root.browseRequest) {
                root.browseRequest=-1;root.browseError=failure;
                if (failure) {root.browsing=false;root.searchBuffer=[];return;}
                if (root.browseOperation==="search") {
                    if(root.pagedSearchSupported) root.acceptSearchPage(data);
                    else {root.results=Array.isArray(data) ? data : [];root.searchComplete=true;root.browsing=false;}
                    return;
                }
                root.browsing=false;
                if(root.validAlbum(data) && (data.id===root.browseValue || data.browseId===root.browseValue)) {root.shownEdition=root.normalize(data);root.shownDisc=0;root.refreshLibrary();root.editionSelected();root.albumOpened();}
                else root.browseError="The local album record is invalid.";
            } else if(lane==="queue" && id>=0 && id===root.queueRequest) {
                root.queueRequest=-1;
                if(failure || !root.validAlbum(data) || (data.id!==root.queueAlbumId && data.browseId!==root.queueAlbumId)) root.browseError=failure || "The queued album is invalid.";
                else root.appendRows(root.editionRows(root.normalize(data)),root.queueInsertNext);
                root.dispatchNextEnqueue();

            }
        }
        function onProfileLeaving() { root.clearProfile(); }
        function onProfileEntered() { root.profileActive=true;backend.profileScope=typeof VinylProfileScope!=="undefined" ? VinylProfileScope : "local-test";root.restore();root.refreshLibrary(); }
        function onCheckpointRequested() { root.snapshot(); }
        function onShuttingDown() { root.shutdown(); }
    }
    Timer {objectName:"vinylProgressTimer";interval:5000;repeat:true;running:!root.shuttingDown && !root.resetting && root.loaded;onTriggered:{root.activitySample();root.snapshot();}}
    Component.onCompleted: {restore();refreshLibrary();}
    Component.onDestruction: { if(source) source.cancel("browse");backend.shutdown(); }
}
