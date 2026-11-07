import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import vm from 'node:vm'

// Exercise the production event branch. The full ReaderShell requires native registrations;
// this bounded check does not claim Android window, WebView selection or IME execution.
const source = await readFile(new URL('../qml/reader2/ReaderShell.qml', import.meta.url), 'utf8')
const start = source.indexOf('} else if (name === "selectionCleared") {')
const end = source.indexOf('} else if (name === "selection") {', start)
assert.ok(start >= 0 && end > start, 'ReaderShell selection event branch must be present')
const body = source.slice(source.indexOf('{', start) + 1, end)

function clearSelection({ nativeOverlay, noteEditing, accepted = true, shown = true }) {
  let dismissals = 0
  const shell = {
    selMenuShown: shown,
    selText: 'stashed selection',
    selCfi: 'epubcfi(/6/4!/4/2)',
    acceptPaperBookEvent: () => accepted,
    dismissSelectionMenu() {
      ++dismissals
      this.selMenuShown = false
      this.selText = ''
      this.selCfi = ''
    },
  }
  vm.runInNewContext(`(function(p) { ${body}\n})({gen: 7})`, {
    shell, selectionMenu: { nativeOverlay, noteEditing },
  })
  return { shell, dismissals }
}

const editing = clearSelection({ nativeOverlay: true, noteEditing: true })
assert.equal(editing.dismissals, 0, 'native note focus must not discard the draft')
assert.equal(editing.shell.selMenuShown, true)
assert.equal(editing.shell.selText, 'stashed selection')
assert.equal(editing.shell.selCfi, 'epubcfi(/6/4!/4/2)')

for (const state of [
  { nativeOverlay: true, noteEditing: false },
  { nativeOverlay: false, noteEditing: true },
  { nativeOverlay: false, noteEditing: false },
]) {
  assert.equal(clearSelection(state).dismissals, 1,
               'ordinary selection clearing must dismiss the menu')
}
assert.equal(clearSelection({ nativeOverlay: false, noteEditing: false, accepted: false }).dismissals,
             0, 'rejected old-book events must remain inert')
assert.equal(clearSelection({ nativeOverlay: false, noteEditing: false, shown: false }).dismissals,
             0, 'a hidden selection menu must remain inert')
console.log('ANDROID_READER_SELECTION_TOUCH_OK')
