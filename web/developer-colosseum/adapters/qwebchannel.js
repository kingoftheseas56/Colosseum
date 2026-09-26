// adapters/qwebchannel.js — real mode. Talks to native ColosseumWebBridge over QWebChannel (CONTRACT §2).
// Only this file knows about qt.webChannelTransport.
(function (CW) {
  'use strict';

  CW.adapters = CW.adapters || {};

  CW.adapters.qwebchannel = function () {
    if (!(window.qt && window.qt.webChannelTransport && typeof window.QWebChannel === 'function'))
      return null;

    const feedFns = [];
    const shellFns = [];
    const ready = new Promise(resolve => {
      new window.QWebChannel(window.qt.webChannelTransport, channel => {
        const bridge = channel.objects && channel.objects.ColosseumWebBridge;
        if (!bridge) { console.error('[qwebchannel] ColosseumWebBridge is not registered'); return; }
        bridge.feedEvent.connect(env => feedFns.forEach(fn => fn(env)));
        bridge.shellEvent.connect(state => shellFns.forEach(fn => fn(state)));
        bridge.shellState(state => shellFns.forEach(fn => fn(state)));
        resolve(bridge);
      });
    });

    const call = (method, ...args) => ready.then(bridge =>
      new Promise(resolve => bridge[method](...args, resolve)));

    return {
      name: 'qwebchannel',
      onFeedEvent: fn => feedFns.push(fn),
      onShellEvent: fn => shellFns.push(fn),
      subscribe: (feed, params) => call('subscribe', feed, params),
      unsubscribe: id => { ready.then(bridge => bridge.unsubscribe(id)); },
      more: (id, sectionId) => call('more', id, sectionId),
      act: (action, payload) => call('act', action, payload)
    };
  };
})(window.CW = window.CW || {});
