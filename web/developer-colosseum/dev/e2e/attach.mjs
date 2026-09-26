// node attach.mjs [port] ['<route json>'] [label]
// Drives the web layer INSIDE the running Colosseum app over the Chrome DevTools Protocol.
// Start Colosseum with both variables set, e.g. in PowerShell:
//   $env:COLOSSEUM_WEBUI='1'; $env:QTWEBENGINE_REMOTE_DEBUGGING='9222'; .\native\build-msvc\colosseum.exe
// Then: node attach.mjs 9222 '{"name":"world","world":"Theatre","tab":"discover"}' live-theatre
// It navigates the router (never reloads the app), screenshots, and runs the same keyboard walk on real data.
import fs from 'node:fs';
import path from 'node:path';
import { chromium } from 'playwright-core';
import { OUT, watchConsole, keyboardWalk } from './lib.mjs';

const port = process.argv[2] || '9222';
const route = process.argv[3] ? JSON.parse(process.argv[3]) : null;
const label = process.argv[4] || 'live';
fs.mkdirSync(OUT, { recursive: true });

const browser = await chromium.connectOverCDP(`http://127.0.0.1:${port}`);
const failures = [];
try {
  const pages = browser.contexts().flatMap(c => c.pages());
  const page = pages.find(p => /developer-webui|developer-colosseum/.test(p.url()));
  if (!page) throw new Error(`No Colosseum web layer found on port ${port}. Pages: ${pages.map(p => p.url()).join(', ') || 'none'}`);
  const problems = watchConsole(page);
  if (route) {
    await page.evaluate(r => window.CW.router.go(r), route);
    await page.waitForTimeout(2500);
  }
  const shot = path.join(OUT, `${label}.png`);
  await page.screenshot({ path: shot });
  console.log(`screenshot ${shot}`);
  const sections = await page.$$eval('#col [data-section]', ss => ss.map(s => `${s.dataset.section}:${s.dataset.state}`));
  console.log(`sections (${sections.length}): ${sections.join(', ') || 'none'}`);
  const walk = await keyboardWalk(page);
  console.log(`keyboard: reached ${walk.reached}/${walk.total} focusables in ${walk.presses} presses`);
  if (walk.unreachable.length) failures.push(`unreachable by keyboard: ${walk.unreachable.slice(0, 15).join(' | ')}`);
  if (walk.hiddenAfterMove.length) failures.push(`focus hidden under the TopBar: ${walk.hiddenAfterMove.slice(0, 10).join(' | ')}`);
  failures.push(...problems);
} finally {
  await browser.close();                                    // disconnects only; the app keeps running
}
if (failures.length) { console.log('FAIL'); failures.forEach(f => console.log('  - ' + f)); process.exitCode = 1; }
else console.log('PASS');
