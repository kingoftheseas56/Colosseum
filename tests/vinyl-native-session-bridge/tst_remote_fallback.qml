import QtQuick
import QtTest

// Actual native MpvItem failure and replacement, with deterministic transport
// replies. Public YouTube playback is qualified separately in Main.qml.
TestCase {
    id: test
    name: "VinylNativeRemoteFallback"
    when: windowShown
    property var session
    property var source
    property string wave
    Component {
        id: transport
        QtObject {
            property int serial: 0
            property var calls: []
            property string torrentUrl: "http://127.0.0.1:1/unreachable-audio.mp3"
            property string fallbackUrl: ""
            signal completed(string lane,int requestId,var data,string error)
            signal profileLeaving()
            signal profileEntered()
            signal checkpointRequested()
            signal shuttingDown()
            function setAlbums(entries) {}
            function cancel(lane: string) {}
            function request(lane: string,operation: string,value: string): int {
                var id=++serial;
                calls=calls.concat([{operation:operation,track:value}]);
                Qt.callLater(function(){
                    completed(lane,id,{trackId:value,
                        url:operation==="resolve" ? torrentUrl : (fallbackUrl || test.wave),
                        provider:operation==="resolve" ? "torrent" : "youtube",qualityLabel:"Contract audio"},"");
                });
                return id;
            }
        }
    }
    function init() {
        var path=BridgeFixtures.createIdentityAudio();BridgeFixtures.changeIdentityAudio(path,16000);
        wave="file:///"+path.replace(/\\/g,"/");
        source=transport.createObject(test);
        var component=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");
        compare(component.status,Component.Ready,component.errorString());
        session=component.createObject(test,{source:source});verify(session!==null);
        session.shownEdition=session.normalize({id:"remote-album",browseId:"remote-album",origin:"youtube-music",title:"Album",artists:["Artist"],tracks:[{id:"remote-track",videoId:"abcdefghijk",title:"Song",artists:["Artist"],seconds:2,available:true}]});
    }
    function cleanup(){session.destroy();wait(0);source.destroy();wait(0);}
    function test_tagsAreCheckedAgainstCanonicalQueue_data(){
        return [{tag:"matching",title:"Song",artist:"Artist",album:"Album",expected:"torrent"},
            {tag:"wrong-title",title:"Another Song",artist:"Artist",album:"Album",expected:"youtube"},
            {tag:"wrong-artist",title:"Song",artist:"Another Artist",album:"Album",expected:"youtube"},
            {tag:"wrong-album",title:"Song",artist:"Artist",album:"Another Album",expected:"youtube"}];
    }
    function test_tagsAreCheckedAgainstCanonicalQueue(data){
        source.torrentUrl="file:///"+BridgeFixtures.createTaggedAudio(data.title,data.artist,data.album).replace(/\\/g,"/");
        session.playTrack(0);session.paused=true;
        tryVerify(function(){return session.loaded;},15000);
        compare(session.error,"");compare(session.sourceKind,data.expected);
        compare(source.calls.length,data.expected==="torrent"?1:2);
    }
    function test_realDecoderFailureFallsBackOnceAndPreservesPauseAndOccurrence() {
        session.playTrack(0);session.paused=true;
        var occurrence=session.queue[0].queueId;
        tryVerify(function(){return session.loaded;},15000);
        compare(session.error,"");compare(session.sourceKind,"youtube");verify(session.fallbackReason.length>0);
        compare(source.calls.length,2);compare(source.calls[0].operation,"resolve");compare(source.calls[1].operation,"resolveFallback");
        compare(source.calls[0].track,source.calls[1].track);compare(session.queue[0].queueId,occurrence);compare(session.queueIndex,0);
        verify(session.paused);session.paused=false;
        tryVerify(function(){return session.position>0.2;},5000);
        compare(source.calls.length,2);
        session.clearProfile();compare(session.sourceKind,"");compare(session.fallbackReason,"");
    }
    function test_failedFallbackDoesNotLoop(){
        source.fallbackUrl=source.torrentUrl;
        session.playTrack(0);
        tryVerify(function(){return session.error.length>0;},15000);
        compare(source.calls.length,2);compare(session.sourceKind,"youtube");
        wait(400);compare(source.calls.length,2);
    }
}
