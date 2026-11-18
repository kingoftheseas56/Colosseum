.pragma library
.import "BiblioApi.js" as Biblio

const maxPages = 2;
const maxCandidates = 12;
const maxConcurrent = 3;

function text(value) {
    return value === undefined || value === null ? "" : String(value).trim();
}

function normalized(value) {
    return text(value).toLowerCase().replace(/\s+/g, " ");
}

function language(value) {
    const name = normalized(value);
    return /^(en|eng|english)(?:[-_][a-z]{2})?$/.test(name) ? "english" : name;
}

function abridgement(value, dramatized) {
    if (dramatized) return "dramatized";
    const name = normalized(value);
    if (name === "0" || name === "unabridged") return "unabridged";
    if (name === "1" || name === "abridged") return "abridged";
    if (name === "dramatized" || name === "dramatization") return "dramatized";
    return "";
}

function credits(value, splitSource) {
    const names = typeof value === "string" ? [value] : Array.prototype.slice.call(value || []);
    return names.reduce(function(result, name) {
        const label = text(name && (name.name || name));
        if (!label) return result;
        // ABB sometimes places multiple people in one span. Keep inverted
        // names and uncertain labels together instead of guessing initials.
        const parts = splitSource ? label.split(/\s*(?:;|\/|\s+&\s+|\s+and\s+)\s*/i) : [label];
        parts.forEach(function(part) {
            const commaParts = splitSource ? part.split(/,\s*/) : [part];
            const people = commaParts.every(function(person) { return /\S\s+\S/.test(person); })
                ? commaParts : [part];
            people.map(text).forEach(function(person) {
                if (person && !result.includes(person)) result.push(person);
            });
        });
        return result;
    }, []);
}

function recordingEvidence(context) {
    return Array.prototype.slice.call(context.recordings || []).filter(function(record) {
        return record && record.id && !record.bundle
            && (!record.workId || record.workId === context.workId);
    }).map(function(record) {
        return Object.assign({}, record, {
            recordingKey: context.pairKey + "#as:" + record.id,
            workTitle: context.title,
            workAuthor: context.author,
            narrators: credits(record.narrators, false),
            language: language(record.language),
            abridgement: abridgement(record.abridgement, record.dramatized)
        });
    });
}

function countBooks(title) {
    const range = /\b(?:books?|vol(?:ume)?s?|series)\s*#?\s*(\d{1,2})\s*[-–—]\s*(\d{1,2})\b|#\s*(\d{1,2})\s*[-–—]\s*(\d{1,2})\b|\s(\d{1,2})\s*[-–—]\s*(\d{1,2})$/.exec(title.toLowerCase());
    if (range) {
        const first = Number(range[1] || range[3] || range[5]);
        const last = Number(range[2] || range[4] || range[6]);
        return last > first ? last - first + 1 : 0;
    }
    const count = /\b(\d{1,2})\s+(?:audio)?books\b/i.exec(title);
    return count ? Number(count[1]) : 0;
}

function differentNarrators(source, target) {
    if (!target || !source.length || !target.narrators.length) return false;
    const targetNames = target.narrators.map(normalized);
    return !source.some(function(name) { return targetNames.includes(normalized(name)); });
}

function namedNarrators(narrators) {
    return narrators.filter(function(name) {
        return !/^(?:(?:a |the )?full cast|multiple narrators?|various(?: narrators?)?|unknown|n\/?a)$/i.test(name);
    });
}

