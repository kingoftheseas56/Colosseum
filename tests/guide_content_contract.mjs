import fs from "fs";

export function loadQmlJs(path, names) {
    const src = fs.readFileSync(path, "utf8").replace(/^\.pragma library\s*$/m, "");
    const mod = {};
    const expose = names.map(name => `module.${name}=${name};`).join("\n");
    new Function("module", src + "\n" + expose)(mod);
    return mod;
}

export function assertCoverage(lessons, expectedIds) {
    const seen = new Map();
    lessons.forEach(lesson => (lesson.sourceIds || []).forEach(id =>
        seen.set(id, (seen.get(id) || 0) + 1)));
    expectedIds.forEach(id => {
        if (seen.get(id) !== 1) throw new Error(`${id} coverage=${seen.get(id) || 0}`);
    });
}

export function assertNoPublishedUnverified(lessons) {
    lessons.forEach(lesson => {
        if (lesson.status === "published" && (!lesson.verifiedCommit || !lesson.verifiedDate))
            throw new Error(`unverified publish ${lesson.id}`);
    });
}

// Scoped PER LESSON, not across the whole cohort: a forbidden claim lives inside one lesson, so
// matching each lesson's own serialization avoids phantom trips where unrelated tokens in separate
// lessons line up under one regex (e.g. "clear" in a Vault lesson + "delete" in a Downloads lesson).
// This is strictly more permissive than whole-blob matching — it can only drop false positives,
// never miss a real single-lesson violation. Author patterns to target the actionable CLAIM
// ("use the Downloaded filter"), not the mere mention ("there is no Downloaded filter").
export function assertNoForbidden(lessons, patterns) {
    lessons.forEach(lesson => {
        const text = JSON.stringify(lesson);
        patterns.forEach(pattern => {
            if (pattern.test(text))
                throw new Error(`forbidden claim ${pattern} in ${lesson.id || "(unknown lesson)"}`);
        });
    });
}
