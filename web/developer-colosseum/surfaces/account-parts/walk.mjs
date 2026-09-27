import fs from 'node:fs';
import path from 'node:path';
import { serve, launch, WEB, OUT, watchConsole, keyboardWalk } from '../../dev/e2e/lib.mjs';

const sizes = [[1920, 1080, true], [1280, 720, true], [900, 720, false]];
const failures = [];
fs.mkdirSync(OUT, { recursive: true });

function near(actual, expected, tolerance = 1.25) {
  return Math.abs(actual - expected) <= tolerance;
}

const { server, base } = await serve();
const browser = await launch();
try {
  for (const [width, height, capture] of sizes) {
    const page = await browser.newPage({ viewport: { width, height } });
    const problems = watchConsole(page);
    await page.goto(base + WEB + 'surfaces/account-parts/dev.html');
    await page.waitForFunction(() => window.__accountPartsReady === true, null, { timeout: 30000 });
    await page.evaluate(() => document.fonts.ready);
    await page.waitForTimeout(150);

    if (capture) {
      const shot = path.join(OUT, 'account-parts-web-' + width + '.png');
      await page.screenshot({ path: shot, fullPage: false });
      console.log('screenshot ' + shot);
    }
    const audit = await page.evaluate(() => {
      const s = window.__accountPartsShowcase;
      const rect = el => {
        const r = el.getBoundingClientRect();
        return { x: r.x, y: r.y, width: r.width, height: r.height };
      };
      const headline = document.querySelector('.ap-page-headline');
      const header = document.querySelector('.ap-panel-header');
      const emptyHeader = CW.accountParts.panelHeader({ kicker: 'K', title: 'T', copy: '' });
      const disabledButton = CW.accountParts.button({ text: 'Disabled', enabled: false });
      const disabledChoice = CW.accountParts.choice({ title: 'Disabled', enabled: false });
      return {
        frame: rect(s.frame), intro: rect(s.frame.intro), panel: rect(s.frame.panel),
        field: rect(s.field), input: rect(s.field.input), reveal: rect(s.field.revealButton),
        choice: rect(s.choice), primary: rect(s.primary), secondary: rect(s.secondary), link: rect(s.link),
        headlineSize: parseFloat(getComputedStyle(headline).fontSize),
        copyCount: header.querySelectorAll('.ap-panel-copy').length,
        emptyHeaderCopyCount: emptyHeader.querySelectorAll('.ap-panel-copy,.ap-panel-copy-gap').length,
        disabledButtonFocus: disabledButton.hasAttribute('data-focus'),
        disabledChoiceFocus: disabledChoice.hasAttribute('data-focus'),
        inputType: s.field.input.type,
        inputMaxLength: s.field.input.maxLength,
        inputValueAttr: s.field.input.getAttribute('value'),
        compact: s.frame.dataset.compact,
        scrollHeight: s.frame.scrollRegion.scrollHeight,
        clientHeight: s.frame.scrollRegion.clientHeight
      };
    });

    const compact = width < 1040;
    const margin = compact ? 34 : 54;
    const expectedPanelWidth = Math.min(560, width - margin * 2);
    const expectedPanelX = compact ? (width - expectedPanelWidth) / 2 : width - margin - expectedPanelWidth;
    const expectedIntroWidth = compact ? width - margin * 2 : Math.max(320, width - margin * 3 - expectedPanelWidth - 52);
    const expectedHeadline = compact ? 50 : Math.min(76, Math.max(56, width * 0.052));
    if (audit.compact !== String(compact)) failures.push(width + ': compact state ' + audit.compact + ' != ' + compact);
    if (!near(audit.panel.x, expectedPanelX)) failures.push(width + ': panel x ' + audit.panel.x + ' != ' + expectedPanelX);
    if (!near(audit.panel.width, expectedPanelWidth)) failures.push(width + ': panel width ' + audit.panel.width);
    if (!near(audit.intro.x, margin)) failures.push(width + ': intro x ' + audit.intro.x);
    if (!near(audit.intro.width, expectedIntroWidth)) failures.push(width + ': intro width ' + audit.intro.width);
    if (!near(audit.headlineSize, expectedHeadline)) failures.push(width + ': headline size ' + audit.headlineSize);
    const expectedIntroY = compact ? 42 : Math.max(30, (height - audit.intro.height) / 2);
    const expectedPanelY = compact ? expectedIntroY + audit.intro.height + 44 : Math.max(26, (height - audit.panel.height) / 2);
    if (!near(audit.panel.y, expectedPanelY)) failures.push(width + ': panel y ' + audit.panel.y + ' != ' + expectedPanelY);
    if (!near(audit.intro.y, expectedIntroY)) failures.push(width + ': intro y ' + audit.intro.y + ' != ' + expectedIntroY);
    if (!near(audit.input.height, 46)) failures.push(width + ': field input height ' + audit.input.height);
    if (!near(audit.reveal.width, 32) || !near(audit.reveal.height, 32)) failures.push(width + ': reveal is not 32x32');
    if (!near(audit.choice.height, 76)) failures.push(width + ': choice height ' + audit.choice.height);
    for (const [name, rect] of [['primary', audit.primary], ['secondary', audit.secondary], ['link', audit.link]]) {
      if (!near(rect.height, 46)) failures.push(width + ': ' + name + ' height ' + rect.height);
      if (rect.width < 159) failures.push(width + ': ' + name + ' below 160px minimum');
    }
    if (audit.copyCount !== 1) failures.push(width + ': panel header copy not rendered exactly once');
    if (audit.emptyHeaderCopyCount !== 0) failures.push(width + ': empty panel copy left a copy node or spacer');
    if (audit.disabledButtonFocus || audit.disabledChoiceFocus) failures.push(width + ': disabled control entered focus graph');
    if (audit.inputType !== 'password') failures.push(width + ': password input starts revealed');
    if (audit.inputMaxLength !== 512) failures.push(width + ': maximumLength was not preserved');
    if (audit.inputValueAttr !== null) failures.push(width + ': input value was serialized into an attribute');
    if (audit.scrollHeight < audit.clientHeight) failures.push(width + ': scroll content is shorter than viewport');

    if (width === 1920) {
      const walk = await keyboardWalk(page);
      // AccountOnboardingHost.qml:29-32 deliberately keeps arrow keys inside editable fields;
      // Tab is the secondary form route. Use real browser Tab below to cover the reveal button.
      const tabKeys = new Set();
      await page.focus('#accountPartsPassword');
      for (let i = 0; i < 8; ++i) {
        await page.keyboard.press('Tab');
        const key = await page.evaluate(() => {
          const el = document.activeElement;
          return el && (el.getAttribute('data-key') || el.id || '');
        });
        if (key) tabKeys.add(key);
      }
      const unreachable = walk.unreachable.filter(key => !tabKeys.has(key));
      console.log('keyboard: arrow ' + walk.reached + '/' + walk.total + ', arrow+Tab ' + (walk.total - unreachable.length) + '/' + walk.total);
      if (unreachable.length) failures.push('keyboard unreachable: ' + unreachable.join(' | '));
      if (walk.hiddenAfterMove.length) failures.push('keyboard hidden landing: ' + walk.hiddenAfterMove.join(' | '));
      await page.focus('#accountPartsPassword');
      await page.keyboard.press('Enter');
      await page.click('#accountPartsPasswordReveal');
      const revealState = await page.evaluate(() => ({
        type: window.__accountPartsShowcase.field.input.type,
        label: window.__accountPartsShowcase.field.revealButton.getAttribute('aria-label'),
        accepted: window.__accountPartsShowcase.events.accepted
      }));
      if (revealState.accepted !== 1) failures.push('Enter did not emit accepted exactly once');
      if (revealState.type !== 'text' || revealState.label !== 'Hide password') failures.push('reveal did not toggle');
      await page.evaluate(() => window.__accountPartsShowcase.field.clear());
      const cleared = await page.evaluate(() => ({
        type: window.__accountPartsShowcase.field.input.type,
        value: window.__accountPartsShowcase.field.input.value,
        label: window.__accountPartsShowcase.field.revealButton.getAttribute('aria-label')
      }));
      if (cleared.value !== '' || cleared.type !== 'password' || cleared.label !== 'Show password') {
        failures.push('clear() did not empty and re-mask the field');
      }
      await page.click('[data-key="account-parts.choice"]');
      await page.click('[data-key="account-parts.primary"]');
      await page.click('[data-key="account-parts.secondary"]');
      await page.click('[data-key="account-parts.link"]');
      const actions = await page.evaluate(() => window.__accountPartsShowcase.events);
      for (const name of ['choice', 'primary', 'secondary', 'link']) {
        if (actions[name] !== 1) failures.push(name + ' activation did not fire exactly once');
      }
    }

    failures.push(...problems.map(problem => '[' + width + '] ' + problem));
    await page.close();
  }
} finally {
  await browser.close();
  server.close();
}

if (failures.length) {
  console.log('FAIL');
  failures.forEach(failure => console.log('  - ' + failure));
  process.exitCode = 1;
} else {
  console.log('PASS');
}
