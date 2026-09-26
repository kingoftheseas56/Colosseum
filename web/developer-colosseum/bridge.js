(function () {
  'use strict';

  const VERSION = 1;
  const SURFACES = Object.freeze(['Home', 'Tankoban', 'Biblio', 'Theatre']);
  let directActionHandler = null;
  let qtBridge = null;
  let qtMounted = false;

  function cleanSurface(value) {
    const name = String(value || 'Home');
    return SURFACES.includes(name) ? name : 'Home';
  }

  function action(type, payload) {
    const body = payload && typeof payload === 'object' ? payload : {};
    const detail = Object.assign({
      type: String(type || ''),
      source: 'developer-web-colosseum',
      version: VERSION
    }, body);

    window.dispatchEvent(new CustomEvent('colosseum:webui-action', { detail }));

    if (typeof directActionHandler === 'function') {
      directActionHandler(detail);
    } else if (window.chrome && window.chrome.webview &&
               typeof window.chrome.webview.postMessage === 'function') {
      window.chrome.webview.postMessage(detail);
    }
    return detail;
  }

  function mount(data) {
    if (!data || typeof data !== 'object')
      throw new TypeError('ColosseumWeb.mount expects a snapshot object.');

    const detail = Object.assign({}, data, {
      surface: cleanSurface(data.surface)
    });
    window.dispatchEvent(new CustomEvent('colosseum:webui-mount', { detail }));
  }

  function patch(data) {
    if (!data || typeof data !== 'object')
      throw new TypeError('ColosseumWeb.patch expects an object.');

    window.dispatchEvent(new CustomEvent('colosseum:webui-patch', { detail: data }));
  }

  function handleHostMessage(message) {
    const envelope = message && typeof message === 'object' ? message : {};
    if (envelope.type === 'mount' && envelope.data)
      mount(envelope.data);
    else if (envelope.type === 'patch' && envelope.data)
      patch(envelope.data);
    else if (envelope.type === 'surface')
      patch({ surface: cleanSurface(envelope.surface) });
  }

  function attachIncomingTransports() {
    if (window.chrome && window.chrome.webview &&
        typeof window.chrome.webview.addEventListener === 'function') {
      window.chrome.webview.addEventListener('message', function (event) {
        handleHostMessage(event.data);
      });
    }

    window.addEventListener('message', function (event) {
      if (event.source === window)
        return;
      handleHostMessage(event.data);
    });
  }

  function attachQtWebChannel() {
    if (!window.qt || !window.qt.webChannelTransport ||
        typeof window.QWebChannel !== 'function')
      return false;

    new window.QWebChannel(window.qt.webChannelTransport, function (channel) {
      qtBridge = channel.objects && channel.objects.ColosseumWebBridge;
      if (!qtBridge)
        return;

      directActionHandler = function (detail) {
        qtBridge.postAction(detail);
      };

      if (qtBridge.snapshotReady && typeof qtBridge.snapshotReady.connect === 'function') {
        qtBridge.snapshotReady.connect(function (snapshot) {
          if (!qtMounted) {
            qtMounted = true;
            mount(snapshot);
          } else {
            patch(snapshot);
          }
        });
      }
      if (qtBridge.patchReady && typeof qtBridge.patchReady.connect === 'function')
        qtBridge.patchReady.connect(patch);

      qtBridge.clientReady();
    });
    return true;
  }

  const api = {
    version: VERSION,
    surfaces: SURFACES.slice(),

    mount: mount,
    patch: patch,

    onAction: function (handler) {
      if (typeof handler !== 'function')
        throw new TypeError('ColosseumWeb.onAction expects a function.');
      directActionHandler = handler;
    },

    clearActionHandler: function () {
      directActionHandler = null;
    },

    action: action,

    requestSnapshot: function (surface, tab) {
      return action('request-snapshot', {
        surface: cleanSurface(surface),
        tab: tab ? String(tab) : ''
      });
    }
  };

  Object.freeze(api.surfaces);
  window.ColosseumWeb = Object.freeze(api);

  attachIncomingTransports();
  attachQtWebChannel();

  function announceReady() {
    action('webui-ready', {
      surfaces: SURFACES.slice(),
      contract: {
        snapshots: true,
        patches: true,
        directCallback: true,
        qtWebChannel: Boolean(window.qt && window.qt.webChannelTransport),
        webview2PostMessage: Boolean(window.chrome && window.chrome.webview)
      }
    });
  }

  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', announceReady, { once: true });
  else
    announceReady();
}());
