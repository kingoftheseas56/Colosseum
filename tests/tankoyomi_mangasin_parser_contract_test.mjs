import assert from 'node:assert/strict';
import { createCipheriv, createDecipheriv, createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import path from 'node:path';

const require = createRequire(import.meta.url);
const provider = require(path.resolve('extensions/tankoyomi/languages/es/mangasin.js'));
const BASE = 'https://m440.in';
const SERIES = BASE + '/manga/solo-bug-player';
const CHAPTER = SERIES + '/156-chp9g';
const PASSWORD = 'X^Ib1O*HLVh%3W2t';
const SALT = Buffer.from('0102030405060708', 'hex');

function derived(passphrase, salt) {
  let bytes = Buffer.alloc(0), previous = Buffer.alloc(0);
  while (bytes.length < 48) {
    previous = createHash('md5').update(Buffer.concat([previous, Buffer.from(passphrase), salt])).digest();
    bytes = Buffer.concat([bytes, previous]);
  }
  return { key: bytes.subarray(0, 32), iv: bytes.subarray(32, 48) };
}
function encryptedFixture(rows) {
  const { key, iv } = derived(PASSWORD, SALT);
  const cipher = createCipheriv('aes-256-cbc', key, iv);
  const payload = JSON.stringify(JSON.stringify(rows));
  const ct = Buffer.concat([cipher.update(payload), cipher.final()]).toString('base64');
  return '<html><script>const UsaPoncho = ' +
    JSON.stringify(JSON.stringify({ ct, iv: iv.toString('hex'), s: SALT.toString('hex') })) +
    ';</script></html>';
}
function decrypt(ct, saltHex, password) {
  assert.equal(password, PASSWORD, 'expected observed public MangaSin chapter key');
  const { key, iv } = derived(password, Buffer.from(saltHex, 'hex'));
  const decipher = createDecipheriv('aes-256-cbc', key, iv);
  return Buffer.concat([decipher.update(Buffer.from(ct, 'base64')), decipher.final()]).toString('utf8');
}

assert.equal(provider.id, 'mangasin');
assert.equal(provider.language, 'es');
assert.equal(provider.baseUrl, BASE);
for (const method of ['searchSeries', 'getChapters', 'getPages']) {
  assert.equal(typeof provider[method], 'function', method + ' contract');
}
console.log('ok — Spanish MangaSin provider contract');

{
  const calls = [];
  const rows = [
    { data: 'solo-bug-player', value: 'Solo Bug Player', label: '<img onerror="x()">' },
    { data: 'solo-bug-player', value: 'Duplicate' },
    { data: 'solo-camp-debut', value: 'Solo Camp Debut' },
    { data: 'https://evil.example/path', value: 'Off-site' },
    { data: 'foo', value: '' }
  ];
  const found = await provider.searchSeries({ fetchJson: async url => { calls.push(url); return rows; } }, ' solo ');
  assert.deepEqual(found.map(x => x.url), [SERIES, BASE + '/manga/solo-camp-debut']);
  assert.ok(found.every(x => x.source === 'mangasin' && x.language === 'es'));
  assert.equal(calls[0], BASE + '/search?q=solo');
  assert.deepEqual(provider.searchSeries({}, '  '), []);
  console.log('ok — search JSON slugs, duplicate suppression, unsafe slug rejection');
}

{
  const html = encryptedFixture([
    { id: '529541', slug: '156-chp9g', name: 'Capítulo 156', number: '156' },
    { id: '529540', slug: '155-ab1', name: 'Capítulo 155', number: '155' },
    { id: 'dup', slug: '155-ab1', name: 'Duplicate', number: '155' },
    { id: 'evil', slug: 'https://evil.example', name: 'Off-site', number: '3' },
    { id: 'odd', slug: 'x', name: 'Prólogo', number: '' }
  ]);
  const calls = [];
  const ctx = {
    fetchText: async url => { calls.push(url); return html; },
    decryptCryptoJs: decrypt
  };
  const chapters = await provider.getChapters(ctx, { url: SERIES });
  assert.equal(chapters.length, 3);
  assert.deepEqual(chapters.map(x => x.url), [SERIES + '/155-ab1', CHAPTER, SERIES + '/x']);
  assert.deepEqual(chapters.slice(0, 2).map(x => x.number), [155, 156]);
  assert.equal(chapters[1].id, '529541');
  assert.ok(chapters.every(x => x.source === 'mangasin' && x.language === 'es'));
  assert.deepEqual(calls, [SERIES]);
  console.log('ok — salted CryptoJS chapter envelope, sort, identities, off-site rejection');

  await assert.rejects(provider.getChapters({
    fetchText: async () => '<html>no chapter data</html>', decryptCryptoJs: decrypt
  }, { url: SERIES }), /chapter data is missing/);
  await assert.rejects(provider.getChapters({
    fetchText: async () => html, decryptCryptoJs: () => ''
  }, { url: SERIES }), /could not be decrypted/);
  assert.throws(() => provider.getChapters(ctx, { url: 'https://evil.example/foo' }), /series URL/);
  console.log('ok — missing payload, crypto failure and foreign metadata URLs fail closed');
}

{
  const img1 = 'https://s1.m440.in/uploads/manga/solo-bug-player/chapters/156-chp9g/1.webp';
  const img2 = 'https://s1.m440.in/uploads/manga/solo-bug-player/chapters/156-chp9g/2.webp';
  const html = '<html><div id="all" style="padding:0">' +
    '<img class="img-responsive" src="https://m440.in/images/loading.gif" data-src=' +
      "'" + img1 + "'" + '>' +
    '<img class="img-responsive" data-src="' + img1 + '">' +
    '<img class="img-responsive" src="' + img2 + '">' +
    '<img class="img-responsive" data-src="https://evil.example/hijack.png">' +
    '<img class="img-responsive" src="http://s1.m440.in/not-https.webp">' +
    '<img class="img-responsive" data-src="https://localhost/hijack.png">' +
    '<img class="not-a-page" src="https://s1.m440.in/ignored.webp">' +
    '<img data-class="img-responsive" src="https://s1.m440.in/spoof.webp">' +
    '</div></html>';
  const pages = await provider.getPages({ fetchText: async u => {
    assert.equal(u, CHAPTER); return html;
  } }, { url: CHAPTER });
  assert.deepEqual(pages, [{ index: 0, url: img1 }, { index: 1, url: img2 }]);
  assert.deepEqual(await provider.getPages({ fetchText: async () => '<div id="other"></div>' }, CHAPTER), []);
  assert.throws(() => provider.getPages({}, { url: 'file:///etc/passwd' }), /chapter URL/);
  console.log('ok — reader lazy images, ordering, duplicates, and safe HTTPS-only pages');
}

{
  // Historical One Piece chapters use a fake https:// hostname containing
  // Base64 of a percent-encoded Blogspot URL, unlike the direct MangaSin WebP.
  const wrap = url => 'https://' + Buffer.from(encodeURIComponent(url)).toString('base64');
  const valid = 'https://4.bp.blogspot.com/-chapter/s0/001.jpg';
  const direct = 'https://s1.m440.in/uploads/manga/one-piece/chapters/1/002.webp';
  const html = '<div id="all">' +
    '<img class="img-responsive" data-src="' + wrap(valid) + '">' +
    '<img class="img-responsive" data-src="' + wrap(valid) + '">' +
    '<img class="img-responsive" data-src="' + wrap('http://4.bp.blogspot.com/bad.jpg') + '">' +
    '<img class="img-responsive" data-src="' + wrap('https://127.0.0.1/private.jpg') + '">' +
    '<img class="img-responsive" data-src="' + wrap('https://4.bp.blogspot.com.evil.test/a.jpg') + '">' +
    '<img class="img-responsive" data-src="' + wrap('https://evil.example/page.jpg') + '">' +
    '<img class="img-responsive" data-src="' + direct + '">' +
    '</div>';
  let decodes = 0;
  const ctx = {
    fetchText: async u => { assert.equal(u, SERIES + '/1'); return html; },
    decodeBase64: data => { decodes++; return Buffer.from(data, 'base64').toString('utf8'); }
  };
  const pages = await provider.getPages(ctx, { url: SERIES + '/1' });
  assert.deepEqual(pages, [{ index: 0, url: valid }, { index: 1, url: direct }]);
  assert.equal(decodes, 6);
  console.log('ok — encoded historical One Piece images decoded, unsafe origins rejected');
}

console.log('PASS — MangaSin bundled provider fixtures');
