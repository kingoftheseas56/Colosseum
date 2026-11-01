import QtQuick
import "../../qml"

// Production Main, profile preferences and catalogue transport. Args: phase,
// evidence directory. One isolated profile for done/restore/inspect; another
// for skip/skip-restore. Semantic handlers; no OS pointer automation claim.
Main {
    id: root
    readonly property string phase: Qt.application.arguments[2]
    readonly property string evidence: Qt.application.arguments[3]
    property string stage: "profile"
    property int ticks: 0
    property var expected: []
    function fail(message){console.error("VINYL_ONBOARDING_FAIL",stage,message);cycle.stop();Qt.exit(21);}
    function check(ok,message){if(!ok)fail(message);return !!ok;}
    function next(value){stage=value;ticks=0;console.log("VINYL_ONBOARDING_STAGE",value);}
    function find(item,name){if(!item)return null;if(item.objectName===name && item.visible!==false)return item;var children=item.children||[];for(var i=0;i<children.length;i++){var c=find(children[i],name);if(c)return c;}return null;}
    function activate(name){var item=find(contentItem,name);if(!check(item&&item.enabled,"Missing action "+name))return false;if(typeof item.activate==="function")item.activate(Qt.MouseFocusReason);else item.triggered();return true;}
    function capture(name,target){next("capture");contentItem.grabToImage(result=>{if(!result.saveToFile(evidence+"/"+name+".png")){root.fail("capture");return;}root.next(target);});}
    Timer {
        id:cycle;interval:200;repeat:true;running:true
        onTriggered:{
            if(++root.ticks>600){root.fail("Stage deadline");return;}
            if(root.stage==="profile"){
                if(AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                if(root.shellEscapeState().bootVisible)return;
                root.openWorld("Vinyl");root.next("world");return;
            }
            var w=root.vinylWorld;if(!w)return;var s=w.recommendationService;
            if(root.stage==="world"&&root.ticks>6){
                w.showHome();
                if(root.phase==="restore"||root.phase==="inspect"){
                    if(!root.check(s.onboardingState==="answered"&&s.selectedArtists.length===2,"Done preferences did not persist"))return;
                    if(!root.check(s.selectedArtists.some(a=>a.name==="Nas")&&s.selectedArtists.some(a=>a.name==="Ilaiyaraaja"),"Artist identity changed on reload"))return;
                    console.log("VINYL_ONBOARDING_RESTORE",JSON.stringify(s.selectedArtists));
                    if(root.phase==="inspect"){root.next("inspection-settle");return;}
                    root.activate("vinylTasteEdit");root.next("editing");return;
                }
                if(root.phase==="skip-restore"){
                    if(!root.check(s.onboardingState==="answered"&&s.selectedArtists.length===0,"Skip answer did not persist"))return;
                    root.next("pass");return;
                }
                if(!root.check(s.onboardingState==="draft"&&!ProfilePreferences.vinylTasteAnswered,"Fresh Home did not offer picker"))return;
                if(root.phase==="skip") {root.capture("first-home-offline","skip");return;}
                root.next("suggestions");return;
            }
            if(root.stage==="suggestions"){
                if(s.artistResults.status==="loading")return;
                if(!root.check(s.artistResults.status==="ready"&&s.artistResults.items.length>0,"Public artist suggestions did not load: "+s.artistResults.error))return;
                console.log("VINYL_ONBOARDING_SUGGESTIONS",s.artistResults.items.length);
                root.capture("first-home-catalogue","search-nas");return;
            }
            if(root.stage==="search-nas"){w.enterDiscoverSearch(true,false);w.setQuery("Nas");root.next("nas");return;}
            if(root.stage==="skip"){root.activate("vinylPickerSkip");if(!root.check(s.onboardingState==="answered"&&ProfilePreferences.vinylTasteAnswered,"Skip did not save"))return;root.next("pass");return;}
            if(root.stage==="nas"||root.stage==="ilaiyaraaja"){
                if(s.artistResults.status==="loading")return;
                if(s.artistResults.status==="error"){root.fail(s.artistResults.error);return;}
                var name=root.stage==="nas"?"Nas":"Ilaiyaraaja",rows=s.artistResults.items||[],at=rows.findIndex(a=>a.name===name);
                if(!root.check(at>=0,"Real search did not return "+name))return;
                if(!root.check(rows[at].provider==="youtube-music"&&rows[at].providerId.indexOf("UC")===0,"Not a real catalogue artist"))return;
                root.activate("vinylPickerArtist_"+at);
                console.log("VINYL_ONBOARDING_SELECTED",JSON.stringify(rows[at]));
                if(root.stage==="nas"){w.setQuery("Ilaiyaraaja");root.next("ilaiyaraaja");return;}
                if(!root.check(s.draftArtists.length===2,"Query change lost selection"))return;
                root.expected=s.draftArtists.map(a=>a.key);w.showLibrary();w.showHome();
                if(!root.check(s.draftArtists.length===2&&s.onboardingState==="draft","Leaving Home lost draft"))return;
                root.capture("selected-artists","done");return;
            }
            if(root.stage==="done"){
                root.activate("vinylPickerDone");
                if(!root.check(s.onboardingState==="answered"&&ProfilePreferences.vinylTasteArtists.length===2,"Done did not save typed preferences: "+s.error))return;
                if(!root.check(ProfilePreferences.vinylTasteArtists.map(a=>a.key).join("|")===root.expected.join("|"),"Saved artist references changed"))return;
                root.capture("answered-home","open-artist");return;
            }
            if(root.stage==="open-artist") {root.activate("vinylCard_taste-artists_0");root.next("artist");return;}
            if(root.stage==="artist"&&root.ticks>3){
                if(!root.check(w.page==="artist","Your artists card did not open Artist"))return;
                w.goBack();if(!root.check(w.page==="home"&&s.onboardingState==="answered","Artist Back lost answered Home"))return;
                root.next("pass");return;
            }
            if(root.stage==="editing"){
                if(!root.check(s.onboardingState==="editing","Edit did not open"))return;
                s.toggleDraftArtist(s.draftArtists[0]);root.activate("vinylPickerCancel");
                if(!root.check(s.selectedArtists.length===2&&s.onboardingState==="answered","Cancel erased saved choices"))return;
                root.next("pass");return;
            }
            if(root.stage==="inspection-settle"&&root.ticks>30){root.capture("home-open-for-review","hold");return;}
            if(root.stage==="hold"){console.log("VINYL_ONBOARDING_PASS",root.phase);cycle.stop();return;}
            if(root.stage==="pass"){console.log("VINYL_ONBOARDING_PASS",root.phase);cycle.stop();Qt.quit();}
        }
    }
}
