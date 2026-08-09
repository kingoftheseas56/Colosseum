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

export function assertNoForbidden(lessons, patterns) {
    const text = JSON.stringify(lessons);
    patterns.forEach(pattern => {
        if (pattern.test(text)) throw new Error(`forbidden claim ${pattern}`);
    });
}
