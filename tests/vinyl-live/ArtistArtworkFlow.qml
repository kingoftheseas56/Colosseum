import QtQuick
import "../../qml"

// Normal Main.qml and native registrations. live/offline qualify and quit;
// review leaves the ordinary world interactive for Hemanth.
Main {
    id:root
    property string stage:"profile"
    property int ticks:0
    property int stageTick:0
    property string firstPhoto:""
    property string pinnedPhoto:""
    property string albumId:""
    readonly property bool offline:Qt.application.arguments[2]==="offline"
    readonly property bool review:Qt.application.arguments[2]==="review"
    readonly property string directory:Qt.application.arguments[3]
    function fail(message){console.error("VINYL_ARTWORK_FAIL",stage,message);cycle.stop();Qt.exit(19);}
    function next(value){stage=value;stageTick=ticks;console.log("VINYL_ARTWORK_STAGE",stage);}
    function capture(name,target){next("capturing");if(!contentItem.grabToImage(function(image){if(!image.saveToFile(directory+"/"+Qt.application.arguments[2]+"-"+name+".png")){fail("capture");return;}next(target);}))fail("capture request");}
    function named(item,name){if(item.objectName===name)return item;var children=item.children || [];for(var i=0;i<children.length;i++){var found=named(children[i],name);if(found)return found;}return null;}
    Timer {
        id:cycle;interval:200;repeat:true;running:true
        onTriggered:{
            if(++root.ticks>1200){root.fail("deadline");return;}
            if(root.stage==="profile"){
                if(root.ticks<5||AccountController.busy)return;
                if(AccountController.onboardingRequired){AccountController.continueWithoutAccount();return;}
                root.openWorld("Vinyl");root.next("world");return;
            }
            var w=root.vinylWorld;if(!w)return;
            var source=w.session.source,page=root.named(w,"vinylArtistPage"),hero=page.heroItem;
            if(root.stage==="world"){
                w.showArtist({id:"ytm:artist:UCedvOgsKFzcK3hA5taf3KoQ",name:"Eminem"});root.next("photos");return;
            }
            if(root.stage==="photos"&&!source.artistBusy){
                if(root.named(w,"vinylArtistBackdropsButton")||root.named(w,"vinylArtistBackdropsDialog")){root.fail("removed photo controls still exist");return;}
                if(hero.eligiblePhotoCount!==4){root.fail("expected four real photos: "+hero.eligiblePhotoCount+" "+source.artistError);return;}
                if(!hero.portraitReady||hero.pendingVisualLayer>=0)return;
                if(root.offline&&!source.cached){root.fail("offline metadata not labelled");return;}
                root.firstPhoto=String(hero.currentSynchronizedSource);
                if(root.firstPhoto.indexOf("/artist/fanart/")<0){root.fail("album art substituted");return;}
                console.log("VINYL_ARTWORK_FOUR_OK",JSON.stringify(source.artistArtwork));
                if(root.review){page.openSearchDialog(root.named(w,"vinylArtistSearchButton"));root.next("review-search");return;}
                root.capture("first","rotation");return;
            }
            if(root.stage==="rotation"){
                if(String(hero.currentSynchronizedSource)===root.firstPhoto||hero.pendingVisualLayer>=0)return;
                console.log("VINYL_ARTWORK_ROTATION_OK",hero.slideshowIndex,hero.currentSynchronizedSource);
                root.capture("rotated","pin");return;
            }
            if(root.stage==="review-search"&&root.ticks-root.stageTick>5){root.capture("search","hold");return;}
            if(root.stage==="pin"){
                root.pinnedPhoto=hero.eligiblePhotos[2].cover;hero.setBackdrop(hero.eligiblePhotos[2].id);root.next("pinned");return;
            }
            if(root.stage==="pinned"){
                if(!hero.portraitReady||hero.pendingVisualLayer>=0||String(hero.currentSynchronizedSource)!==root.pinnedPhoto)return;
                if(hero.slideshowEnabled||hero.rotationMode!=="pinned"){root.fail("pin state");return;}
                console.log("VINYL_ARTWORK_PIN_OK",hero.currentSynchronizedSource);hero.setBackdrop("auto");root.capture("auto","singles");return;
            }
            if(root.stage==="singles"){page.switchTab("singles",true);root.next("singles-ready");return;}
            if(root.stage==="singles-ready"&&root.ticks-root.stageTick>5){root.capture("singles","album-open");return;}
            if(root.stage==="album-open"){
                if(!source.artistAlbums.length){root.fail("missing catalogue album");return;}
                root.albumId=source.artistAlbums[0].id;w.openAlbum(root.albumId);root.next("album");return;
            }
            if(root.stage==="album"&&!w.session.browsing){
                if(w.page!=="album"||!w.session.tracks.length){root.fail("album route "+w.session.browseError);return;}
                console.log("VINYL_ARTWORK_ALBUM_OK",w.session.shownEdition.name,w.session.tracks.length);
                root.capture("album","song-open");return;
            }
            if(root.stage==="song-open"){
                root.named(w,"vinylAlbumPage").songRequested(0);root.next("song");return;
            }
            if(root.stage==="song"){
                if(w.page!=="song"||!w.browsingSong||!root.named(w,"vinylSongPage").songTitle){root.fail("song route");return;}
                console.log("VINYL_ARTWORK_SONG_OK",root.named(w,"vinylSongPage").songTitle);root.capture("song","back");return;
            }
            if(root.stage==="back"){
                w.goBack();if(w.page!=="album"){root.fail("song back");return;}w.goBack();
                if(w.page!=="artist"||hero.eligiblePhotoCount!==4){root.fail("artist return");return;}
                console.log("VINYL_ARTWORK_FLOW_OK",root.offline?"offline":"live");cycle.stop();Qt.quit();return;
            }
            if(root.stage==="hold"){
                page.contentY=0;console.log("VINYL_ARTWORK_REVIEW_READY");cycle.stop();
            }
        }
    }
}
