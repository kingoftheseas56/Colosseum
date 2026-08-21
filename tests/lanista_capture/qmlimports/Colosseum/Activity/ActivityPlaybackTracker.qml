import QtQuick

QtObject {
    property var sink: null
    function begin(identity, sessionId) {}
    function sample(positionMs, durationMs, rateMilli, consuming) {}
    function discontinuity(positionMs, durationMs, rateMilli) {}
    function naturalEof() {}
    function endSession() {}
}
