// Transport/source adapter only. The shared paper_glue owns Foliate behavior.
(() => {
  'use strict'
  const origin = 'https://appassets.androidplatform.net'
  const publication = /^\/publication\/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\/book\.epub$/
  let generation = 0
  const commands = new Set(['open', 'next', 'prev', 'goTo', 'setAppearance', 'search',
    'clearSearch', 'addHighlight', 'removeHighlight', 'clearSelection', 'setReadAlongStyle',
    'paintReadAlong', 'clearReadAlong', 'ensureReadAlongVisible', 'navigateReadAlong'])
  window.colosseumDisablePublicationScripts = true
  window.bridge = {
    epubOnly: true,
    paperEvent(name, json) {
      const payload = JSON.parse(json)
      const eventGeneration = name === 'glueLoaded' ? 0 : (payload.gen ?? generation)
      window.ColosseumPaper.postMessage(JSON.stringify({ event: name, generation: eventGeneration, payload }))
    },
    async filesReadBlob(source) {
      const url = new URL(source)
      if (url.origin !== origin || !publication.test(url.pathname) || url.search || url.hash)
        throw new Error('Unauthorized publication URL')
      const controller = new AbortController()
      const timeout = setTimeout(() => controller.abort(), 20000)
      try {
        const response = await fetch(url.href, { signal: controller.signal, credentials: 'omit', cache: 'no-store' })
        if (!response.ok) throw new Error('The book could not be read (' + response.status + ')')
        return new File([await response.blob()], 'book.epub', { type: 'application/epub+zip' })
      } finally { clearTimeout(timeout) }
    },
  }
  window.androidPaperDispatch = command => {
    if (!command || !commands.has(command.name) || !Array.isArray(command.args)) return
    if (command.name === 'open') {
      if (!Number.isSafeInteger(command.args[2]) || command.args[2] <= 0) return
      generation = command.args[2]
    }
    const invoke = window.paper?.[command.name]
    if (typeof invoke === 'function') invoke(...command.args)
  }
})()
