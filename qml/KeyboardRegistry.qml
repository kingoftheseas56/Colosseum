// KeyboardRegistry — one live registry for semantic keyboard commands.
//
// The registry stores command objects, not executable strings. It is therefore
// useful to the Guide and audit tooling without creating a dynamic backend
// dispatch surface. Loader-owned commands can unregister explicitly, and the
// QObject destruction hook removes them when a Loader tears them down.
import QtQuick

QtObject {
    id: registry

    property var _commands: []
    property var _byId: ({})
    property var _triggerHandlers: ({})

    signal commandRegistered(string semanticId)
    signal commandUnregistered(string semanticId)
    signal commandTriggered(string semanticId, var source)
    signal registrationRejected(string semanticId, string reason)

    function _idFor(command) {
        return command && command.semanticId !== undefined
            ? String(command.semanticId).trim()
            : ""
    }

    function _reject(id, reason) {
        registry.registrationRejected(id, reason)
        return false
    }

    function registerCommand(command) {
        var id = registry._idFor(command)
        if (!command || !id.length)
            return registry._reject(id, "missing-semantic-id")

        var existing = registry._byId[id]
        if (existing) {
            if (existing === command)
                return true
            return registry._reject(id, "duplicate-semantic-id")
        }

        var handler = function(source) {
            registry.commandTriggered(id, source)
        }
        command.triggered.connect(handler)
        command.aboutToDestroy.connect(function() {
            registry._unregisterId(id, null)
        })

        registry._byId[id] = command
        registry._triggerHandlers[id] = handler
        registry._commands.push(command)
        registry.commandRegistered(id)
        return true
    }

    function _unregisterId(id, expected) {
        var current = registry._byId[id]
        if (!current || (expected && current !== expected))
            return false

        var handler = registry._triggerHandlers[id]
        if (handler && current.triggered)
            current.triggered.disconnect(handler)

        delete registry._byId[id]
        delete registry._triggerHandlers[id]
        var remaining = []
        for (var i = 0; i < registry._commands.length; i++) {
            if (registry._commands[i] !== current)
                remaining.push(registry._commands[i])
        }
        registry._commands = remaining
        registry.commandUnregistered(id)
        return true
    }

    function unregisterCommand(command) {
        var id = registry._idFor(command)
        return id.length ? registry._unregisterId(id, command) : false
    }

    function command(semanticId) {
        var id = String(semanticId || "").trim()
        return id.length ? (registry._byId[id] || null) : null
    }

    function invoke(semanticId, source) {
        var target = registry.command(semanticId)
        if (!target || !target.invoke)
            return false
        return target.invoke(source)
    }

    function _entryFor(command) {
        return {
            semanticId: String(command.semanticId),
            label: String(command.label || ""),
            category: String(command.category || ""),
            scope: String(command.scope || ""),
            sequences: command.sequences ? command.sequences.slice(0) : [],
            enabled: command.enabled === true,
            icon: String(command.icon || ""),
            whenFocused: String(command.whenFocused || ""),
            notes: String(command.notes || "")
        }
    }

    function snapshot() {
        var result = []
        for (var i = 0; i < registry._commands.length; i++) {
            var target = registry._commands[i]
            if (target && target.semanticId !== undefined)
                result.push(registry._entryFor(target))
        }
        return result
    }

    function entriesFor(scope, category) {
        var result = []
        var wantedScope = scope === undefined || scope === null ? "" : String(scope)
        var wantedCategory = category === undefined || category === null
            ? "" : String(category)
        var all = registry.snapshot()
        for (var i = 0; i < all.length; i++) {
            if ((wantedScope.length === 0 || all[i].scope === wantedScope)
                    && (wantedCategory.length === 0 || all[i].category === wantedCategory))
                result.push(all[i])
        }
        return result
    }
}
