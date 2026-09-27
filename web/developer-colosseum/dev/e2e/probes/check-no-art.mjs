import { serve, launch, WEB, routeHash } from '../lib.mjs';

const route = { name: 'detail', kind: 'theatre',
  params: { id: 'tt0133093', type: 'movie', title: 'No art fixture' } };
const { server, base } = await serve();
const browser = await launch();
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  await page.goto(base + WEB + 'index.html' + routeHash(route));
  await page.locator('#col [data-section="hero"][data-state="ready"]').waitFor();
  const observed = await page.evaluate(() => ({
    title: document.querySelector('.title-name')?.textContent,
    scores: document.querySelectorAll('.title-score-group,.title-score').length,
    fallbackArt: document.querySelector('.title-backdrop-art')?.getAttribute('src') || '',
    playFocusable: !!document.querySelector('[data-key="theatre.play"][data-focus]')
  }));
  console.log(JSON.stringify(observed));
  if (observed.title !== 'The Matrix' || observed.scores !== 0 ||
      !observed.fallbackArt.includes('cold-ripple.jpg') || !observed.playFocusable)
    process.exitCode = 1;
  else console.log('PASS');
  await page.close();
} finally {
  await browser.close();
  server.close();
}
