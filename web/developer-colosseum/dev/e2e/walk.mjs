// node walk.mjs '<route json>' [label]
// Opens the page on the recorded fixtures (a plain browser = fixture mode), then:
//   · screenshots at 1920×1080 and 1280×720 into dev/e2e/out/<label>-<w>.png
//   · reports console errors and events the port dropped
//   · reports section states on the page (so loading/empty/error can be seen)
//   · keyboard walk: unreachable focusables, focus landing hidden under the TopBar
//   · Escape must leave the page (route changes)
// Exit 1 if anything fails. Examples:
//   node walk.mjs '{"name":"world","world":"Theatre","tab":"discover"}' theatre-discover
//   node walk.mjs '{"name":"page","page":"downloads"}' downloads
import fs from 'node:fs';
import path from 'node:path';
import { serve, launch, WEB, OUT, routeHash, watchConsole, keyboardWalk } from './lib.mjs';

const route = JSON.parse(process.argv[2] || '{"name":"home"}');
const label = process.argv[3] || (route.name + (route.world ? '-' + route.world : '') + (route.tab ? '-' + route.tab : '') + (route.page ? '-' + route.page : ''));
fs.mkdirSync(OUT, { recursive: true });

const { server, base } = await serve();
const browser = await launch();
const failures = [];
try {
  for (const [w, h] of [[1920, 1080], [1280, 720]]) {
    const page = await browser.newPage({ viewport: { width: w, height: h } });
    const problems = watchConsole(page);
    await page.goto(base + WEB + 'index.html' + routeHash(route));
    await page.waitForTimeout(2500);                       // recordings replay with their timing (capped)
    const shot = path.join(OUT, `${label}-${w}.png`);
    await page.screenshot({ path: shot, fullPage: false });
    console.log(`screenshot ${shot}`);

    if (w === 1920) {
      const sections = await page.$$eval('#col [data-section]', ss => ss.map(s => `${s.dataset.section}:${s.dataset.state}`));
      console.log(`sections (${sections.length}): ${sections.join(', ') || 'none'}`);
      if (!sections.length) failures.push('no sections rendered');

      const walk = await keyboardWalk(page);
      console.log(`keyboard: reached ${walk.reached}/${walk.total} focusables in ${walk.presses} presses`);
      if (walk.unreachable.length) failures.push(`unreachable by keyboard (${walk.unreachable.length}): ${walk.unreachable.slice(0, 15).join(' | ')}`);
      if (walk.hiddenAfterMove.length) failures.push(`focus landed hidden under the TopBar/off the board: ${walk.hiddenAfterMove.slice(0, 10).join(' | ')}`);

      if (route.name !== 'home') {
        const before = await page.evaluate(() => location.hash);
        await page.keyboard.press('Escape');
        await page.waitForTimeout(300);
        if (before === await page.evaluate(() => location.hash)) failures.push('Escape did not leave the page');
      }
    }
    if (problems.length) failures.push(...problems.map(p => `[${w}] ${p}`));
    await page.close();
  }
} finally {
  await browser.close();
  server.close();
}
if (failures.length) { console.log('FAIL'); failures.forEach(f => console.log('  - ' + f)); process.exitCode = 1; }
else console.log('PASS');
