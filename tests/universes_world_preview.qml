// Preview for the Universes world outside the app: the real world page over the default
// wallpaper, with the four seeded installed universes. Mirrors tests/saga_template_preview.qml.
//   qml.exe tests/universes_world_preview.qml -- [--scroll N] [--wait S] [--capture out.png]
import QtQuick
import QtQuick.Window
import "../qml" as App
import "../qml/Universes.js" as UDB

Window {
    id: host
    visible: true
    width: Number(argument("--width", "1280"))
    height: Number(argument("--height", "720"))
    color: "#111213"
    title: "Universes world"
    function argument(key, fallback) {
        const i = Qt.application.arguments.indexOf(key)
        return i >= 0 ? Qt.application.arguments[i + 1] : fallback
    }
    function seeded(extensionId, name) {
        return { extensionId: extensionId, name: name, banner: UDB.configFor(name).banner || "", logo: "" }
    }
    // the app loads these in Main.qml; a standalone preview has to bring its own
    FontLoader { source: "../assets/fonts/Fraunces-Regular.ttf" }

    Image { id: wall; anchors.fill: parent; source: "../assets/wallpaper/cold-ripple.jpg"
            fillMode: Image.PreserveAspectCrop }
    App.UniversesWorld {
        id: world
        anchors.fill: parent
        backdrop: wall
        installed: [ host.seeded("com.colosseum.universe.onepiece", "One Piece"),
                     host.seeded("com.colosseum.universe.cosmere", "Cosmere"),
                     host.seeded("com.colosseum.universe.dcau", "DC Animated Universe"),
                     host.seeded("com.colosseum.universe.starwars", "Star Wars") ]
        onUniverseRequested: (entry) => console.log("[universes-preview] UNIVERSE", entry.name, entry.extensionId || entry.page)
        onTributeRequested: (entry) => console.log("[universes-preview] TRIBUTE", entry.name)
    }
    readonly property int waitMs: Number(argument("--wait", "12")) * 1000
    Timer {
        interval: host.waitMs - 2000
        running: Number(host.argument("--scroll", "0")) > 0
        onTriggered: world.pageFlickable.contentY = Number(host.argument("--scroll", "0"))
    }
    Timer {
        interval: host.waitMs
        running: host.argument("--capture", "").length > 0
        onTriggered: world.grabToImage(function(result) {
            const saved = result.saveToFile(host.argument("--capture", ""))
            console.log("[universes-preview] capture", saved, "tributes", world.tributes.length, "universes", world.roster.length)
            Qt.exit(saved ? 0 : 1)
        })
    }
}
