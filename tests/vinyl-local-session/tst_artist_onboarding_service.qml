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
            signal vinylTasteChanged()
            function setVinylTasteSelections(answered, artists) {
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
            signal completed(string lane,int id,var data,string error)
            function request(lane,operation,value,options){calls=calls.concat([{lane:lane,operation:operation,query:value,options:options}]);return ++serial;}
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
    function test_firstHomeAndCancelEdit() {
        compare(service.onboardingState,"unanswered");service.beginHomeVisit();compare(service.onboardingState,"draft");
        service.toggleDraftArtist(makeArtist("UCaaaaaaaaaaaaaaaaaaaaaa"));compare(service.draftArtists.length,1);
        verify(service.commitTaste(false));compare(service.onboardingState,"answered");compare(prefs.vinylTasteArtists.length,1);
        service.beginHomeVisit();compare(service.onboardingState,"answered");
        service.beginTasteEdit();service.toggleDraftArtist(makeArtist("UCbbbbbbbbbbbbbbbbbbbbbb"));compare(service.draftArtists.length,2);
        service.cancelTasteEdit();compare(service.selectedArtists.length,1);compare(service.onboardingState,"answered");
    }
    function test_skipWithoutNetworkAndFailedCommit() {
        service.beginHomeVisit();prefs.failWrites=true;verify(!service.commitTaste(false));compare(service.onboardingState,"draft");
        prefs.failWrites=false;verify(service.commitTaste(true));verify(prefs.vinylTasteAnswered);compare(service.selectedArtists.length,0);
    }
    function test_realPageContractAndPagingKeepDraft() {
        service.beginHomeVisit();service.searchArtists("Nas",{});tryCompare(source,"serial",1);
        compare(source.calls[0].operation,"cataloguePage");compare(source.calls[0].options.kind,"artists");
        source.completed("taste-artists",1,page("Nas",[makeArtist("UCaaaaaaaaaaaaaaaaaaaaaa")],{token:"next",offset:1}),"");
        tryCompare(service,"artistResults",service.artistResults);tryVerify(()=>service.artistResults.items.length===1);
        service.toggleDraftArtist(service.artistResults.items[0]);
        service.searchArtists("Nas",service.artistResults.nextCursor);tryCompare(source,"serial",2);
        source.completed("taste-artists",2,page("Nas",[makeArtist("UCbbbbbbbbbbbbbbbbbbbbbb")],null,1),"");
        tryVerify(()=>service.artistResults.items.length===2);compare(service.draftArtists.length,1);
        service.searchArtists("Other",{});tryCompare(source,"serial",3);compare(service.draftArtists.length,1);
    }
    function test_lateProfileAndQueryRepliesAreFenced() {
        service.beginHomeVisit();service.searchArtists("Old",{});tryCompare(source,"serial",1);
        service.searchArtists("New",{});source.completed("taste-artists",1,page("Old",[makeArtist("UCaaaaaaaaaaaaaaaaaaaaaa")]),"");
        wait(20);compare(service.artistResults.items.length,0);
        tryCompare(source,"serial",2);service.profileGeneration++;
        source.completed("taste-artists",2,page("New",[makeArtist("UCbbbbbbbbbbbbbbbbbbbbbb")]),"");
        wait(20);compare(service.artistResults.items.length,0);compare(service.draftArtists.length,0);
    }
    function test_invalidPaginationIsNotAcceptedAsEmptySuccess() {
        service.beginHomeVisit();service.searchArtists("Nas",{});tryCompare(source,"serial",1);
        var bad=page("Nas",[]);bad.complete=false;
        source.completed("taste-artists",1,bad,"");
        tryVerify(()=>service.artistResults.status!=="loading");compare(service.artistResults.status,"error");
    }
    function test_cursorMustAdvanceWithNumericOffsets() {
        service.beginHomeVisit();service.searchArtists("Nas",{});tryCompare(source,"serial",1);
        var bad=page("Nas",[],{token:"page-2",offset:0});
        source.completed("taste-artists",1,bad,"");
        tryVerify(()=>service.artistResults.status!=="loading");compare(service.artistResults.status,"error");
        service.searchArtists("Nas",{});tryCompare(source,"serial",2);
        bad=page("Nas",[]);bad.offset="0";
        source.completed("taste-artists",2,bad,"");
        tryVerify(()=>service.artistResults.status!=="loading");compare(service.artistResults.status,"error");
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
