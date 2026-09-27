(function (CW) {
  'use strict';

  const h = CW.h;
  const parts = CW.accountParts;
  const events = { accepted: 0, choice: 0, primary: 0, secondary: 0, link: 0 };

  const field = parts.field({
    label: 'Password',
    hint: 'Use the reveal control to check what you typed.',
    placeholderText: 'Your password',
    password: true,
    maximumLength: 512,
    inputMethodHints: 'noPredictiveText',
    controlObjectName: 'accountPartsPassword',
    key: 'account-parts.password',
    onAccepted: () => { events.accepted += 1; }
  });
  field.classList.add('ap-show-field');

  const choice = parts.choice({
    title: 'Use this device',
    detail: 'Approve this device and continue.',
    key: 'account-parts.choice',
    onChosen: () => { events.choice += 1; }
  });
  const primary = parts.button({
    text: 'Continue', variant: 'primary', key: 'account-parts.primary',
    onClicked: () => { events.primary += 1; }
  });
  const secondary = parts.button({
    text: 'Not now', key: 'account-parts.secondary',
    onClicked: () => { events.secondary += 1; }
  });
  const link = parts.button({
    text: 'Need help?', variant: 'link', key: 'account-parts.link',
    onClicked: () => { events.link += 1; }
  });

  const frame = parts.pageFrame({
    eyebrow: 'COLOSSEUM · ACCOUNT',
    headline: 'Shared account foundation.',
    detail: 'One set of account controls keeps sign-in, recovery, security, and profile flows visually consistent.',
    panelWidth: 560
  });

  frame.panelContent.append(
    parts.panelHeader({
      kicker: 'ACCOUNT CENTER',
      title: 'Shared account parts',
      copy: 'One source of truth for fields, buttons, and choices.'
    }),
    h('div.ap-show-space-24'),
    field,
    h('div.ap-show-space-20'),
    choice,
    h('div.ap-show-space-20'),
    h('div.ap-show-actions', {}, primary, secondary),
    h('div.ap-show-space-12'),
    link
  );

  document.getElementById('parts-root').appendChild(frame);
  window.__accountPartsShowcase = { frame, field, choice, primary, secondary, link, events };
  document.fonts.ready.then(() => {
    frame.syncGeometry();
    window.__accountPartsReady = true;
  });
})(window.CW);
