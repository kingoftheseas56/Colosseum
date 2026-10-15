.pragma library

// Chart-local bounds preserve the artwork as zoom, fonts and detail change.
function protectedAreas(chart, locations) {
    var areas = [];
    if (!chart) return areas;
    function visit(item) {
        if (!item || !item.visible) return;
        if (typeof item.kind === "string" && item.fineDetail !== undefined) {
            if (item.kind === "region") return;
            var origin = item.mapToItem(chart, 0, 0);
            areas.push({ x: origin.x, y: origin.y, width: item.width, height: item.height });
            return;
        }
        if (item.paintedWidth !== undefined && item.text !== undefined && item.text.length) {
            var dx = item.horizontalAlignment === Qt.AlignHCenter ? (item.width - item.paintedWidth) / 2
                   : item.horizontalAlignment === Qt.AlignRight ? item.width - item.paintedWidth : 0;
            var point = item.mapToItem(chart, dx, 0);
            areas.push({ x: point.x - 3, y: point.y - 3, width: item.paintedWidth + 6, height: item.paintedHeight + 6 });
        }
        var children = item.children || [];
        for (var i = 0; i < children.length; ++i) visit(children[i]);
    }
    visit(chart);
    // Keep hard map boundaries clear. Individual landmark items already protect their own art.
    for (var j = 0; j < locations.length; ++j) {
        var place = locations[j];
        if (place.id === "reverse-mountain") {
            areas.push({ x: 0, y: 0, width: 145, height: 960 });
            areas.push({ x: 1535, y: 0, width: 145, height: 960 });
        }
        if (place.kind === "boundary" && place.w && place.h)
            areas.push({ x: place.x * 1680 - place.w / 2, y: place.y * 960 - place.h / 2, width: place.w, height: place.h });
    }
    return areas;
}

function circleOverlapsRect(x, y, radius, rect) {
    var dx = x - Math.max(rect.x, Math.min(rect.x + rect.width, x));
    var dy = y - Math.max(rect.y, Math.min(rect.y + rect.height, y));
    return dx * dx + dy * dy < radius * radius;
}

function placeBadges(markers, diameter, areas, focusOutline) {
    var positions = {}, placed = [], radius = diameter / 2 + Math.max(6, (focusOutline || 0) / 2 + 1);
    var minimumSeparation = diameter * 1.6;
    function clear(x, y) {
        if (x - radius < 26 || x + radius > 1654 || y - radius < 26 || y + radius > 934) return false;
        for (var i = 0; i < areas.length; ++i)
            if (circleOverlapsRect(x, y, radius, areas[i])) return false;
        for (var j = 0; j < placed.length; ++j) {
            var dx = x - placed[j].x, dy = y - placed[j].y;
            if (dx * dx + dy * dy < minimumSeparation * minimumSeparation) return false;
        }
        return true;
    }
    // Reserve the crowded lower coastline first; upper seas offer more spare water.
    var ordered = markers.slice().sort(function(a, b) { return b.y - a.y; });
    for (var m = 0; m < ordered.length; ++m) {
        var marker = ordered[m], ax = marker.x * 1680, ay = marker.y * 960;
        var point = clear(ax, ay) ? { x: ax, y: ay } : null;
        // Keep each badge associated with its island instead of searching the whole sea.
        for (var distance = 8; !point && distance <= 128; distance += 8) {
            for (var step = 0; step < 32; ++step) {
                var angle = step * Math.PI / 16;
                var x = ax + Math.cos(angle) * distance, y = ay + Math.sin(angle) * distance;
                if (clear(x, y)) { point = { x: x, y: y }; break; }
            }
        }
        positions[marker.id] = { id: marker.id, x: point ? point.x / 1680 : marker.x,
            y: point ? point.y / 960 : marker.y, visible: point !== null };
        if (point) placed.push(point);
    }
    return positions;
}
