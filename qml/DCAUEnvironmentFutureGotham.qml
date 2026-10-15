import QtQuick
import "UniverseContentApi.js" as UniverseContent

Item {
    id: root
    function resolveContentAsset(path) { root.universeContentGeneration; return UniverseContent.asset(path) }
    readonly property int universeContentGeneration: typeof Content !== "undefined" ? Content.generation : 0
    objectName: "dcauEnvironmentFutureGothamV11"
    property bool reducedMotion: false

    // v11 is the visual source of truth. This preserves its megastructure canyon,
    // suspended rail, neon signs, scan beams, moon, haze, and skyline depth.
    Image {
        id: futureGothamV11Scene
        anchors.fill: parent
        source: root.resolveContentAsset("../assets/universes/dcau/environments/future-v11.svg")
        fillMode: Image.Stretch
        sourceSize.width: 1600
        sourceSize.height: 900
        smooth: true
        mipmap: true
        cache: true
    }
}
