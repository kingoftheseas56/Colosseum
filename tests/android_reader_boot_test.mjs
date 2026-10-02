import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import vm from 'node:vm'
import { File } from 'node:buffer'

const messages = [], commands = [], fetched = []
const window = {
  ColosseumPaper: { postMessage: text => messages.push(JSON.parse(text)) },
  paper: { open: (...args) => commands.push(args), next: () => commands.push('next') },
}
const context = vm.createContext({ window, URL, File, AbortController, setTimeout, clearTimeout,
  fetch: async (url, options) => {
    fetched.push({ url, options })
    return new Response(new Blob(['EPUB bytes']), { status: 200 })
  },
})
vm.runInContext(await readFile(new URL('../resources/reader2/android_boot.js', import.meta.url), 'utf8'), context)
const url = 'https://appassets.androidplatform.net/publication/00000000-1111-2222-3333-444444444444/book.epub'
window.androidPaperDispatch({ name: 'open', args: [url, 'resume-cfi', 7] })
assert.equal(commands.length, 1)
assert.equal(commands[0][1], 'resume-cfi')
window.bridge.paperEvent('ready', '{"gen":7,"metadata":{}}')
window.androidPaperDispatch({ name: 'open', args: [url, '', 8] })
window.bridge.paperEvent('relocated', '{"gen":7,"cfi":"old"}')
assert.equal(messages[1].generation, 7, 'late callbacks must never be relabeled as the new book')
window.bridge.paperEvent('toggleChrome', '{}')
assert.equal(messages[2].generation, 8)
window.androidPaperDispatch({ name: 'constructor', args: ['bad'] })
window.androidPaperDispatch({ name: 'open', args: [url, '', -1] })
assert.equal(commands.length, 2, 'unlisted commands and invalid generations are rejected')
const file = await window.bridge.filesReadBlob(url)
assert.equal(file.name, 'book.epub')
assert.equal(await file.text(), 'EPUB bytes')
assert.equal(fetched.length, 1)
assert.equal(fetched[0].options.credentials, 'omit')
for (const bad of ['content://provider/document/1', 'file:///private/book.epub',
  'https://evil.test/book.epub', url + '?source=other', url.replace('book.epub', '../book.epub')]) {
  await assert.rejects(window.bridge.filesReadBlob(bad), /Unauthorized publication URL/)
}
assert.equal(fetched.length, 1, 'unapproved URLs never reach fetch')
assert.equal(window.colosseumDisablePublicationScripts, true)
console.log('ANDROID_READER_BOOT_OK')
