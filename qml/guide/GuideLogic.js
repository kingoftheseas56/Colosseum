.pragma library

var STATUSES = ["draft", "verified", "published", "uncertain", "retired"];

function validateLesson(lesson) {
    var errors = [];
    ["id", "section", "title", "outcome", "status"].forEach(function(key) {
        if (!lesson || !String(lesson[key] || "").trim()) errors.push("missing " + key);
    });
    if (lesson && typeof lesson.order !== "number") errors.push("missing order");
    if (lesson && !Array.isArray(lesson.worlds)) errors.push("missing worlds");
    if (lesson && !Array.isArray(lesson.evidence)) errors.push("missing evidence");
    if (lesson && STATUSES.indexOf(lesson.status) < 0) errors.push("invalid status");
    if (lesson && lesson.status === "published" && (!lesson.verifiedCommit || !lesson.verifiedDate))
        errors.push("published lesson lacks verification");
    return errors;
}

function asLessons(all) {
    return Array.isArray(all) ? all : [];
}

function visibleLessons(all) {
    return asLessons(all).filter(function(lesson) { return lesson && lesson.status === "published"; });
}

function lessonById(all, id) {
    var lessons = visibleLessons(all);
    for (var index = 0; index < lessons.length; index++) {
        if (lessons[index].id === id) return lessons[index];
    }
    return null;
}

function lessonsForSection(all, section) {
    return visibleLessons(all).filter(function(lesson) { return lesson.section === section; });
}

function relatedLessons(all, lesson) {
    if (!lesson || !Array.isArray(lesson.related)) return [];
    var result = [];
    var seen = {};
    for (var index = 0; index < lesson.related.length; index++) {
        var id = lesson.related[index];
        var key = String(id);
        if (seen[key]) continue;
        seen[key] = true;
        var related = lessonById(all, id);
        if (related) result.push(related);
    }
    return result;
}

function normalized(value) {
    return String(value === undefined || value === null ? "" : value)
        .toLowerCase()
        .replace(/[^a-z0-9]+/g, " ")
        .replace(/\s+/g, " ")
        .trim();
}

function containsAllWords(text, words) {
    var tokens = " " + text + " ";
    for (var index = 0; index < words.length; index++) {
        if (tokens.indexOf(" " + words[index] + " ") < 0) return false;
    }
    return words.length > 0;
}

function fieldScore(text, query, words, exactScore, prefixScore, wordsScore) {
    if (!text) return 0;
    if (text === query) return exactScore;
    if (text.indexOf(query) === 0) return prefixScore;
    return containsAllWords(text, words) ? wordsScore : 0;
}

function termsScore(terms, query, words) {
    if (!Array.isArray(terms)) return 0;
    var score = 0;
    for (var index = 0; index < terms.length; index++) {
        score = Math.max(score, fieldScore(normalized(terms[index]), query, words, 3000, 2800, 2600));
    }
    return score;
}

function bodyText(lesson) {
    var blocks = "";
    try { blocks = JSON.stringify(lesson.blocks || []); } catch (error) { blocks = ""; }
    return normalized(String(lesson.outcome || "") + " " + blocks);
}

function hasContext(lesson, context) {
    var wanted = normalized(context);
    if (!wanted || !Array.isArray(lesson.contexts)) return false;
    for (var index = 0; index < lesson.contexts.length; index++) {
        if (normalized(lesson.contexts[index]) === wanted) return true;
    }
    return false;
}

function search(all, query, context) {
    var wanted = normalized(query);
    if (!wanted) return [];
    var words = wanted.split(" ");
    var ranked = [];
    visibleLessons(all).forEach(function(lesson, index) {
        var score = fieldScore(normalized(lesson.title), wanted, words, 5000, 4800, 4600);
        score = Math.max(score, termsScore(lesson.searchTerms, wanted, words));
        score = Math.max(score, fieldScore(bodyText(lesson), wanted, words, 2000, 1800, 1600));
        if (!score) return;
        if (hasContext(lesson, context)) score += 100;
        ranked.push({ lesson: lesson, score: score, index: index });
    });
    ranked.sort(function(left, right) {
        return right.score - left.score || left.index - right.index;
    });
    return ranked.map(function(item) { return item.lesson; });
}
