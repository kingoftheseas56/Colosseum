// Exercise port.more with a 250-episode derived fixture and walk to the last unit.
import { serve, launch, WEB, routeHash } from './lib.mjs';

const negative = process.argv.includes('--negative');
const route = { name: 'detail', kind: 'theatre',
  params: { id: 'tt-slice4-paging', type: 'series', title: 'Paging Test' } };
const { server, base } = await serve();
const browser = await launch();
let failure = '';
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  await page.goto(base + WEB + 'index.html' + routeHash(route));
  await page.waitForFunction(() => document.querySelectorAll('.title-unit').length === 100);
  for (const count of [200, 250]) {
    await page.locator('[data-key="theatre.episodes.more"]').click();
    await page.waitForFunction(wanted => document.querySelectorAll('.title-unit').length === wanted, count);
  }
  const last = page.locator('.title-unit').last();
  const id = await last.getAttribute('data-unit-id');
  if (id !== 'tt-slice4-paging:1:250') failure = `last id was ${id}`;
  if (negative) await last.locator('[data-key$=".play"]').evaluate(el => el.removeAttribute('data-focus'));
  const lastFocusable = await last.locator('[data-key$=".play"][data-focus]').count();
  if (!lastFocusable) failure = 'last episode is unreachable by keyboard';
  if (!negative && !failure) {
    const previous = page.locator('.title-unit').nth(248).locator('[data-key$=".play"]');
    await previous.focus();
    await page.keyboard.press('ArrowDown');
    const focused = await page.evaluate(() => document.activeElement?.getAttribute('data-key'));
    console.log(`keyboard: episode 249 down → ${focused}`);
    if (focused !== 'theatre.episode.tt-slice4-paging:1:250.play')
      failure = `last episode Play unreachable from previous row (${focused})`;
  }
  await page.close();
} finally {
  await browser.close();
  server.close();
}
if (failure) { console.log('FAIL: ' + failure); process.exitCode = 1; }
else console.log('PASS: paged to episode 250 and its Play action is keyboard reachable');
