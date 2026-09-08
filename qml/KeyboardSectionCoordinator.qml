// KeyboardSectionCoordinator — owner-local return state for adjacent keyboard rails.
// A WorldPage owns one coordinator and its child rails opt in through their existing
// keyboardSectionCoordinator property. It remembers only the immediately preceding
// owner identity; it is not a global navigation history service.
import QtQml

QtObject {
    property var sourceOwner: null
    property string sourceIdentity: ""
    property int sourceIndex: -1
    property real sourceOffset: 0

    function remember(owner, index, identity, offset) {
        sourceOwner = owner
        sourceIndex = Number(index)
        sourceIdentity = String(identity || "")
        sourceOffset = Number(offset || 0)
    }

    function returnRecord(owner) {
        if (!sourceOwner || sourceOwner === owner)
            return null
        return { owner: sourceOwner, index: sourceIndex,
                 identity: sourceIdentity, offset: sourceOffset }
    }

    function clear() {
        sourceOwner = null
        sourceIdentity = ""
        sourceIndex = -1
        sourceOffset = 0
    }
}
