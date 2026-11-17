.pragma library

// Audiobook book page data (Slice F).
//
// Pure functions: they turn one catalogue work plus its recordings into the
// edition-shaped record the Vinyl album page already knows how to show. A work
// with several narrations is one "album" with several "editions".
//
// Inputs
//   work        slice C's work(id) shape: {id, title, authors:[name | {name}],
//               series:[{name|title, id?, number_text?}], description?, cover?}
//   recordings  [{recordingKey, narrators:[], runtime_min?, language?,
//               abridgement?, release_year?, format?, source?,
//               sourceMatch: "exact" | "probable" | "none", local?, bundle?}]
//   local       {recordingKey: {chapters:[{title, seconds?}]} | [chapter]} for
//               recordings that are downloaded; chapters come from local files.
//
// Nothing is invented: a field that is absent stays absent.

function blank(value) {
    return value === undefined || value === null || String(value).trim() === "";
}

function nameOf(entry) {
    if (blank(entry)) return "";
    if (typeof entry === "string") return entry.trim();
    return String(entry.name || entry.title || "").trim();
}

function names(list) {
    var out = [];
    if (!Array.isArray(list)) list = blank(list) ? [] : [list];
    for (var i = 0; i < list.length; ++i) {
        var n = nameOf(list[i]);
        if (n) out.push(n);
    }
    return out;
}

function joinNames(list) {
    if (list.length === 0) return "";
    if (list.length === 1) return list[0];
    return list.slice(0, -1).join(", ") + " & " + list[list.length - 1];
}

function capitalise(text) {
    var t = String(text).trim();
    return t ? t.charAt(0).toUpperCase() + t.slice(1) : "";
}

function runtimeText(minutes) {
    var total = Math.round(Number(minutes));
    if (!(total > 0)) return "";
    var h = Math.floor(total / 60);
    var m = total % 60;
    if (h === 0) return m + " min";
    return m === 0 ? h + " h" : h + " h " + m + " min";
}

// "The Stormlight Archive, Book 4". number_text is shown as the catalogue gave it.
function seriesLine(series) {
    var name = nameOf(series);
    if (!name) return "";
    var number = blank(series.number_text) ? "" : String(series.number_text).trim();
    if (!number) return name;
    if (/^(book|vol\.?|volume|part|no\.?)\b/i.test(number)) return name + ", " + number;
    return name + ", Book " + number;
}

function seriesList(work) {
    var out = [];
    var list = work && Array.isArray(work.series) ? work.series : [];
    for (var i = 0; i < list.length; ++i) {
        var line = seriesLine(list[i]);
        if (line) out.push({ id: list[i].id, name: nameOf(list[i]), line: line });
    }
    return out;
}

function isOther(recording) {
    return recording.bundle === true || recording.sourceMatch === "none"
        || (recording.sourceMatch !== "exact" && recording.sourceMatch !== "probable");
}

function sourceName(source) {
    if (blank(source)) return "";
    return typeof source === "string" ? source.trim() : String(source.name || source.label || source.site || "").trim();
}

function localEntry(recording, local) {
    var entry = local && recording.recordingKey !== undefined ? local[recording.recordingKey] : undefined;
    return entry === undefined ? null : entry;
}

function chaptersOf(entry) {
    var list = Array.isArray(entry) ? entry : (entry && Array.isArray(entry.chapters) ? entry.chapters : []);
    var out = [];
    for (var i = 0; i < list.length; ++i) {
        var c = list[i];
        var title = typeof c === "string" ? c : (c && (c.title || c.name || c.file)) || "";
        var seconds = c && typeof c === "object" ? Number(c.seconds) : NaN;
        out.push({ title: String(title).trim() || "Part " + (i + 1), seconds: seconds > 0 ? seconds : 0, available: true });
    }
    return out;
}

// What the recording is, in the words the Recordings list shows.
function recordingLabel(recording, narrators) {
    var parts = [];
    var n = joinNames(narrators);
    if (n) parts.push(n);
    if (!blank(recording.abridgement)) parts.push(capitalise(recording.abridgement));
    if (!blank(recording.language)) parts.push(capitalise(recording.language));
    return parts.length ? parts.join(" · ") : "Narrator not listed";
}

function recordingFacts(recording, narrators) {
    var facts = [];
    if (!blank(recording.language)) facts.push(["language", "Language", capitalise(recording.language)]);
    if (!blank(recording.abridgement)) facts.push(["version", "Version", capitalise(recording.abridgement)]);
    var runtime = runtimeText(recording.runtime_min);
    if (runtime) facts.push(["runtime", "Length", runtime]);
    if (!blank(recording.format)) facts.push(["format", "Format", String(recording.format).trim()]);
    if (!blank(recording.release_year)) facts.push(["year", "Recorded", String(recording.release_year)]);
    return facts;
}

