// Executed only in the local blank cleanup document, never in provider code.
var clearStorage = [
    "(() => { window.__feriaStorageCleared=''; (async () => { try {",
    "localStorage.clear(); sessionStorage.clear();",
    "if (typeof caches !== 'undefined') for (const key of await caches.keys()) await caches.delete(key);",
    // Qt's loadHtml document cannot enumerate service-worker registrations.
    // Their caches and all credential stores are still cleared below.
    "if (navigator.serviceWorker) { try { for (const worker of await navigator.serviceWorker.getRegistrations()) await worker.unregister(); } catch (error) { if (error.name !== 'InvalidStateError') throw error; } }",
    "if (typeof indexedDB !== 'undefined' && indexedDB.databases) for (const db of await indexedDB.databases()) await new Promise((resolve,reject) => { const request=indexedDB.deleteDatabase(db.name); request.onsuccess=resolve; request.onerror=reject; request.onblocked=reject; });",
    "window.__feriaStorageCleared='done'; } catch (_) { window.__feriaStorageCleared='failed'; } })(); return true; })()"
].join("\n")
