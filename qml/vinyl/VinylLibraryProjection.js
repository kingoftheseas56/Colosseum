.pragma library

function values(source) {
    var result=[];
    for(var i=0;source && i<source.length;++i) result.push(source[i]);
    return result;
}
function occurrence(item) {
    var row=item && (item.row || item.ref);
    return row && row.albumId && row.editionId && row.trackId
        ? JSON.stringify([row.albumId,row.editionId,row.discIndex,row.trackIndex,row.trackId]) : "";
}
function providerRecording(item) {
    var row=item && item.row, track=row && row.track;
    if(!track) return "";
    var youtube=(row.edition || {}).origin==="youtube-music";
    var id=youtube ? track.videoId : row.trackId;
    return id ? JSON.stringify([youtube ? "youtube-music" : "local",id]) : "";
}
function positiveRecordings(state) {
    if(!state || state.ok!==true) return [];
    var snapshot;
    try { snapshot=JSON.parse(state.snapshot || "{}"); } catch(error) { return []; }
    return values(snapshot.entities).filter(function(entity) {
        return entity.kind==="recording" && entity.explicit && entity.explicit.feedback==="positive";
    }).map(function(entity) {
        var provider=entity.identity==="provider" ? entity : entity.explicit.providerFeedbackRef;
        return provider && provider.providerId && provider.itemId ? JSON.stringify([provider.providerId,provider.itemId]) : "";
    }).filter(function(key) { return !!key; });
}
function card(item,kind,action,extra) {
    return Object.assign({},item,{kind:kind,libraryItem:item,libraryAction:action},extra || {});
}

function downloadCards(downloadedMusic) {
    var entries=[];
    values(downloadedMusic).forEach(function(entry,order) {
        if(!entry || entry.kind!=="music" || entry.available!==true
            || typeof entry.path!=="string" || !entry.path.trim()
            || typeof entry.addedAt!=="number" || !isFinite(entry.addedAt) || entry.addedAt<0) return;
        var music=entry.music || {}, album=music.album, index=music.trackIndex;
        if(!album || album.origin!=="youtube-music" || typeof album.id!=="string" || !album.id
            || typeof index!=="number" || !isFinite(index) || Math.floor(index)!==index
            || index<0 || !album.tracks || index>=album.tracks.length
            || typeof music.videoId!=="string" || !/^[A-Za-z0-9_-]{11}$/.test(music.videoId)) return;
        var track=album.tracks[index];
        if(!track || typeof track.id!=="string" || !track.id || track.available===false
            || track.videoId!==music.videoId) return;
        var owner={album:album,index:index};
        entries.push({at:entry.addedAt,order:order,item:{id:track.id,kind:"songs",title:track.title || "",
            artist:track.artist || values(track.artists || album.artists).join(", "),
            seconds:track.seconds,cover:album.cover || "",record:album,trackIndex:index,
            albumId:album.id,trackId:track.id,videoId:music.videoId,
            libraryAction:"openDownloadedSong",libraryItem:owner}});
    });
    entries.sort(function(a,b){return b.at-a.at || a.order-b.order;});
    return entries.map(function(entry){return entry.item;});
}

// Inputs are existing owner projections, already bound to the current profile:
// personalLibrary.homeCollections/availableSongs, session.feedbackState(), and
// recommendationService.historySnapshot (HistoryStore's newest-first chronology),
// and DownloadStore.downloadedMusic(currentProfileScope).
// Dispatch stays with the caller: openPlaylist takes libraryItem.id; the other
// named PersonalLibraryData methods take libraryItem itself.
function project(personalLibrary,feedbackState,historySnapshot,downloadedMusic) {
    if(!personalLibrary || personalLibrary.personalEnabled===false) return [];
    var owned=personalLibrary.homeCollections || {};
    var songs=values(personalLibrary.availableSongs).concat(values(owned.songs));
    var positive=positiveRecordings(feedbackState), seen={}, liked=[];
    songs.forEach(function(song) {
        var key=providerRecording(song);
        if(key && positive.indexOf(key)>=0 && !seen[key]) {
            seen[key]=true;liked.push(card(song,"songs","openSong"));
        }
    });
    var recent=[];
    if(historySnapshot && historySnapshot.ok===true && historySnapshot.enabled===true) {
        values(historySnapshot.chronology).forEach(function(entry) {
            if(entry.kind!=="music") return;
            var song=songs.find(function(item) { return occurrence(item)===entry.id; });
            if(song) recent.push(card(song,"songs","openSong",{historyRecord:entry}));
        });
    }
    var rows=[
        {key:"playlists",title:qsTr("Playlists"),items:values(owned.playlists).map(function(item){return card(item,"playlists","openPlaylist");})},
        {key:"saved-albums",title:qsTr("Saved albums"),items:values(owned.albums).map(function(item){return card(item,"albums","activateAlbum");})},
        {key:"liked-songs",title:qsTr("Liked songs"),items:liked},
        {key:"followed-artists",title:qsTr("Followed artists"),items:values(owned.artists).map(function(item){return card(item,"artists","activateArtist");})},
        {key:"downloads",title:qsTr("Downloads"),items:downloadCards(downloadedMusic)},
        {key:"recently-played",title:qsTr("Recently played"),items:recent}
    ];
    return rows.filter(function(row){return row.items.length>0;});
}
