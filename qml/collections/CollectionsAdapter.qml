import QtQuick

QtObject {
    id: adapter
    required property var nativeStore
    property var transientReference: ({})
    readonly property int revision: nativeStore ? nativeStore.revision : 0
    readonly property bool healthy: !!nativeStore && nativeStore.healthy
    readonly property string error: nativeStore ? nativeStore.error : qsTr("Collections are unavailable while your profile is changing.")
    readonly property var collections: {
        adapter.revision
        if (!adapter.nativeStore) return []
        return adapter.nativeStore.collections().map(function(row) {
            const item = Object.assign({}, row)
            item.tint = adapter.tintFor(item.id)
            return item
        })
    }
    readonly property var references: {
        const result = Object.create(null)
        for (const collection of adapter.collections)
            for (const reference of collection.members || [])
                if (!result[reference.key]) result[reference.key] = reference
        return result
    }
    readonly property var groups: [
        {key:"film", label:qsTr("Films"), singular:qsTr("Film")},
        {key:"series", label:qsTr("Series"), singular:qsTr("Series")},
        {key:"manga", label:qsTr("Manga"), singular:qsTr("Manga")},
        {key:"comic", label:qsTr("Comics"), singular:qsTr("Comic")},
        {key:"book", label:qsTr("Books"), singular:qsTr("Book")},
        {key:"album", label:qsTr("Albums"), singular:qsTr("Album")},
        {key:"song", label:qsTr("Songs"), singular:qsTr("Song")},
        {key:"artist", label:qsTr("Artists"), singular:qsTr("Artist")},
        {key:"other", label:qsTr("Other titles"), singular:qsTr("Title")}
    ]

    function tintFor(id) {
        const tones = ["#6d6661", "#735843", "#3c5964", "#4f5464", "#664c50", "#59624f"]
        var hash = 0
        for (var i = 0; i < id.length; i++) hash = ((hash * 31) + id.charCodeAt(i)) >>> 0
        return tones[hash % tones.length]
    }
    function filteredCollections(query) {
        const needle = String(query || "").trim().toLocaleLowerCase()
        return adapter.collections.filter(function(row) {
            return !needle || row.name.toLocaleLowerCase().indexOf(needle) !== -1
        })
    }
    function getCollection(id) {
        return adapter.collections.find(function(row) { return row.id === id }) || null
    }
    function reference(key) {
        return adapter.references[key]
            || (adapter.transientReference.key === key ? adapter.transientReference : null)
    }
    function mediaKind(reference) {
        const kind = String(reference ? reference.kind : "")
        if (kind === "movie" || kind === "film") return "film"
        return adapter.groups.some(function(row) { return row.key === kind }) ? kind : "other"
    }
    function media(key) {
        const ref = adapter.reference(key) || {}
        const kind = adapter.mediaKind(ref)
        const group = adapter.groups.find(function(row) { return row.key === kind })
        const worlds = { theatre: "Theatre", tankoban: "Tankoban", biblio: "Biblio", vinyl: "Vinyl", vault: "Vault" }
        return { id: key, title: ref.title || qsTr("Untitled"), creator: ref.creator || "",
            year: ref.year || "", kind: kind, kindLabel: group.singular,
            world: worlds[ref.world] || qsTr("Original source"), cover: ref.cover || "",
            available: !!ref.route, route: ref.route || "", reference: ref }
    }
    function filterOptions(ids) {
        const present = Object.create(null)
        for (const key of ids || []) present[adapter.media(key).kind] = true
        return [{key:"all", label:qsTr("All titles")}].concat(adapter.groups.filter(function(group) { return !!present[group.key] }))
    }
    function summary(collection) {
        return adapter.filterOptions(collection.items).slice(1).map(function(group) { return group.label }).join(" · ")
    }
    function memberships(key) {
        return adapter.nativeStore ? adapter.nativeStore.memberships(key) : []
    }
    function createEmpty(name) {
        if (!adapter.healthy) return {ok:false, error:adapter.error}
        const id = adapter.nativeStore.createCollection(name)
        return {ok:!!id, id:id, error:id ? "" : adapter.nativeStore.error}
    }
    function saveMemberships(reference, ids, newName) {
        if (!adapter.healthy) return {ok:false, error:adapter.error}
        return adapter.nativeStore.saveMemberships(reference, ids, newName || "")
    }
    function beginEdit(id) {
        return adapter.healthy ? adapter.nativeStore.beginEdit(id) : ({})
    }
    function saveEdit(token, name, description, items, notes) {
        if (!adapter.healthy) return {ok:false, error:adapter.error}
        const ok = adapter.nativeStore.saveEdit(token, name, description, items, notes)
        return {ok:ok, error:ok ? "" : adapter.nativeStore.error}
    }
    function cancelEdit(token) {
        if (adapter.nativeStore && token) adapter.nativeStore.cancelEdit(token)
    }
    function deleteCollection(id) {
        if (!adapter.healthy) return {ok:false, error:adapter.error}
        const ok = adapter.nativeStore.deleteCollection(id)
        return {ok:ok, error:ok ? "" : adapter.nativeStore.error}
    }
    function reload() {
        return adapter.nativeStore ? adapter.nativeStore.reload() : false
    }
}
