import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'

const glue = readFileSync(new URL('../resources/reader2/paper_glue.js', import.meta.url), 'utf8').replace(/\r\n/g, '\n')
const source = /const fontUrl = rel => \{[\s\S]*?\n\}/.exec(glue)?.[0]
assert.ok(source, 'exercise the real shared font URL resolver')
const resolve = window => new Function('window', 'URL', source + '\nreturn fontUrl')(window, URL)
const android = resolve({ location: { href: 'https://appassets.androidplatform.net/assets/reader2/android_paper.html' },
  colosseumFontBase: '/assets/colosseum-runtime/assets/fonts/' })
assert.equal(android('../../assets/fonts/Literata-Regular.ttf'),
  'https://appassets.androidplatform.net/assets/colosseum-runtime/assets/fonts/Literata-Regular.ttf')
assert.equal(android('../../assets/fonts/Inter-Regular.otf'),
  'https://appassets.androidplatform.net/assets/colosseum-runtime/assets/fonts/Inter-Regular.otf')
const desktop = resolve({ location: { href: 'file:///app/resources/reader2/paper.html' } })
assert.equal(desktop('../../assets/fonts/Literata-Regular.ttf'), 'file:///app/assets/fonts/Literata-Regular.ttf')
console.log('ANDROID_READER_FONTS_OK')
