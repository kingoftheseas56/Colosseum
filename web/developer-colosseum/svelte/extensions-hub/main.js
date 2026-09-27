// page.extensionsHub — Chain / House / Store (CONTRACT §16). Svelte 5, built to ../../surfaces/extensions-hub/.
import { mount, unmount } from 'svelte';
import App from './App.svelte';

(function (CW) {
  CW.router.register('page.extensionsHub', {
    mount(el, route, env) {
      const app = mount(App, { target: el, props: { route, env } });
      return {
        update(next) { app.setRoute(next); },
        unmount() { unmount(app); }
      };
    }
  });
})(window.CW = window.CW || {});
