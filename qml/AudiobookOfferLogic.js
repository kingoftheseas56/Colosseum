.pragma library

function releaseFacts(row, includeLanguage) {
    row = row || {};
    var parts = [];
    if (row.format) parts.push(String(row.format).toUpperCase());
    if (includeLanguage && row.language) parts.push(String(row.language));
    if (row.sizeLabel || row.size) parts.push(String(row.sizeLabel || row.size));
    return parts.join(" · ");
}

function offerState(input) {
    input = input || {};
    var lookup = input.lookup || {}, pick = input.pick || {}, status = input.status || {};
    var state, label = "", meta = "", progress = 0;
    if (input.builtIn || input.downloaded) {
        state = "ready"; label = "Listen";
        meta = input.builtIn ? "Built-in narration" : releaseFacts(input.entry,false);
    } else if (status.state === "resolving" || status.state === "downloading" || status.state === "queued") {
        state = "getting";
        progress = Number(status.total) > 0 ? Math.max(0,Math.min(1,Number(status.received)/Number(status.total))) : 0;
        if (!isFinite(progress)) progress = 0;
        label = "Getting audiobook · " + Math.round(progress*100) + "%";
        meta = releaseFacts(pick.pick,true);
    } else if (input.failed) {
        state = "failed"; label = "Couldn't get the audiobook · Try again";
    } else if (lookup.loading) {
        state = "checking"; label = "Looking for an audiobook…";
    } else if (lookup.error) {
        state = "unknown";
    } else if (!(lookup.rows || []).length || (pick.candidates && !pick.candidates.length)) {
        state = "none"; label = "No audiobook found";
    } else if (pick.confident && pick.pick) {
        state = "available"; label = "Get audiobook"; meta = releaseFacts(pick.pick,true);
    } else {
        state = "choose"; label = "Choose audiobook";
        meta = (pick.candidates || lookup.rows || []).length + " possible matches · none certain";
    }
    return { offerState:state, label:label, meta:meta, progress:progress };
}
