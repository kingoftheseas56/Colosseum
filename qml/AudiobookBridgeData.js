.pragma library
.import "BiblioApi.js" as Biblio
.import "AudiobookFlowData.js" as FlowData

function pairFor(book) {
    var key = book.pairKey || Biblio.pairKey(book.title, FlowData.authorFor(book));
    return /^[^|]+\|[^|]+$/.test(key) ? key : "";
}

function findWork(catalogue, book, pairKey) {
    if (!catalogue) return {};
    if (book.audiobookWorkId) return catalogue.work(book.audiobookWorkId) || {};
    if (!pairKey || !book.title) return {};
    var candidates = catalogue.search(book.title, 12) || [];
    var exact = [];
    for (var i = 0; i < candidates.length; ++i) {
        var candidate = candidates[i];
        if (Biblio.pairKey(candidate.title, "") !== Biblio.pairKey(book.title, "")) continue;
        var work = catalogue.work(candidate.id || candidate.workId) || {};
        if (FlowData.pairFor(work) === pairKey) exact.push(work);
    }
    return exact.length === 1 ? exact[0] : {};
}

function withPartNumber(recording) {
    var pattern = /(?:^|[^a-z0-9])part[\s_-]+([1-9]\d?)(?=$|[^a-z0-9])/i;
    var part = pattern.exec(recording.title || "") || pattern.exec(recording.sourceRef || "");
    return Object.assign({}, recording, { partNumber: part ? part[1] : "" });
}

function describe(catalogue, downloads, book, pairing) {
    book = book || {};
    pairing = pairing || {};
    var suppliedPair = pairFor(book);
    var work = findWork(catalogue, book, suppliedPair);
    var pairKey = pairing.pairKey || suppliedPair || FlowData.pairFor(work);
    var workId = work.id || book.audiobookWorkId || "";
    var recordings = (pairKey && downloads && downloads.recordingsForWork
        ? Array.prototype.slice.call(downloads.recordingsForWork(pairKey) || []) : []).filter(function(row) {
        return !row.bundle && !row.missing && (!row.workId || !workId || row.workId === workId)
            && downloads.isDownloaded(row.recordingKey);
    }).map(withPartNumber);
    var local = recordings.find(function(row) { return row.recordingKey === pairing.recordingKey; });
    if (!local && pairKey && downloads && downloads.resolveRecordingKey) {
        var preferred = downloads.resolveRecordingKey(pairKey);
        local = recordings.find(function(row) { return row.recordingKey === preferred; });
    }
    var editions = work.recordings || [];
    var edition = local && editions.find(function(row) { return row.id === local.audiosiloRecordingId; });
    edition = edition || editions[0] || {};
    return {
        workId: workId, pairKey: pairKey,
        recordingKey: local ? local.recordingKey : "",
        recordingId: edition.id || "",
        narrators: FlowData.names(edition.narrators || (local && local.narrators)).join(" & "),
        recordings: recordings,
        needsChoice: recordings.length > 1 && !local
    };
}

function ownedEbook(audio, rows) {
    if (!audio || audio.bundle) return null;
    var pairKey = pairFor(audio);
    if (!pairKey) return null;
    return Array.prototype.slice.call(rows || []).find(function(row) {
        return row.path && !row.missing && pairFor(row) === pairKey
            && (!row.audiobookWorkId || !audio.workId || row.audiobookWorkId === audio.workId);
    }) || null;
}
