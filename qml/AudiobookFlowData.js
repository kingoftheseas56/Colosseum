.pragma library
.import "BiblioApi.js" as Biblio

function names(people) {
    if (typeof people === "string") return people ? [people] : []
    return Array.prototype.slice.call(people || []).map(function(person) {
        return typeof person === "string" ? person : person && person.name
    }).filter(Boolean)
}

function authorFor(work) {
    var authors = names(work.authors || work.author)
    return authors.length < 2 ? (authors[0] || "")
        : authors.slice(0, -1).join(", ") + " & " + authors[authors.length - 1]
}

function pairFor(work) {
    return work.title ? Biblio.pairKey(work.title, authorFor(work)) : ""
}

function recordingsFor(work, selectedId, chosen, owned, useSnapshot) {
    var pairKey = pairFor(work)
    var records = Array.prototype.slice.call(work.recordings || []).map(function(recording) {
        var key = recording.id && pairKey ? pairKey + "#as:" + recording.id : ""
        return Object.assign({}, recording, { recordingKey: key })
    })
    function mergeSource(source) {
        if (!source || source.bundle || !source.recordingKey) return
        var match = records.findIndex(function(record) { return record.recordingKey === source.recordingKey })
        if (match < 0 && source.audiosiloRecordingId)
            match = records.findIndex(function(record) {
                return record.id === source.audiosiloRecordingId &&
                    (source.workId === (work.id || work.workId) || source.pairKey === pairKey)
            })
        if (match >= 0) {
            records[match] = Object.assign({}, records[match], { recordingKey: source.recordingKey })
            return
        }
        records.push({
            id: source.audiosiloRecordingId || source.recordingId || "",
            recordingKey: source.recordingKey,
            title: source.title, coverUrl: source.coverUrl || source.cover,
            local: useSnapshot && (source.local === true || source.downloaded === true),
            narrators: source.narrators, language: source.language, abridgement: source.abridgement
        })
    }
    for (var i = 0; i < owned.length; ++i) {
        var source = owned[i]
        if (source.audiosiloRecordingId || (source.workId && source.workId === (work.id || work.workId)))
            mergeSource(source)
    }
    mergeSource(chosen)
    var preferred = records.findIndex(function(record) {
        return chosen ? record.recordingKey === chosen.recordingKey : record.id === selectedId
    })
    if (preferred > 0) records.unshift(records.splice(preferred, 1)[0])
    return records
}

function status(service, key, failed) {
    if (!service || !key) return { state: "none" }
    var current = service.statusOf(key)
    var state = current && current.state || "none"
    if (state === "done" || state === "queued" || state === "paused") return current
    if (state !== "none" && state !== "failed") {
        var total = Number(current.total)
        return { state: "active", progress: total > 0 ? Number(current.received) / total : -1 }
    }
    return failed ? { state: "failed" } : (current || { state: "none" })
}

function playerBook(item, work) {
    work = item.bundle ? {} : work || {}
    var recordingId = item.audiosiloRecordingId || item.recordingId
    var recording = (work.recordings || []).find(function(record) { return record.id === recordingId }) || {}
    var series = work.series && work.series[0]
    var cover = recording.coverUrl || work.coverUrl || work.cover || item.coverUrl || item.cover
    if (!cover && work.recordings)
        cover = (work.recordings.find(function(edition) { return edition.coverUrl }) || {}).coverUrl
    return {
        id: work.id || item.workId || item.recordingKey,
        pairKey: item.bundle ? "" : item.pairKey || pairFor(work),
        title: work.title || item.title || "",
        author: authorFor(work) || item.author || "",
        authors: names(work.authors || item.authors || item.author),
        narrators: names(recording.narrators || item.narrators),
        cover: cover || "",
        description: work.description || "",
        bundle: !!item.bundle,
        seriesLine: series && series.name ? (series.numberText
            ? qsTr("%1 · Book %2").arg(series.name).arg(series.numberText) : series.name) : ""
    }
}
