import QtQuick
import QtTest
import Colosseum.Vinyl 1.0

TestCase {
    id: test; name: "VinylArtistOnboardingService"; when: windowShown
    property var service: null
    property var prefs: null
    property var source: null
    Component {
        id: prefsFactory
        QtObject {
            property bool vinylTasteAnswered: false
            property var vinylTasteArtists: []
            property bool failWrites: false
            property int writeCalls: 0
            signal vinylTasteChanged()
            function setVinylTasteSelections(answered, artists) {
                writeCalls++;
                if(failWrites)return false;
                vinylTasteAnswered=answered;vinylTasteArtists=artists;vinylTasteChanged();return true;
            }
        }
    }
    Component {
        id: sourceFactory
        QtObject {
            property int serial: 0
            property var calls: []
            readonly property var tasteCalls: calls.filter(function(call){return call.lane==="taste-artists";})
            signal completed(string lane,int id,var data,string error)
            function request(lane,operation,value,options){
                var id=++serial;
                calls=calls.concat([{id:id,lane:lane,operation:operation,query:value,options:options}]);return id;
            }
            function cancel(lane) {}
        }
    }
    function makeArtist(id) {return {id:"ytm:artist:"+id,title:id,cover:""};}
    function page(query,items,cursor,offset) {return {version:1,query:query,kind:"artists",source:"youtube-music",items:items,records:[],offset:offset||0,nextOffset:(offset||0)+items.length,complete:!cursor,nextCursor:cursor||{}};}
    function init() {
        prefs=prefsFactory.createObject(test);source=sourceFactory.createObject(test);
        var c=Qt.createComponent("data:text/plain,import QtQuick; import Colosseum.Vinyl 1.0; VinylRecommendationService {}");
        verify(c.status===Component.Ready,c.errorString());
        service=c.createObject(test,{preferences:prefs,source:source,profileActive:true,profileScope:"local"});verify(service!==null);
    }
    function cleanup(){if(service)service.destroy();prefs.destroy();source.destroy();wait(0);}
    function pickDraft(count) {
        for(var i=0;i<count;++i)service.toggleDraftArtist(makeArtist("UC"+String.fromCharCode(97+i).repeat(22)));
    }
    function test_firstHomeNeedsNoChoicesAndEditCancelKeepsSavedChoices() {
        compare(service.minimumArtists,1);compare(service.minimumArtistCount,1);
        compare(service.onboardingState,"unanswered");
        service.beginHomeVisit();compare(service.onboardingState,"unanswered");
        compare(service.draftArtists.length,0);compare(prefs.writeCalls,0);
        tryVerify(()=>source.calls.some(function(call){return call.operation==="chartCandidates";}));
        service.beginTasteEdit();compare(service.onboardingState,"editing");
        pickDraft(5);compare(service.draftArtists.length,5);
        verify(service.commitTaste(false));compare(service.onboardingState,"answered");compare(prefs.vinylTasteArtists.length,5);
        compare(prefs.writeCalls,1);
        service.beginHomeVisit();compare(service.onboardingState,"answered");compare(prefs.writeCalls,1);
        service.beginTasteEdit();service.toggleDraftArtist(makeArtist("UCffffffffffffffffffffff"));compare(service.draftArtists.length,6);
        service.cancelTasteEdit();compare(service.selectedArtists.length,5);compare(service.onboardingState,"answered");
        compare(prefs.vinylTasteArtists.length,5);compare(prefs.writeCalls,1);
        // Cancelling an edit of an answered profile preserves saved choices without another write.
        service.beginTasteEdit();service.toggleDraftArtist(makeArtist("UC"+"a".repeat(22)));
        service.cancelTasteEdit();compare(service.onboardingState,"answered");
        compare(service.selectedArtists.length,5);compare(prefs.writeCalls,1);
    }

    function test_emptyDraftCannotSaveAndOneArtistWriteCanBeRetried() {
        service.beginHomeVisit();service.beginTasteEdit();
        verify(!service.commitTaste(false),"An empty draft cannot be saved.");
        compare(service.onboardingState,"editing");compare(prefs.writeCalls,0);
        pickDraft(1);compare(service.draftArtists.length,1);
        prefs.failWrites=true;verify(!service.commitTaste(false));compare(service.onboardingState,"editing");
        compare(prefs.writeCalls,1);verify(!prefs.vinylTasteAnswered);compare(prefs.vinylTasteArtists.length,0);
        prefs.failWrites=false;
        verify(service.commitTaste(false));compare(service.onboardingState,"answered");
        compare(service.selectedArtists.length,1);compare(prefs.vinylTasteArtists.length,1);
        compare(prefs.writeCalls,2);
    }

    function test_realPageContractAndPagingKeepDraft() {
        service.beginHomeVisit();service.beginTasteEdit();service.searchArtists("Nas",{});tryVerify(()=>source.tasteCalls.length===1);
        compare(source.tasteCalls[0].operation,"cataloguePage");compare(source.tasteCalls[0].options.kind,"artists");
        source.completed("taste-artists",source.tasteCalls[0].id,page("Nas",[makeArtist("UCaaaaaaaaaaaaaaaaaaaaaa")],{token:"next",offset:1}),"");
        tryCompare(service,"artistResults",service.artistResults);tryVerify(()=>service.artistResults.items.length===1);
        service.toggleDraftArtist(service.artistResults.items[0]);
        service.searchArtists("Nas",service.artistResults.nextCursor);tryVerify(()=>source.tasteCalls.length===2);
        source.completed("taste-artists",source.tasteCalls[1].id,page("Nas",[makeArtist("UCbbbbbbbbbbbbbbbbbbbbbb")],null,1),"");
        tryVerify(()=>service.artistResults.items.length===2);compare(service.draftArtists.length,1);
        service.searchArtists("Other",{});tryVerify(()=>source.tasteCalls.length===3);compare(service.draftArtists.length,1);
    }
    function test_lateProfileAndQueryRepliesAreFenced() {
        service.beginHomeVisit();service.beginTasteEdit();service.searchArtists("Old",{});tryVerify(()=>source.tasteCalls.length===1);
        service.searchArtists("New",{});source.completed("taste-artists",source.tasteCalls[0].id,page("Old",[makeArtist("UCaaaaaaaaaaaaaaaaaaaaaa")]),"");
        wait(20);compare(service.artistResults.items.length,0);
        tryVerify(()=>source.tasteCalls.length===2);service.profileGeneration++;
        source.completed("taste-artists",source.tasteCalls[1].id,page("New",[makeArtist("UCbbbbbbbbbbbbbbbbbbbbbb")]),"");
        wait(20);compare(service.artistResults.items.length,0);compare(service.draftArtists.length,0);
    }
    function test_invalidPaginationIsNotAcceptedAsEmptySuccess() {
        service.beginHomeVisit();service.beginTasteEdit();service.searchArtists("Nas",{});tryVerify(()=>source.tasteCalls.length===1);
        var bad=page("Nas",[]);bad.complete=false;
        source.completed("taste-artists",source.tasteCalls[0].id,bad,"");
        tryVerify(()=>service.artistResults.status!=="loading");compare(service.artistResults.status,"error");
    }
    function test_cursorMustAdvanceWithNumericOffsets() {
        service.beginHomeVisit();service.beginTasteEdit();service.searchArtists("Nas",{});tryVerify(()=>source.tasteCalls.length===1);
        var bad=page("Nas",[],{token:"page-2",offset:0});
        source.completed("taste-artists",source.tasteCalls[0].id,bad,"");
        tryVerify(()=>service.artistResults.status!=="loading");compare(service.artistResults.status,"error");
        service.searchArtists("Nas",{});tryVerify(()=>source.tasteCalls.length===2);
        bad=page("Nas",[]);bad.offset="0";
        source.completed("taste-artists",source.tasteCalls[1].id,bad,"");
        tryVerify(()=>service.artistResults.status!=="loading");compare(service.artistResults.status,"error");
    }
    function test_serviceAvailabilityTracksProfileNotArtistChoices() {
        verify(service.canEnterVinyl);compare(service.onboardingState,"unanswered");
        service.profileActive=false;verify(!service.canEnterVinyl);
        service.beginTasteEdit();compare(service.onboardingState,"unanswered");
        service.profileActive=true;verify(service.canEnterVinyl);
        service.profileScope="";verify(!service.canEnterVinyl);
        service.profileScope="local";verify(service.canEnterVinyl);
        compare(prefs.writeCalls,0);verify(!prefs.vinylTasteAnswered);
    }
    function test_storedMappingStatusSurvivesServiceRead() {
        var artist={key:JSON.stringify(["youtube-music","artist","UCaaaaaaaaaaaaaaaaaaaaaa"]),kind:"artist",provider:"youtube-music",providerId:"UCaaaaaaaaaaaaaaaaaaaaaa",name:"Ambiguous artist",image:"",mappingStatus:"ambiguous"};
        prefs.vinylTasteArtists=[artist];prefs.vinylTasteAnswered=true;prefs.vinylTasteChanged();
        compare(service.selectedArtists.length,1);compare(service.selectedArtists[0].mappingStatus,"ambiguous");
    }
    function test_verifiedUppercaseCanonicalMapStaysVerified() {
        var a={key:JSON.stringify(["youtube-music","artist","UCaaaaaaaaaaaaaaaaaaaaaa"]),kind:"artist",provider:"youtube-music",providerId:"UCaaaaaaaaaaaaaaaaaaaaaa",name:"Mapped",image:"",mappingStatus:"verified",canonicalMbid:"AABBCCDD-1234-4234-8234-AABBCCDDEEFF"};
        prefs.vinylTasteArtists=[a];prefs.vinylTasteAnswered=true;prefs.vinylTasteChanged();
        compare(service.selectedArtists[0].mappingStatus,"verified");
    }
}
