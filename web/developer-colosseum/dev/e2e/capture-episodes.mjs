// Capture the list rather than the first-screen hero on a recorded detail route.
import fs from 'node:fs';
import path from 'node:path';
import { serve, launch, WEB, OUT, routeHash } from './lib.mjs';

const route = JSON.parse(process.argv[2]);
const label = process.argv[3] || 'episodes';
const menu = process.argv.includes('--menu');
const aired = process.argv.includes('--aired');
const dpr15 = process.argv.includes('--dpr15');
fs.mkdirSync(OUT, { recursive: true });
const { server, base } = await serve();
const browser = await launch();
try {
  for (const [width, height] of dpr15 ? [[1280, 720]] : [[1920, 1080], [1280, 720]]) {
    const page = await browser.newPage({ viewport: { width, height }, deviceScaleFactor: dpr15 ? 1.5 : 1 });
    await page.goto(base + WEB + 'index.html' + routeHash(route));
    await page.locator('[data-section="episodes"][data-state="ready"] .title-unit').first().waitFor();
    await page.waitForTimeout(1300);
    await page.evaluate(() => {
      const board = document.getElementById('board');
      const section = document.querySelector('[data-section="seasons"]');
      board.scrollTop += section.getBoundingClientRect().top - 90;
    });
    await page.waitForTimeout(200);
    if (menu) {
      await page.locator('[data-key="theatre.season.trigger"]').click();
      await page.locator('.title-season-menu').waitFor();
      if (aired && await page.locator('[data-key="theatre.season.mode.absolute"].on').count()) {
        await page.locator('[data-key="theatre.season.mode.seasons"]').click();
        await page.waitForFunction(() => document.querySelector('[data-key="theatre.season.trigger"]')
          ?.textContent.includes('Season'));
        await page.locator('[data-key="theatre.season.trigger"]').click();
        await page.locator('.title-season-menu').waitFor();
      }
    }
    const file = path.join(OUT, `${label}-${dpr15 ? '1920-dpr15' : width}.png`);
    await page.screenshot({ path: file });
    console.log(file);
    await page.close();
  }
} finally {
  await browser.close();
  server.close();
}
