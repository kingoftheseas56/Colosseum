// Temporary visual comparison probe for Harbor's public web app.
import fs from 'node:fs';
import path from 'node:path';
import { chromium } from 'playwright-core';

const out = path.join(import.meta.dirname, 'out');
fs.mkdirSync(out, { recursive: true });
const browser = await chromium.launch({ channel: 'msedge', headless: true });
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 }, deviceScaleFactor: 1.5 });
  await page.goto('https://app.harbor.site/', { waitUntil: 'domcontentloaded', timeout: 45000 });
  await page.waitForTimeout(2500);
  console.log('url', page.url());
  await page.screenshot({ path: path.join(out, 'harbor-inspect-home.png') });
  await page.getByText('Collapse', { exact: true }).first().click();
  const search = page.getByText(/Search movies, shows/i).first();
  await search.click();
  await page.locator('input').last().fill('One Piece');
  await page.waitForTimeout(3500);
  await page.screenshot({ path: path.join(out, 'harbor-inspect-search.png') });
  await page.getByRole('dialog', { name: 'Search' }).getByText('One Piece', { exact: true }).first().click();
  await page.waitForTimeout(4500);
  await page.screenshot({ path: path.join(out, 'harbor-inspect-detail.png') });
  await page.locator('button:has(svg.lucide-x)').last().click({ timeout: 1500 }).catch(() => {});
  await page.waitForTimeout(7000);
  await page.screenshot({ path: path.join(out, 'harbor-inspect-detail-closed.png') });
  const episodeHeading = page.getByText('Episodes', { exact: true }).first();
  console.log('episodes', await episodeHeading.count());
  if (await episodeHeading.count()) {
    await episodeHeading.scrollIntoViewIfNeeded();
    await page.waitForTimeout(800);
    console.log('heading-parent', await episodeHeading.locator('..').evaluate(el => el.outerHTML.slice(0, 900)));
    await page.screenshot({ path: path.join(out, 'harbor-inspect-episodes.png') });
    const season = page.getByRole('button', { name: /Season \d+/ }).last();
    console.log('season-button', await season.count());
    if (await season.count()) {
      await season.click();
      await page.screenshot({ path: path.join(out, 'harbor-inspect-picker.png') });
      console.log('picker-text', (await page.locator('body').innerText()).slice(-700));
    }
  }
} finally {
  await browser.close();
}
