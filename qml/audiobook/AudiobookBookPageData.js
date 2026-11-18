.pragma library

function text(value) {
    return value === undefined || value === null ? "" : String(value).trim();
}

function names(list) {
    return array(list).map(function(person) {
        return typeof person === "string" ? text(person) : text(person && person.name);
    }).filter(Boolean);
}

function array(list) {
    return list && typeof list !== "string" && list.length !== undefined
        ? Array.prototype.slice.call(list) : [];
}

function joinNames(list) {
    return list.length < 2 ? (list[0] || "")
        : list.slice(0, -1).join(", ") + " & " + list[list.length - 1];
}

function html(value) {
    return value.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function authors(list) {
    return array(list).map(function(person) {
        return { id: text(person && person.id),
                 name: typeof person === "string" ? text(person) : text(person && person.name) };
    }).filter(function(person) { return person.name; });
}

function authorMarkup(people, linkColor) {
    return joinNames(people.map(function(person) {
        var name = html(person.name);
        return person.id ? '<a style="color:' + linkColor + '" href="'
            + encodeURIComponent(person.id) + '">' + name + '</a>' : name;
    })).replace(/ & /g, " &amp; ");
}

function value(object, camel, snake) {
    return object[camel] !== undefined ? object[camel] : object[snake];
}

function runtimeText(minutes) {
    var total = Math.round(Number(minutes));
    if (!isFinite(total) || total <= 0) return "";
    var hours = Math.floor(total / 60), rest = total % 60;
    return hours ? hours + " h" + (rest ? " " + rest + " min" : "") : rest + " min";
}

function capitalise(string) {
    var s = text(string);
    return s ? s.charAt(0).toUpperCase() + s.slice(1) : "";
}

function editionText(value) {
    var edition = text(value);
    if (edition === "0") return qsTr("Unabridged");
    if (edition === "1") return qsTr("Abridged");
    return capitalise(edition);
}

function languageText(value) {
    var language = text(value);
    if (/^(en|eng|english)$/i.test(language)) return qsTr("English");
    if (/^[a-z]{2,3}([-_][a-z]{2})?$/i.test(language)) {
        var locale = Qt.locale(language);
        if (locale.name !== "C" && locale.nativeLanguageName) return capitalise(locale.nativeLanguageName);
    }
    return capitalise(language);
}

// The owner supplies catalogue work(id), and series(id).works separately.
// Both the catalogue's camelCase fields and its bake's snake_case fields are accepted.
function build(work, seriesWorks, recordings) {
    work = work || {};
    var memberships = array(work.series);
    var series = memberships[0] || {};
    var seriesName = text(series.name);
    var number = text(value(series, "numberText", "number_text"));
    var books = seriesName ? array(seriesWorks) : [];
    books = books.map(function(book, index) {
        var position = value(book, "positionSort", "position_sort");
        return {
            workId: text(book.workId || book.id),
            title: text(book.title),
            cover: text(book.coverUrl || book.cover),
            number: text(value(book, "numberText", "number_text")),
            position: position === undefined || position === null || text(position) === ""
                || !isFinite(Number(position)) ? Infinity : Number(position),
            index: index,
            current: text(book.workId || book.id) === text(work.workId || work.id)
        };
    }).filter(function(book) { return book.workId !== ""; });
    books.sort(function(a, b) {
        return a.position === b.position ? a.index - b.index : a.position - b.position;
    });
    var records = array(recordings === undefined ? work.recordings : recordings);
    records = records.filter(function(r) { return r && !r.bundle; }).map(function(r) {
        var facts = [];
        var language = languageText(r.language);
        if (language) facts.push({ key: "language", label: qsTr("Language"), text: language });
        var edition = editionText(r.abridgement);
        if (edition) facts.push({ key: "edition", label: qsTr("Edition"), text: edition });
        var runtime = runtimeText(value(r, "runtimeMin", "runtime_min"));
        if (runtime) facts.push({ key: "runtime", label: qsTr("Runtime"), text: runtime });
        return { key: text(r.recordingKey), id: text(r.id), cover: text(r.coverUrl),
                 narrators: joinNames(names(r.narrators)),
                 facts: facts, local: r.local === true };
    });
    return {
        workId: text(work.workId || work.id), title: text(work.title),
        authors: joinNames(names(work.authors)), authorPeople: authors(work.authors),
        cover: text(work.coverUrl || work.cover),
        synopsis: text(work.description), seriesName: seriesName, seriesNumber: number,
        seriesBooks: books, recordings: records
    };
}

function stateOf(states, key) {
    var raw = states && key ? states[key] : null;
    if (typeof raw === "string") return { state: raw, progress: -1 };
    var progress = raw && typeof raw.progress === "number" && isFinite(raw.progress) ? raw.progress : -1;
    return { state: text(raw && raw.state), progress: progress < 0 ? -1 : Math.max(0, Math.min(1, progress)) };
}

function isLocal(record, local, states) {
    return !!record.key && (record.local || !!(local && local[record.key])
        || stateOf(states, record.key).state === "done");
}

// Prefer an owned recording; the page never chooses a source or starts a download.
function recording(built, local, states) {
    var list = built.recordings;
    for (var i = 0; i < list.length; ++i)
        if (isLocal(list[i], local, states)) return list[i];
    for (var j = 0; j < list.length; ++j)
        if (stateOf(states, list[j].key).state === "active") return list[j];
    return list[0] || { key: "", id: "", narrators: "", facts: [], local: false };
}

function primaryAction(built, record, local, states) {
    if (isLocal(record, local, states))
        return { kind: "listen", label: qsTr("Listen"), enabled: true, progress: -1 };
    var status = stateOf(states, record.key);
    if (!status.state) status = stateOf(states, built.workId);
    if (status.state === "active" || status.state === "queued" || status.state === "paused") {
        var label = status.state === "queued" ? qsTr("Queued")
            : status.state === "paused" ? qsTr("Download paused") : qsTr("Downloading");
        if (status.progress >= 0) label += " " + Math.round(status.progress * 100) + "%";
        return { kind: "progress", label: label, enabled: false, progress: status.progress };
    }
    return { kind: "download", label: qsTr("Download"), enabled: built.workId !== "", progress: -1 };
}
