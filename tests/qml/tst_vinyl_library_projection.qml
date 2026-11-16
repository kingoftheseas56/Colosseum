import QtQuick
import QtTest
import "../../qml/vinyl/VinylLibraryProjection.js" as Projection

TestCase {
    name:"VinylLibraryProjection"
    function song(id,video,local) {
        var row={albumId:"album",editionId:"edition",discIndex:0,trackIndex:id,trackId:"track-"+id,
            track:{id:"track-"+id,videoId:video},edition:{origin:local ? "local" : "youtube-music"}};
        return {id:"song-"+id,title:"Song "+id,artist:"Artist",row:row,album:{id:"album"},index:id,editionId:"edition"};
    }
    function library() {
        return {homeCollections:{playlists:[{id:"playlist",title:"Owner playlist"}],
            albums:[{id:"album",title:"Owner album",albumId:"album",album:{id:"album",origin:"local"}}],
            artists:[{id:"ytm:artist:fixture",artistId:"ytm:artist:fixture",title:"Followed"}],songs:[song(2,"VID00000002",false)]},
            availableSongs:[song(0,"VID00000000",false),song(1,"VID00000001",false),song(2,"VID00000002",false),song(3,"",true)]};
    }
    function feedback(entries) {
        return {ok:true,snapshot:JSON.stringify({schema:"colosseum.vinyl.feedback-taste-state",version:3,profileScopeId:"fixture",entities:entries})};
    }
    function positive(video,extra) {
        return Object.assign({identity:"provider",kind:"recording",providerId:"youtube-music",itemId:video,
            explicit:{feedback:"positive",revision:"1",changedAtMs:100,providerFeedbackRef:null}},extra || {});
    }
    function history(item,at) {return {kind:"music",id:Projection.occurrence(item),lastActivityAt:at};}
    function row(rows,key) {return rows.find(function(item){return item.key===key;});}
    function test_fixedOrderEmptyRowsAndOwnerActions() {
        var lib=library(), source=JSON.stringify(lib);
        var recent={ok:true,enabled:true,chronology:[history(lib.availableSongs[1],200)]};
        var rows=Projection.project(lib,feedback([positive("VID00000000")]),recent);
        compare(rows.map(function(item){return item.key;}).join(","),"playlists,saved-albums,liked-songs,followed-artists,recently-played");
        compare(row(rows,"downloads"),undefined);
        for(var action of [["playlists","openPlaylist"],["saved-albums","activateAlbum"],["followed-artists","activateArtist"],["liked-songs","openSong"],["recently-played","openSong"]])
            compare(row(rows,action[0]).items[0].libraryAction,action[1]);
        verify(row(rows,"playlists").items[0].libraryItem===lib.homeCollections.playlists[0]);
        verify(row(rows,"liked-songs").items[0].libraryItem===lib.availableSongs[0]);
        compare(JSON.stringify(lib),source);
        compare(Projection.project({homeCollections:{},availableSongs:[]},feedback([]),recent).length,0);
    }
    function test_savedDoesNotMeanLikedAndFeedbackMustBindExactRecording() {
        var lib=library();
        var entities=[positive("VID00000000"),positive("VID00000001",{explicit:{feedback:"negative"}}),
            positive("VID00000002",{explicit:{feedback:"neutral"}}),positive("VID00000002",{kind:"artist"}),
            positive("VID00000002",{identity:"canonical",mbid:"11111111-1111-4111-8111-111111111111"})];
        var rows=Projection.project(lib,feedback(entities),{});
        compare(row(rows,"liked-songs").items.length,1);
        compare(row(rows,"liked-songs").items[0].id,"song-0");
        compare(row(Projection.project(lib,{ok:false,snapshot:feedback(entities).snapshot},{}),"liked-songs"),undefined);
        compare(row(Projection.project(lib,{ok:true,snapshot:"broken"},{}),"liked-songs"),undefined);
    }
    function test_explicitBoundProviderEvidenceSupportsCanonicalAndLocalFeedback() {
        var lib=library();
        var canonical={identity:"canonical",kind:"recording",mbid:"11111111-1111-4111-8111-111111111111",
            explicit:{feedback:"positive",providerFeedbackRef:{providerId:"youtube-music",itemId:"VID00000000"}}};
        var local={identity:"provider",kind:"recording",providerId:"local",itemId:"track-3",explicit:{feedback:"positive"}};
        var rows=Projection.project(lib,feedback([canonical,local,positive("VID00000000")]),{});
        compare(row(rows,"liked-songs").items.map(function(item){return item.id;}).join(","),"song-0,song-3");
    }
    function test_recentChronologyRetainsOwnerOrderAndExactOccurrence() {
        var lib=library(), a=lib.availableSongs[0], b=lib.availableSongs[1];
        var chronology=[history(b,300),{kind:"video",id:Projection.occurrence(a),lastActivityAt:250},
            {kind:"music",id:JSON.stringify(["wrong","edition",0,0,"track-0"]),lastActivityAt:220},history(a,100)];
        var state={ok:true,enabled:true,chronology:chronology};
        var rows=Projection.project(lib,feedback([]),state),recent=row(rows,"recently-played").items;
        compare(recent.map(function(item){return item.id;}).join(","),"song-1,song-0");
        verify(recent[0].historyRecord===chronology[0]);
        compare(recent[0].libraryItem.index,1);compare(recent[0].libraryItem.editionId,"edition");
        state.enabled=false;compare(row(Projection.project(lib,feedback([]),state),"recently-played"),undefined);
        state.enabled=true;state.ok=false;compare(row(Projection.project(lib,feedback([]),state),"recently-played"),undefined);
    }
    function test_libraryFilteringAndImportsCannotInventDownloadsOrLikes() {
        var lib=library();lib.albums=[];lib.savedSongs=[];lib.artists=[];lib.playlists=[];
        lib.downloads=[{id:"not-a-proven-music-download"}];
        var rows=Projection.project(lib,feedback([]),{});
        compare(row(rows,"saved-albums").items.length,1);
        compare(row(rows,"downloads"),undefined);compare(row(rows,"liked-songs"),undefined);
        compare(row(rows,"recently-played"),undefined);
        lib.personalEnabled=false;compare(Projection.project(lib,feedback([]),{}).length,0);
    }
    function downloaded(id,at) {
        var video="VID0000000"+id;
        var album={id:"ytm:album:"+id,origin:"youtube-music",title:"Download album "+id,
            artists:["Download artist"],cover:"",tracks:[{id:"original-track-"+id,videoId:video,
                title:"Download song "+id,artist:"Download artist",seconds:180,available:true}]};
        return {id:"queue-job-"+id,kind:"music",music:{album:album,trackIndex:0,videoId:video},
            path:"/owned-fixture/music/"+id+".m4a",bytes:24,addedAt:at,available:true};
    }
    function test_downloadsPreserveOriginalIdentityPayloadAndStableChronology() {
        var a=downloaded(0,100),b=downloaded(1,300),c=downloaded(2,300);
        var input=[a,b,c], before=JSON.stringify(input), lib=library();
        var recent={ok:true,enabled:true,chronology:[history(lib.availableSongs[0],400)]};
        var rows=Projection.project(lib,feedback([positive("VID00000000")]),recent,input);
        compare(rows.map(function(item){return item.key;}).join(","),
            "playlists,saved-albums,liked-songs,followed-artists,downloads,recently-played");
        var songs=row(rows,"downloads").items;
        compare(songs.map(function(item){return item.id;}).join(","),"original-track-1,original-track-2,original-track-0");
        var item=songs[0];
        compare(item.kind,"songs");compare(item.libraryAction,"openDownloadedSong");
        compare(item.albumId,b.music.album.id);compare(item.trackId,b.music.album.tracks[0].id);
        compare(item.videoId,b.music.videoId);compare(item.trackIndex,0);
        verify(item.record===b.music.album);verify(item.libraryItem.album===b.music.album);
        compare(item.libraryItem.index,0);compare(item.title,b.music.album.tracks[0].title);
        compare(item.artist,"Download artist");compare(JSON.stringify(input),before);
        verify(item.id!==b.id); // Queue job identity never replaces the original track.
    }
    function test_downloadsRejectVideoMissingUnavailableAndMismatchedRecords() {
        var good=downloaded(0,100), invalid=[];
        function changed(change) {var entry=JSON.parse(JSON.stringify(good));change(entry);invalid.push(entry);}
        changed(function(e){e.kind="movie";});
        changed(function(e){e.available=false;});
        changed(function(e){e.path="";});
        changed(function(e){delete e.music.album;});
        changed(function(e){e.music.album.origin="local";});
        changed(function(e){delete e.music.album.id;});
        changed(function(e){e.music.album.tracks=[];});
        changed(function(e){e.music.trackIndex="0";});
        changed(function(e){e.music.trackIndex=0.5;});
        changed(function(e){e.music.trackIndex=-1;});
        changed(function(e){e.music.trackIndex=1;});
        changed(function(e){e.music.videoId="invalid";});
        changed(function(e){e.music.album.tracks[0].videoId="VID00000009";});
        changed(function(e){e.music.album.tracks[0].id="";});
        changed(function(e){e.music.album.tracks[0].available=false;});
        changed(function(e){e.addedAt="100";});
        invalid.push(null);
        compare(row(Projection.project(library(),feedback([]),{},invalid),"downloads"),undefined);
        var accepted=row(Projection.project(library(),feedback([]),{},invalid.concat([good])),"downloads").items;
        compare(accepted.length,1);compare(accepted[0].id,good.music.album.tracks[0].id);
    }
    function test_downloadsRemainOptionalAndRespectPersonalGate() {
        var lib=library();
        compare(row(Projection.project(lib,feedback([]),{}),"downloads"),undefined);
        compare(row(Projection.project(lib,feedback([]),{},[]),"downloads"),undefined);
        lib.personalEnabled=false;
        compare(Projection.project(lib,feedback([]),{},[downloaded(0,100)]).length,0);
    }
}
