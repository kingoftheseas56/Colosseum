// node selftest.mjs — runs dev/selftest.html headless at 1600×950; exit 1 on any FAIL.
import { serve, launch, WEB } from './lib.mjs';

const { server, base } = await serve();
const browser = await launch();
try {
  const page = await browser.newPage({ viewport: { width: 1600, height: 950 } });
  await page.goto(base + WEB + 'dev/selftest.html');
  await page.waitForFunction(() => document.title === 'PASS' || document.title === 'FAIL', null, { timeout: 30000 });
  const lines = await page.$$eval('#report div', ds => ds.map(d => d.textContent));
  const fails = lines.filter(l => l.startsWith('FAIL'));
  console.log(`${await page.title()} — ${lines.length - fails.length}/${lines.length} checks`);
  fails.forEach(f => console.log('  ' + f));
  process.exitCode = fails.length ? 1 : 0;
} finally {
  await browser.close();
  server.close();
}
