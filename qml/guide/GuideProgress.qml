import QtQuick 2.15
import QtCore

QtObject {
    id: root

    property string settingsCategory: "guide"
    property var _completedSteps: []
    readonly property var completedSteps: _completedSteps

    property Settings settingsStore: Settings {
        id: store
        category: root.settingsCategory
    }

    function _decodedSteps(serialized) {
        var parsed = [];
        try {
            parsed = JSON.parse(serialized);
        } catch (error) {
            parsed = [];
        }
        if (!Array.isArray(parsed))
            return [];

        var result = [];
        var seen = {};
        for (var index = 0; index < parsed.length; ++index) {
            if (typeof parsed[index] !== "string")
                continue;
            var id = parsed[index].trim();
            if (!id || seen[id])
                continue;
            seen[id] = true;
            result.push(id);
        }
        return result;
    }

    function _storeSteps(next) {
        _completedSteps = next.slice();
        store.setValue("completedIds", JSON.stringify(_completedSteps));
        store.sync();
    }

    function complete(stepId) {
        if (typeof stepId !== "string")
            return;
        var id = stepId.trim();
        if (!id || _completedSteps.indexOf(id) >= 0)
            return;
        var next = _completedSteps.slice();
        next.push(id);
        _storeSteps(next);
    }

    function resetJourney() {
        _storeSteps([]);
    }

    Component.onCompleted: _completedSteps = _decodedSteps(store.value("completedIds", "[]"))
}
