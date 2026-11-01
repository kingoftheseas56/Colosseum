import QtQuick
import "../../qml"

// Actual Main, shared Content asset resolver and world surfaces. No fake skin.
// Launch from the repository resource root, as normal Main startup does.
Main {
    id: root
    readonly property string evidence: Qt.application.arguments[2]
    property string stage: "profile"
    property int ticks: 0
    property int at: 0
    property var worlds: ["Tankoban", "Biblio", "Theatre", "Vinyl"]
    property var sharedWall: null
    property string sharedSource: ""
    property bool capturing: false
    property var renderedWorld: null
    Image { id: probe; visible: false; source: root.wallpaperSource }
    function fail(reason) { console.error("VINYL_WALLPAPER_FAIL", stage, reason); cycle.stop(); Qt.exit(24); }
    function findWorld(item, medium) {
        if (!item) return null;
        if (item.medium === medium && item.backdrop) return item;
        var children = item.children || [];
        for (var i = 0; i < children.length; ++i) {
            var found = findWorld(children[i], medium);
            if (found) return found;
        }
        return null;
    }
    function capture(name) {
        capturing = true;
        if (!contentItem.grabToImage(function(result) {
            if (!result.saveToFile(evidence + "/" + name + ".png")) { root.fail("capture"); return; }
            root.capturing = false; root.at++; root.ticks = 0;
            if (root.at < root.worlds.length) root.openWorld(root.worlds[root.at]);
        })) fail("capture request");
    }
    Timer {
        id: cycle; running: true; repeat: true; interval: 200
        onTriggered: {
            if (++root.ticks > 300) { root.fail("deadline"); return; }
            if (root.capturing) return;
            if (root.stage === "profile") {
                if (AccountController.busy) return;
                if (AccountController.onboardingRequired) { AccountController.continueWithoutAccount(); return; }
                if (root.shellEscapeState().bootVisible) return;
                if (!root.wallpaperSource) { root.fail("shared asset resolver returned no wallpaper"); return; }
                if (probe.status === Image.Error) { root.fail("shared wallpaper failed to decode"); return; }
                if (probe.status !== Image.Ready) return;
                root.sharedSource = root.wallpaperSource;
                root.openWorld(root.worlds[0]); root.stage = "worlds"; root.ticks = 0; return;
            }
            if (root.stage === "worlds" && root.at < root.worlds.length) {
                if (root.ticks < 12) return;
                var medium = root.worlds[root.at];
                var world = root.findWorld(root.contentItem, medium);
                if (!world) return;
                if (world.lifecycleActive === false) return;
                if (root.renderedWorld !== world) {
                    root.renderedWorld = world; root.ticks = 0;
                    if (medium === "Vinyl") world.showHome();
                    return;
                }
                if (root.ticks < 8) return;
                if (!root.sharedWall) root.sharedWall = world.backdrop;
                if (world.backdrop !== root.sharedWall || !root.sharedWall.visible
                    || root.wallpaperSource !== root.sharedSource || probe.status !== Image.Ready) {
                    root.fail("world does not share the ready shell wallpaper: " + medium); return;
                }
                if (medium === "Vinyl" && root.vinylWorld.chromeBackdrop !== root.sharedWall) {
                    root.fail("Vinyl chrome detached from shell wallpaper"); return;
                }
                console.log("VINYL_WALLPAPER_WORLD_OK", medium, root.sharedSource);
                root.capture(medium.toLowerCase() + "-wallpaper"); return;
            }
            if (root.stage === "worlds") {
                var vinyl = root.vinylWorld;
                vinyl.showHome();
                if (vinyl.chromeBackdrop !== root.sharedWall) { root.fail("Home skin"); return; }
                vinyl.worldView.setTab("discover");
                if (vinyl.chromeBackdrop !== root.sharedWall) { root.fail("Discover skin"); return; }
                vinyl.showLibrary();
                if (vinyl.chromeBackdrop !== root.sharedWall) { root.fail("Library skin"); return; }
                console.log("VINYL_WALLPAPER_PASS"); cycle.stop(); Qt.quit();
            }
        }
    }
}
