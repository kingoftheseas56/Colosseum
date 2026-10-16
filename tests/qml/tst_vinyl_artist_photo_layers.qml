import QtQuick
import QtTest
TestCase {
    name: "PhotoLayerRepair"
    when: windowShown
    property var host
    property var hero
    function init() {
        let c = Qt.createComponent("../../qml/vinyl/ArtistPreview.qml");
        verify(c.status === Component.Ready, c.errorString());
        host = c.createObject(null);
        verify(host !== null);
        tryCompare(host, "ready", true);
        hero = findChild(host.contentItem, "vinylArtistHero");
        verify(hero !== null);
        host.artistData.reducedMotion = true;
        tryCompare(hero, "portraitReady", true);
    }
    function cleanup() { host.close(); host.destroy(); }
    function photo(name) {
        return {cover: "artist-media/eminem-" + name + ".jpg", focalX: 0.56, focalY: 0.4};
    }
    function test_realFourTwoOneZeroPhotos() {
        compare(hero.eligiblePhotoCount, 4);
        let seen = [String(hero.currentSynchronizedSource)];
        for (let i=0;i<3;++i) {
            let next = hero.activeVisualLayer === 0 ? 1 : 0;
            hero.advanceSlideshow();
            tryCompare(hero,"activeVisualLayer",next);
            verify(seen.indexOf(String(hero.currentSynchronizedSource)) < 0);
            seen.push(String(hero.currentSynchronizedSource));
        }
        host.artistData.overrideHeroPhotos = [photo("hood"), photo("early")];
        compare(hero.rotationMode,"dual-crossfade");
        tryCompare(hero,"activeVisualLayer",0);
        let first=hero.currentSynchronizedSource;
        hero.advanceSlideshow();
        tryCompare(hero,"activeVisualLayer",1);
        verify(first !== hero.currentSynchronizedSource);
        host.artistData.overrideHeroPhotos = [photo("hood")];
        compare(hero.rotationMode,"static");
        verify(!hero.slideshowEnabled);
        host.artistData.overrideHeroPhotos = [];
        compare(hero.rotationMode,"fallback");
        verify(!hero.slideshowEnabled);
    }
    function test_skinReadinessGatesCommit() {
        hero.skinLayerBReady = false;
        hero.transitionToPhoto(photo("hood"), false);
        compare(hero.activeVisualLayer, 0);
        compare(hero.pendingVisualLayer, 1);
        hero.skinLayerBReady = true;
        tryCompare(hero, "activeVisualLayer", 1);
        compare(hero.pendingVisualLayer, -1);
    }
    function test_latestRequestWinsBeforeReady() {
        hero.skinLayerBReady = false;
        hero.transitionToPhoto(photo("hood"), false);
        hero.transitionToPhoto(photo("early"), false);
        hero.skinLayerBReady = true;
        tryCompare(hero, "activeVisualLayer", 1);
        verify(String(hero.currentSynchronizedSource).endsWith("eminem-early.jpg"));
    }
    function test_failedLoadKeepsCurrentImage() {
        let previous = hero.currentSynchronizedSource;
        ignoreWarning(/.*Cannot open.*missing-photo-for-test.jpg/);
        ignoreWarning(/.*Cannot open.*missing-photo-for-test.jpg/);
        ignoreWarning(/.*Cannot open.*missing-photo-for-test.jpg/);
        hero.transitionToPhoto({cover:"artist-media/missing-photo-for-test.jpg"}, false);
        wait(30);
        compare(hero.activeVisualLayer, 0);
        compare(hero.currentSynchronizedSource, previous);
        hero.transitionToPhoto(photo("hood"), false);
        tryCompare(hero, "activeVisualLayer", 1);
    }
    function test_fallbackRetainsBindingsAndSkinURL() {
        ignoreWarning(/.*Cannot open.*missing-photo-for-test.jpg/);
        ignoreWarning(/.*Cannot open.*missing-photo-for-test.jpg/);
        ignoreWarning(/.*Cannot open.*missing-photo-for-test.jpg/);
        hero.transitionToPhoto({cover:"artist-media/missing-photo-for-test.jpg",fallbackCover:"artist-media/eminem-early.jpg"}, false);
        tryCompare(hero, "activeVisualLayer", 1);
        verify(String(hero.layerBSource).endsWith("eminem-early.jpg"));
        compare(findChild(host.contentItem, "vinylArtistWallpaper").artistLayerBSource, hero.layerBSource);
        hero.transitionToPhoto(photo("hood"), false);
        tryCompare(hero, "activeVisualLayer", 0);
        hero.transitionToPhoto(photo("signature"), false);
        tryCompare(hero, "activeVisualLayer", 1);
        compare(findChild(hero,"vinylArtistHeroImgB").source,hero.layerBSource);
    }
    function test_focalCropAndHiddenPause() {
        hero.transitionToPhoto(photo("hood"), false);
        tryCompare(hero,"activeVisualLayer",1);
        let img = findChild(hero,"vinylArtistHeroImgB");
        compare(img.x, (hero.width-img.width)*0.56);
        compare(img.y, (hero.height-img.height)*0.4);
        host.artistData.reducedMotion = false;
        hero.visible = false;
        compare(findChild(hero,"vinylArtistHeroTimer").running,false);
    }
}
