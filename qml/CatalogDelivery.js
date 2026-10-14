// Async delivery over synchronous cache accessors; no blocking network on the GUI thread.
.pragma library
function available(catalog) {
    return !!catalog && (typeof catalog.available === "function" ? catalog.available()
                    : (typeof catalog.ready === "function" && catalog.ready()));
}
// Own only this callback subscription. Cancelling does not abort other consumers' requests.
function wait(catalog, read, done) {
    var stopped = false;
    var connected = false;
    function disconnect() {
        if (!connected) return;
        connected = false;
        try { catalog.revisionChanged.disconnect(deliver); } catch (e) {}
        try { catalog.requestFailed.disconnect(deliver); } catch (e) {}
    }
    function deliver() {
        if (stopped) return;
        var result = read();
        var waiting = catalog && (typeof catalog.remoteWaiting === "function" ? catalog.remoteWaiting()
                    : (typeof catalog.remotePending === "function" && catalog.remotePending()));
        if (waiting) return;
        stopped = true;
        disconnect();
        done(result);
    }
    if (catalog && catalog.remoteEnabled && catalog.revisionChanged && catalog.requestFailed) {
        catalog.revisionChanged.connect(deliver);
        catalog.requestFailed.connect(deliver);
        connected = true;
    }
    deliver();
    return function() { stopped = true; disconnect(); };
}