function uploadTitle(title, context) {
    const clean = normalized(title).replace(/\s*\((?:unabridged|abridged|dramatized|audiobook)\)$/, "").trim();
    const work = normalized(context.title);
    if (!work || !clean.startsWith(work)) return clean;
    const suffix = /^\s*(?:\(([^()]+)\)|:\s*(.+))$/.exec(clean.slice(work.length));
    if (!suffix) return clean;
    const membership = /^(.*?)[,\s]+(?:books?\s+|vol(?:ume)?\.?\s+|#\s*)(\d+(?:\.\d+)?)$/.exec((suffix[1] || suffix[2]).trim());
    if (!membership) return clean;
    const name = normalized(membership[1]).replace(/^the\s+/, "");
    const verifiedSeries = Array.prototype.some.call(context.series || [], function(series) {
        const number = normalized(series.numberText).replace(/^(?:book\s+|#\s*)/, "");
        return name === normalized(series.name).replace(/^the\s+/, "")
            && /^\d+(?:\.\d+)?$/.test(number) && Number(number) === Number(membership[2]);
    });
    return verifiedSeries ? work : clean;
}

function describe(listing, facts, matcher, context, editions, target) {
    const infoHash = text(facts && facts.infoHash).toLowerCase();
    if (!/^[a-f0-9]{40}$/.test(infoHash)) return null;
    const bundle = !!(listing.bundle || facts.bundle);
    const title = text(listing.title);
    const author = text(facts.author || listing.author);
    const narrators = credits(facts.narrator || facts.narrators, true);
    const source = Object.assign({}, listing, facts, {
        title: uploadTitle(title, context), author: normalized(author), infoHash: infoHash,
        language: language(facts.language || listing.language),
        abridgement: abridgement(facts.abridgement, facts.dramatized), narrators: namedNarrators(narrators)
    });
    const verdict = !bundle && matcher && typeof matcher.matchSource === "function"
        ? matcher.matchSource(source, editions) : null;
    const confident = verdict && (verdict.verdict === "exact" || verdict.verdict === "probable")
        ? editions.find(function(record) { return record.recordingKey === verdict.recordingKey; }) : null;
    const sameWork = !bundle && (!!confident || (source.title && source.author
        && source.title === normalized(context.title) && source.author === normalized(context.author)));
    const targetMatch = !!(target && confident && confident.id === target.id);
    const rank = targetMatch ? (verdict.verdict === "exact" ? 2 : 1) : 0;
    const different = !bundle && !targetMatch && differentNarrators(source.narrators, target);
    const differentEdition = !bundle && !targetMatch && target
        && (confident || (source.abridgement && target.abridgement && source.abridgement !== target.abridgement)
            || (source.language && target.language && source.language !== target.language));
    const pairKey = sameWork ? context.pairKey : Biblio.pairKey(title, author);
    const format = text(facts.format || listing.format).toUpperCase();
    const request = {
        recordingKey: bundle ? "bundle#src:" + infoHash : confident ? confident.recordingKey
            : pairKey + "#src:" + infoHash,
        infoHash: infoHash,
        title: sameWork ? context.title : title,
        author: sameWork ? context.author : author,
        sourceKind: "abb",
        sourceRef: listing.slug,
        bundle: bundle,
        narrators: narrators,
        language: source.language,
        abridgement: source.abridgement,
        format: format
    };
    if (!bundle) request.pairKey = pairKey;
    if (confident) request.audiosiloRecordingId = confident.id;
    if (sameWork && context.workId) request.workId = context.workId;
    return {
        title: title,
        format: format,
        fileCount: Number(facts.audioFileCount === undefined ? facts.fileCount : facts.audioFileCount) || 0,
        size: text(facts.size || listing.size),
        narrators: narrators,
        bundle: bundle,
        bundleCount: bundle ? (Number(facts.bundleCount || listing.bundleCount) || countBooks(title)) : 0,
        verdict: targetMatch ? verdict.verdict : "none",
        reason: bundle || targetMatch ? "" : different ? qsTr("Different narration")
            : differentEdition ? qsTr("Different edition") : qsTr("Unconfirmed"),
        recommended: false,
        request: request,
        rank: rank,
        seeders: Number(facts.seeders === undefined ? listing.seeders : facts.seeders) || 0,
        unresolved: !bundle && !targetMatch && !different && !differentEdition
    };
}

// Search/detail can complete synchronously (cached data or a test adapter).
// Register each slot before invoking the adapter, then release it on terminal
// callback so cancellation only aborts requests that are still pending.
function resolve(abb, matcher, context, done) {
    const editions = recordingEvidence(context);
    const target = editions.find(function(record) { return record.id === context.targetRecordingId; });
    const candidates = [];
    const active = [];
    const seen = Object.create(null);
    let stopped = false;
    let incomplete = false;
    let problem = "";
    let next = 0;
    let rows = [];
    let pumping = false;

    function finish() {
        if (stopped) return;
        stopped = true;
        rows = rows.filter(function(row) { return !!row; });
        rows.sort(function(a, b) {
            return b.rank - a.rank || Number(b.fileCount === 1) - Number(a.fileCount === 1)
                || b.seeders - a.seeders;
        });
        const confident = rows.filter(function(row) { return row.rank > 0; });
        if (confident.length) confident[0].recommended = true;
        const automatic = confident.length === 1 && !incomplete && !problem
            && !rows.some(function(row) { return row.unresolved; }) ? confident[0] : null;
        done({ rows: rows, automatic: automatic, error: rows.length ? "" : problem });
    }

    function start(invoke, receive, waitForFresh) {
        const slot = {};
        active.push(slot);
        slot.handle = invoke(function(value, error) {
            const index = active.indexOf(slot);
            if (stopped || index < 0) return;
            // ABB emits an interim stale cache value as it starts revalidation.
            if (waitForFresh && value && value.stale && !error) return;
            active.splice(index, 1);
            receive(value, error);
        });
        if (stopped && slot.handle && active.includes(slot)) slot.handle.cancel();
    }

    function pump() {
        if (pumping || stopped) return;
        pumping = true;
        while (next < candidates.length && active.length < maxConcurrent) {
            const index = next++;
            const listing = candidates[index];
            start(function(callback) { return abb.detail(listing.slug, callback); }, function(value, error) {
                const row = describe(listing, value, matcher, context, editions, target);
                if (!row || error) problem = problem || error || "unavailable";
                rows[index] = row;
                pump();
            });
        }
        pumping = false;
        if (next === candidates.length && active.length === 0) finish();
    }

    function search(page) {
        const term = abb.searchTerm ? abb.searchTerm(context.title, context.author)
            : normalized(text(context.title) + " " + text(context.author));
        start(function(callback) { return abb.search(term, page, callback); }, function(result, error) {
            if (error || !result || result.offline) problem = error || "offline";
            const resultRows = (result && result.rows) || [];
            const listings = abb.relevantRows ? abb.relevantRows(resultRows, context.title, context.author) : resultRows;
            for (let i = 0; i < listings.length; i++) {
                const listing = listings[i];
                if (!listing.slug) {
                    incomplete = true;
                    continue;
                }
                if (seen[listing.slug]) continue;
                seen[listing.slug] = true;
                if (candidates.length < maxCandidates) candidates.push(listing);
                else incomplete = true;
            }
            if (result && result.hasMore && !problem) {
                if (page < maxPages && candidates.length < maxCandidates) {
                    search(page + 1);
                    return;
                }
                incomplete = true;
            }
            pump();
        }, true);
    }

    search(1);
    return function() {
        if (stopped) return;
        stopped = true;
        active.splice(0).forEach(function(slot) {
            if (slot.handle && typeof slot.handle.cancel === "function") slot.handle.cancel();
        });
    };
}
