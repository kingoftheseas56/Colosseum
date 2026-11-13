.pragma library

// The existing transport/player.ini keys remain the only persisted owner.
function read(store) {
    store.sync();
    var quality = String(store.value("smartPlayQuality", "1080p"));
    return quality === "4k" || quality === "off" ? quality : "1080p";
}

function write(store, quality) {
    var previous = read(store);
    if (quality !== "4k" && quality !== "off") quality = "1080p";
    if (quality !== "off") store.setValue("smartPlayLastQuality", quality);
    else if (previous !== "off") store.setValue("smartPlayLastQuality", previous);
    store.setValue("smartPlayQuality", quality);
    store.sync();
    return quality;
}

function setEnabled(store, enabled) {
    var quality = read(store);
    if (!enabled) return write(store, "off");
    if (quality === "off")
        quality = store.value("smartPlayLastQuality", "1080p") === "4k" ? "4k" : "1080p";
    return write(store, quality);
}
