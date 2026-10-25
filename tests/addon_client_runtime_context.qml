import QtQml 2.15
import "../qml/AddonClient.js" as AddonClient

QtObject {
    property bool runtimeVisibleHere: PlatformRuntime.android === true
    property var compatibility: AddonClient.parseStream({
        name: "1080p x265 Main10 HDR10+",
        url: "https://fixture.invalid/stream.mkv"
    }, "Fixture", 0, "fixture-addon", PlatformRuntime).videoCompatibility
}
