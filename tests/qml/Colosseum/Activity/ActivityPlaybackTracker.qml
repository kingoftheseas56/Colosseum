import QtQuick 2.15
QtObject {
    property var sink: null
    function begin(payload, sessionId) {}
    function sample(positionMs, durationMs, rateMilli, consuming) {}
    function end() {}
}
