// mock_bridge.js — browser-only stand-in for the Qt host bridge.
//
// In the real app QML injects `window.bridge` (paperEvent forwards to C++, filesRead
// returns book bytes as base64). In the browser bench we define it here and add a file
// picker so a real book can be loaded from disk.
//
// Load this as a CLASSIC script BEFORE paper_glue.js (which is a module) so window.bridge
// exists when the glue emits `glueLoaded` and when the user opens a book.
//
// [Agent 2 (Claude), biblio]

(function () {
  window.__mockBookB64 = null

  window.bridge = {
    // Events UP: paper -> host. Bench just logs them so the console is the event tape.
    paperEvent: (name, json) => console.log('[event]', name, json),
    // Bytes DOWN: host -> paper. Returns the last picked book as base64 via callback.
    filesRead: (path, cb) => cb(window.__mockBookB64),
  }

  const readAsBase64 = file => new Promise((resolve, reject) => {
    const r = new FileReader()
    r.onerror = () => reject(r.error)
    r.onloadend = () => resolve(String(r.result).split(',')[1] || '') // strip data: prefix
    r.readAsDataURL(file)
  })

  const mountPicker = () => {
    const bar = document.createElement('div')
    bar.style.cssText =
      'position:fixed;top:0;left:0;z-index:99999;padding:6px 10px;' +
      'background:rgba(20,20,24,.85);color:#e6e1d5;font:12px system-ui;' +
      'border-bottom-right-radius:8px;'
    bar.innerHTML = '<span style="margin-right:8px">reader2 bench</span>'

    const input = document.createElement('input')
    input.type = 'file'
    input.accept = '.epub,.mobi,.azw3,.fb2,.fbz,.cbz,.pdf,.txt'
    input.style.color = '#e6e1d5'
    input.addEventListener('change', async () => {
      const file = input.files && input.files[0]
      if (!file) return
      try {
        window.__mockBookB64 = await readAsBase64(file)
        console.log('[bench] picked', file.name, file.size, 'bytes')
        if (window.paper && typeof window.paper.open === 'function') {
          window.paper.open(file.name, '')
        } else {
          console.warn('[bench] window.paper not ready yet')
        }
      } catch (e) {
        console.error('[bench] read failed', e)
      }
    })

    bar.appendChild(input)
    document.body.appendChild(bar)
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', mountPicker)
  } else {
    mountPicker()
  }
})()
