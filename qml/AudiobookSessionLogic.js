.pragma library

function choices() { return ["off",15,30,45,60,"chapterEnd"]; }
function sleepTick(state, nowMs) {
    state = state || {};
    if (!state.mode || state.mode === "off" || !(Number(state.deadlineMs) > 0))
        return {remainingSec:0,volumeFactor:1,shouldPause:false};
    var seconds = Math.max(0,(Number(state.deadlineMs)-Number(nowMs))/1000);
    return {remainingSec:Math.ceil(seconds),volumeFactor:Math.min(1,seconds/10),shouldPause:seconds <= 0};
}
function stopsAtChapterEnd(mode, previousIndex, currentIndex, reachedEof) {
    return mode === "chapterEnd" && (!!reachedEof || (previousIndex >= 0 && currentIndex >= 0 && previousIndex !== currentIndex));
}
function countdownLabel(mode, remainingSec) {
    if (mode === "chapterEnd") return "Sleeps at chapter end";
    if (!mode || mode === "off") return "";
    return "Sleeps in " + Math.ceil(Math.max(0,remainingSec)/60) + " min";
}
function speedFromResume(record) {
    var speed = Number(record && record.speed);
    return isFinite(speed) && speed >= 0.25 && speed <= 4 ? speed : 1.0;
}
function withSpeed(record, speed) {
    var out = {};
    for (var key in (record || {})) out[key] = record[key];
    out.speed = speedFromResume({speed:speed});
    return out;
}
