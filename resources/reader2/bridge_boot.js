// bridge_boot.js — injected at DocumentCreation (AFTER qwebchannel.js) by
// Paper.qml. Wires the native Reader2Bridge (registered on the WebChannel as
// "bridge") onto window.bridge, so the paper's classic-script glue can pull book
// bytes (filesRead) and push events (paperEvent). The QWebChannel handshake is
// async — window.bridge appears once the callback fires; the glue's waitForBridge
// bounds on exactly that. Ported from the proven qt_bridge_shim.js pattern.
//
// [Agent 2 (Claude), biblio]
(function () {
  try {
    var transport = (typeof qt !== 'undefined') && qt.webChannelTransport;
    if (!transport) { console.error('[bridge_boot] no qt.webChannelTransport'); return; }
    if (typeof QWebChannel === 'undefined') {
      console.error('[bridge_boot] QWebChannel undefined — qwebchannel.js did not load first');
      return;
    }
    new QWebChannel(transport, function (channel) {
      window.bridge = channel.objects.bridge;
      console.log('[bridge_boot] window.bridge ready');
    });
  } catch (e) {
    console.error('[bridge_boot] threw:', e && (e.stack || e.message || String(e)));
  }
})();
