/* Controlled page adapter. Intentionally not an HTMLMediaElement emulation. */
'use strict';
window.labReady = new Promise(resolve => {
  new QWebChannel(qt.webChannelTransport, async channel => {
    const remote = channel.objects.player;
    const invoke = (object, method, ...args) => new Promise(resolve => object[method](...args, resolve));
    const boot = await invoke(remote, 'bootstrap');
    let session = boot.session;
    const documentId = boot.document;
    let state = {}, lastResult = {}, interactions = [];
    const aperture = document.getElementById('aperture');
    const adapter = new EventTarget();
    adapter.load = async fixture => {
      const result = await invoke(remote, 'loadFixture', documentId, session, fixture);
      if (result.ok) { session = result.session; await adapter.layout(); }
      lastResult = result; return result;
    };
    adapter.act = async (action, value = 0) => {
      const result = await invoke(remote, 'act', documentId, session, action, value);
      if (result.ok && action === 'close') session = '';
      lastResult = result; return result;
    };
    adapter.layout = () => {
      const r = aperture.getBoundingClientRect();
      return invoke(remote, 'layout', documentId, {x:r.x,y:r.y,width:r.width,height:r.height,visible:r.width>0&&r.height>0});
    };
    remote.state.connect(next => {
      if (next.document !== documentId || next.session !== session) return;
      state = next;
      const volumeInput = document.getElementById("volume");
      if (document.activeElement !== volumeInput && Number.isFinite(next.volume)) volumeInput.value = next.volume;
      document.getElementById('time').textContent = `${Number(next.position).toFixed(2)} / ${Number(next.duration).toFixed(2)}`;
      document.getElementById('status').textContent = `${next.fixture || 'closed'} | ${next.error || (next.ended?'ended':next.loaded?(next.paused?'paused':'playing'):'loading')}\nDecoded: ${next.decodedWidth} × ${next.decodedHeight} | volume ${next.volume} | session ${next.session.slice(0,8)}`;
      adapter.dispatchEvent(new CustomEvent('state', {detail: next}));
    });
    const on = (id, action) => document.getElementById(id).addEventListener('click', async event => {
      const entry = {id,type:event.type,isTrusted:event.isTrusted,at:performance.now()}; interactions.push(entry);
      try { entry.result = await action(); } catch(error) { entry.error = String(error); }
      entry.done = true;
    });
    on('loadH264', () => adapter.load('h264'));
    on('loadFfv1', () => adapter.load('ffv1'));
    on('loadMissing', () => adapter.load('missing'));
    on('play', () => adapter.act('play'));
    on('pause', () => adapter.act('pause'));
    on('seekButton', () => adapter.act('seek', Number(document.getElementById('seek').value)));
    on('volumeButton', () => adapter.act('volume', Number(document.getElementById('volume').value)));
    on('close', () => adapter.act('close'));
    on('menuButton', () => { document.getElementById('menu').hidden = !document.getElementById('menu').hidden; return {ok:true}; });
    on('captionButton', () => { document.getElementById('caption').hidden = !document.getElementById('caption').hidden; return {ok:true}; });
    on('fullscreen', async () => {
      const full = !aperture.classList.contains('full');
      const result = await adapter.act('fullscreen', Number(full));
      if (result.ok) { aperture.classList.toggle('full', full); await adapter.layout(); }
      return result;
    });
    on('reload', () => { location.reload(); });
    const observer = new ResizeObserver(() => adapter.layout()); observer.observe(aperture);
    addEventListener('scroll', adapter.layout, {passive:true}); addEventListener('resize', adapter.layout);
    addEventListener('pagehide', () => { observer.disconnect(); });
    await adapter.layout();
    const rect = id => { const r = document.getElementById(id).getBoundingClientRect(); return {x:r.x,y:r.y,width:r.width,height:r.height}; };
    window.lab = {adapter,remote,probe:channel.objects.probe,invoke,boot,documentId,
      get session(){return session}, get state(){return state}, get lastResult(){return lastResult}, interactions,
      ui: () => ({aperture:rect('aperture'),caption:rect('caption'),captionHidden:document.getElementById('caption').hidden,
        menu:rect('menu'),menuHidden:document.getElementById('menu').hidden,viewport:{width:innerWidth,height:innerHeight},
        devicePixelRatio,scrollY,interactions:[...interactions]})};
    resolve(window.lab);
  });
});
