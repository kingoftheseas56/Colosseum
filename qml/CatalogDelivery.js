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

// Per-query page delivery. read() MUST capture the original 5 discoverPageFiltered
// arguments; inspect() MUST capture those same arguments for the nonfetching
// discoverPageFilteredState(). Do not derive query keys in JavaScript.
// done({ok, hasData, value, state, reason, error}) is terminal and called once:
// - ok=true: authoritative success (including an empty page or local SQLite).
// - ok=false, hasData=true: failed remote refresh with validated cached value.
// - ok=false, hasData=false: no validated value; value is null, not an empty page.
// state is the exact native snapshot, retaining key/status/source/retryable/error.
// reason also distinguishes source-changed, disabled, missing, no-signal and exception.
function waitQuery(catalog, read, inspect, done) {
    if (typeof read !== "function" || typeof inspect !== "function" || typeof done !== "function")
        throw new TypeError("waitQuery requires read, inspect and done functions");

    var stopped = false;
    var queryConnected = false, revisionConnected = false, readyConnected = false;
    var key = null, originalSource = null, sawPending = false;
    var value = null, valueFromCache = false, lastState = null;
    var initializing = true, scanning = false, rescan = false, rescanReason = "";

    function disconnect() {
        if (queryConnected) {
            queryConnected = false;
            try { catalog.remoteQueryChanged.disconnect(onQuery); } catch (e) {}
        }
        if (revisionConnected) {
            revisionConnected = false;
            try { catalog.revisionChanged.disconnect(onRevision); } catch (e) {}
        }
        if (readyConnected) {
            readyConnected = false;
            try { catalog.readyChanged.disconnect(onRevision); } catch (e) {}
        }
    }
    function cancel() { if (stopped) return; stopped = true; disconnect(); }
    function finish(ok, hasData, reason, state, error) {
        if (stopped) return;
        stopped = true;
        disconnect();
        done({ok: ok, hasData: hasData, value: hasData ? value : null,
              state: state || null, reason: reason,
              error: error || (state && state.error) || ""});
    }
    function exception(error) {
        finish(false, false, "exception", lastState, String(error));
    }
    function snapshot() {
        try { lastState = inspect(); return lastState; }
        catch (e) { exception(e); return null; }
    }
    function refresh() {
        try { value = read(); return true; }
        catch (e) { exception(e); return false; }
    }
    function examine(trigger) {
        var state = snapshot();
        if (stopped) return;
        if (!state || (state.source !== "local" && state.source !== "remote")) {
            finish(false, false, "invalid-state", state, "Invalid page source"); return;
        }
        if (originalSource === null) originalSource = state.source;
        if (originalSource !== state.source) {
            finish(false, false, "source-changed", state, "Page source changed"); return;
        }
        if (state.source === "local") {
            if (state.status !== "success") {
                finish(false, false, "invalid-state", state, "Invalid local page status"); return;
            }
            // Local pages complete immediately without a remote key, even when empty.
            finish(true, true, "success", state, ""); return;
        }
        if (typeof state.key !== "string" || !state.key.length) {
            finish(false, false, "invalid-state", state, "Missing native query key"); return;
        }
        if (key === null) key = state.key;
        if (trigger === "initial") valueFromCache = state.cached === true;
        if (key !== state.key) {
            finish(false, false, "source-changed", state, "Native query key changed"); return;
        }
        if (sawPending && trigger === "revision" && state.status !== "pending") {
            // Native source reconfiguration emits revisionChanged but NOT queryChanged.
            // An exact success/failure emits queryChanged first in the same Qt turn;
            // a terminal transition seen only on revision is source invalidation.
            finish(false, false, "source-changed", state, "Remote source invalidated"); return;
        }
        if (state.status === "pending" && state.enabled !== false) {
            if (!queryConnected) {
                finish(false, false, "no-signal", state, "Missing remoteQueryChanged subscription");
            } else {
                sawPending = true;
                valueFromCache = state.cached === true;
            }
            return;
        }
        if (state.status === "success" && state.cached === true) {
            // A pending cached page is NOT success. Read again only after it settles;
            // an unrelated notification can never complete a still-pending key.
            if (sawPending && !refresh()) return;
            if (sawPending) {
                state = snapshot();
                if (stopped) return;
                if (!state || state.source !== "remote" || state.key !== key) {
                    finish(false, false, "source-changed", state, "Remote source changed while reading"); return;
                }
                if (state.status === "pending" && state.enabled !== false) return;
                if (state.status !== "success" || state.cached !== true) {
                    scan("rescan"); return;
                }
            }
            finish(true, true, "success", state, ""); return;
        }
        if (state.status === "failure") {
            // read() previously returned validated stale cache, if present.
            // Do not fetch again on a failure just to get an uncached fallback.
            if (state.cached === true && !valueFromCache) {
                if (!refresh()) return;
                state = snapshot();
                if (stopped) return;
                if (!state || state.source !== "remote" || state.key !== key) {
                    finish(false, false, "source-changed", state, "Remote source changed while reading"); return;
                }
                if (state.status === "pending") { sawPending = true; return; }
                if (state.status !== "failure") { scan("rescan"); return; }
                valueFromCache = state.cached === true;
            }
            finish(false, state.cached === true && valueFromCache, "failure", state, state.error); return;
        }
        // Missing means no in-flight request. Disabled or at-cap requests may return
        // the method's empty fallback; that is NOT a successful empty page.
        finish(false, false, state.enabled === false ? "disabled" :
               state.status === "missing" ? "missing" : "invalid-state", state,
               state.enabled === false ? "Remote page disabled" :
               state.status === "missing" ? "Page request was not queued" : "Invalid page status");
    }
    function scan(reason) {
        if (stopped) return;
        if (scanning) {
            rescan = true;
            if (reason === "query" || rescanReason !== "query") rescanReason = reason;
            return;
        }
        scanning = true;
        try {
            var next = reason;
            do {
                rescan = false;
                rescanReason = "";
                examine(next);
                next = rescanReason || "rescan";
            } while (rescan && !stopped);
        } finally { scanning = false; }
    }
    function onQuery(changedKey) {
        // A read may enqueue synchronously, but native queryChanged is deferred.
        // Until inspect() captures our key, the initial snapshot owns resolution.
        if (!initializing && key !== null && changedKey === key) scan("query");
    }
    function onRevision() { if (!initializing) scan("revision"); }

    try {
        if (catalog && catalog.remoteQueryChanged &&
            typeof catalog.remoteQueryChanged.connect === "function" &&
            typeof catalog.remoteQueryChanged.disconnect === "function") {
            catalog.remoteQueryChanged.connect(onQuery); queryConnected = true;
        }
        if (catalog && catalog.revisionChanged &&
            typeof catalog.revisionChanged.connect === "function" &&
            typeof catalog.revisionChanged.disconnect === "function") {
            catalog.revisionChanged.connect(onRevision); revisionConnected = true;
        }
        if (catalog && catalog.readyChanged &&
            typeof catalog.readyChanged.connect === "function" &&
            typeof catalog.readyChanged.disconnect === "function") {
            catalog.readyChanged.connect(onRevision); readyConnected = true;
        }
    } catch (e) {
        exception(e); return cancel;
    }
    // Subscribe BEFORE the fetching read, then inspect the exact native key.
    // Any synchronous reentrant signal is reconciled by the post-read snapshot.
    var started = refresh();
    initializing = false;
    if (started && !stopped) scan("initial");
    return cancel;
}