function recordingDetail(recording, facts) {
    var parts = [];
    var runtime = runtimeText(recording.runtime_min);
    if (runtime) parts.push(runtime);
    if (!blank(recording.release_year)) parts.push(String(recording.release_year));
    if (!blank(recording.format)) parts.push(String(recording.format).trim());
    var src = sourceName(recording.source);
    if (src) parts.push(src);
    if (recording.sourceMatch === "probable") parts.push("probable match");
    return parts.join(" · ");
}

function editionFor(work, recording, index, local, authors, about, cover) {
    var narrators = names(recording.narrators);
    var entry = localEntry(recording, local);
    var isLocal = recording.local === true || entry !== null;
    var chapters = isLocal ? chaptersOf(entry) : [];
    var facts = recordingFacts(recording, narrators);
    return {
        id: String(recording.recordingKey),
        albumId: String(recording.recordingKey),
        name: String(work.title || ""),
        subtitle: recordingLabel(recording, narrators),
        artist: joinNames(authors),
        artists: authors,
        cover: cover,
        about: about,
        year: blank(recording.release_year) ? undefined : Number(recording.release_year),
        discs: [chapters],
        narrators: narrators,
        narratorLine: joinNames(narrators),
        facts: facts,
        detail: recordingDetail(recording, facts),
        source: sourceName(recording.source),
        sourceMatch: recording.sourceMatch,
        probable: recording.sourceMatch === "probable",
        hasSource: !blank(recording.source),
        local: isLocal,
        hasChapters: chapters.length > 0,
        index: index
    };
}

function otherFor(recording) {
    var narrators = names(recording.narrators);
    var kind = recording.bundle === true
        ? "A bundle: this download holds more than one book"
        : "Not matched to a recording of this book";
    var title = narrators.length ? joinNames(narrators) : "";
    var src = sourceName(recording.source);
    return {
        id: String(recording.recordingKey),
        title: title || src || "Download",
        source: src,
        kind: kind,
        bundle: recording.bundle === true
    };
}

function build(work, recordings, local) {
    work = work || {};
    recordings = Array.isArray(recordings) ? recordings : [];
    var authors = names(work.authors);
    var about = blank(work.description) ? undefined : String(work.description).trim();
    var cover = blank(work.cover) ? "" : String(work.cover);
    var series = seriesList(work);
    var editions = [];
    var others = [];
    for (var i = 0; i < recordings.length; ++i) {
        var r = recordings[i];
        if (!r || blank(r.recordingKey)) continue;
        if (isOther(r)) others.push(otherFor(r));
        else editions.push(editionFor(work, r, editions.length, local, authors, about, cover));
    }
    var information = [];
    for (var s = 1; s < series.length; ++s)
        information.push(["series" + s, "Also in the series", series[s].line]);
    var fallback = {
        id: "",
        albumId: "",
        name: String(work.title || ""),
        subtitle: "",
        artist: joinNames(authors),
        artists: authors,
        cover: cover,
        about: about,
        discs: [[]],
        narrators: [],
        narratorLine: "",
        facts: [],
        detail: "",
        source: "",
        sourceMatch: "none",
        probable: false,
        hasSource: false,
        local: false,
        hasChapters: false,
        index: -1
    };
    return {
        title: String(work.title || ""),
        authors: authors,
        cover: cover,
        about: about,
        series: series,
        seriesLine: series.length ? series[0].line : "",
        information: information,
        editions: editions,
        others: others,
        fallback: fallback
    };
}

// downloadState[recordingKey] is "none" | "done" | "failed" | "active" or
// {state, progress} with progress 0..1 (or 0..100).
function stateOf(downloadState, key) {
    var raw = downloadState ? downloadState[key] : undefined;
    var state = "none";
    var progress = -1;
    if (typeof raw === "string") state = raw;
    else if (raw && typeof raw === "object") {
        state = String(raw.state || "none");
        if (typeof raw.progress === "number") progress = raw.progress > 1 ? raw.progress / 100 : raw.progress;
    }
    return { state: state, progress: progress };
}

// What the one gold primary action says and does for the shown recording.
function primaryAction(edition, downloadState) {
    var st = stateOf(downloadState, edition.id);
    if (!edition.id)
        return { kind: "none", label: "Download", enabled: false };
    if (edition.local || st.state === "done")
        return { kind: "listen", label: "Listen", enabled: true };
    if (st.state === "active") {
        var pct = st.progress >= 0 ? " " + Math.round(Math.max(0, Math.min(1, st.progress)) * 100) + "%" : "";
        return { kind: "progress", label: "Downloading" + pct, enabled: false };
    }
    if (st.state === "failed")
        return { kind: "download", label: "Download failed, try again", enabled: edition.hasSource };
    return { kind: "download", label: "Download", enabled: edition.hasSource };
}
