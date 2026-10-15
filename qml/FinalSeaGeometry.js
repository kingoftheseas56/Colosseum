.pragma library

function finite(value, fallback) {
    return typeof value === "number" && isFinite(value) ? value : fallback;
}

function clamp(value, minimum, maximum) {
    var low = finite(minimum, 0);
    var high = Math.max(low, finite(maximum, low));
    return Math.max(low, Math.min(high, finite(value, low)));
}

function fitScale(width, height) {
    var w = Math.max(0, finite(width, 0));
    var h = Math.max(0, finite(height, 0));
    return Math.min(w / 1672, h / 941);
}

function stageGeometry(width, height, zoom) {
    var w = Math.max(0, finite(width, 0));
    var h = Math.max(0, finite(height, 0));
    var scale = fitScale(w, h) * clamp(zoom, 1, 2);
    return {
        width: 1672 * scale, height: 941 * scale,
        x: Math.max(0, (w - 1672 * scale) / 2),
        y: Math.max(0, (h - 941 * scale) / 2),
        scale: scale
    };
}

function rectanglesOverlap(a, b) {
    return !!a && !!b && a.width > 0 && a.height > 0 && b.width > 0 && b.height > 0
            && a.x < b.x + b.width && a.x + a.width > b.x
            && a.y < b.y + b.height && a.y + a.height > b.y;
}

function overlapArea(a, b) {
    if (!rectanglesOverlap(a, b)) return 0;
    return Math.max(0, Math.min(a.x + a.width, b.x + b.width) - Math.max(a.x, b.x))
            * Math.max(0, Math.min(a.y + a.height, b.y + b.height) - Math.max(a.y, b.y));
}

function placePreview(anchor, size, bounds, obstacles) {
    var left = finite(bounds && bounds.x, 0);
    var top = finite(bounds && bounds.y, 0);
    var width = Math.max(0, finite(size && size.width, 0));
    var height = Math.max(0, finite(size && size.height, 0));
    var right = left + Math.max(0, finite(bounds && bounds.width, 0));
    var bottom = top + Math.max(0, finite(bounds && bounds.height, 0));
    var maxX = Math.max(left, right - width);
    var maxY = Math.max(top, bottom - height);
    var a = anchor || { x: left, y: top, width: 0, height: 0 };
    var cx = finite(a.x, left) + finite(a.width, 0) / 2;
    var cy = finite(a.y, top) + finite(a.height, 0) / 2;
    var gap = 16;
    var candidates = [
        { x: cx - width / 2, y: a.y - height - gap },
        { x: cx - width / 2, y: a.y + a.height + gap },
        { x: a.x + a.width + gap, y: cy - height / 2 },
        { x: a.x - width - gap, y: cy - height / 2 },
        { x: left, y: top }, { x: maxX, y: top },
        { x: left, y: maxY }, { x: maxX, y: maxY },
        { x: left, y: cy - height / 2 }, { x: maxX, y: cy - height / 2 },
        { x: cx - width / 2, y: top }, { x: cx - width / 2, y: maxY }
    ];
    // Candidate edges beside controls allow useful fallbacks on constrained windows.
    var blocked = obstacles || [];
    for (var j = 0; j < blocked.length; ++j) {
        var obstacle = blocked[j];
        if (!obstacle) continue;
        candidates.push({ x: obstacle.x - width - gap, y: cy - height / 2 });
        candidates.push({ x: obstacle.x + obstacle.width + gap, y: cy - height / 2 });
    }
    var best = { x: left, y: top };
    var bestScore = Number.MAX_VALUE;
    for (var i = 0; i < candidates.length; ++i) {
        var rect = {
            x: clamp(candidates[i].x, left, maxX),
            y: clamp(candidates[i].y, top, maxY),
            width: width, height: height
        };
        // Never trade covering the selected seal for a slightly shorter distance.
        var score = overlapArea(rect, a) * 1000000;
        for (var k = 0; k < blocked.length; ++k)
            score += overlapArea(rect, blocked[k]) * 10000;
        var dx = rect.x + width / 2 - cx;
        var dy = rect.y + height / 2 - cy;
        score += dx * dx + dy * dy;
        if (score < bestScore) {
            bestScore = score;
            best = { x: rect.x, y: rect.y };
        }
    }
    return best;
}

function nextMarker(markers, id, direction) {
    var entries = markers || [];
    var current = null;
    for (var i = 0; i < entries.length; ++i)
        if (entries[i].id === id) current = entries[i];
    if (!current || ["left", "right", "up", "down"].indexOf(direction) < 0) return null;
    var best = null;
    var bestScore = Number.MAX_VALUE;
    for (var j = 0; j < entries.length; ++j) {
        var candidate = entries[j];
        if (candidate.id === id) continue;
        var dx = (candidate.x - current.x) * 1672;
        var dy = (candidate.y - current.y) * 941;
        var parallel = direction === "right" ? dx : direction === "left" ? -dx
                       : direction === "down" ? dy : -dy;
        if (parallel <= 0.001) continue;
        var perpendicular = direction === "left" || direction === "right" ? dy : dx;
        var score = parallel * parallel + perpendicular * perpendicular * 4;
        if (score < bestScore) {
            bestScore = score;
            best = candidate.id;
        }
    }
    return best;
}

function zoomScroll(width, height, oldZoom, newZoom, contentX, contentY) {
    var before = stageGeometry(width, height, oldZoom);
    var after = stageGeometry(width, height, newZoom);
    if (!before.width || !before.height || newZoom <= 1) return { x: 0, y: 0 };
    var nx = (finite(contentX, 0) + width / 2 - before.x) / before.width;
    var ny = (finite(contentY, 0) + height / 2 - before.y) / before.height;
    return {
        x: clamp(nx * after.width + after.x - width / 2, 0, after.width - width),
        y: clamp(ny * after.height + after.y - height / 2, 0, after.height - height)
    };
}
