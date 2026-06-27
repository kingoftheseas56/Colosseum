// BUILD_OVERHAUL: Tiny pub/sub event bus for inter-module communication
(function () {
  'use strict';

  const handlers = {};

  window.booksReaderBus = {
    on: function (ev, fn) {
      if (!handlers[ev]) handlers[ev] = [];
      handlers[ev].push(fn);
    },
    off: function (ev, fn) {
      if (!handlers[ev]) return;
      handlers[ev] = handlers[ev].filter(function (f) { return f !== fn; });
    },
    emit: function (ev) {
      var args = Array.prototype.slice.call(arguments, 1);
      var list = handlers[ev];
      if (!list) return;
      for (var i = 0; i < list.length; i++) {
        try { list[i].apply(null, args); } catch (e) { /* swallow */ }
      }
    },
    clear: function () {
      for (var k in handlers) delete handlers[k];
    },
  };
})();
