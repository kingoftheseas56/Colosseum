// Shared Account primitives. Design authority: qml/account/Account{PageFrame,PanelHeader,Field,Button,Choice}.qml.
(function (CW) {
  'use strict';

  const h = CW.h;
  const scriptBase = document.currentScript ? new URL('.', document.currentScript.src) : null;
  const fallbackWallpaper = location.protocol === 'qrc:'
    ? 'qrc:///developer-webui/captured-motion.jpg'
    : new URL('../../../../assets/wallpaper/captured-motion.jpg', scriptBase || location.href).href;

  function append(parent, content) {
    if (content == null || content === false) return;
    for (const item of (Array.isArray(content) ? content : [content])) {
      if (item == null || item === false) continue;
      parent.appendChild(item instanceof Node ? item : document.createTextNode(String(item)));
    }
  }

  function metric(root, name) {
    return Number.parseFloat(getComputedStyle(root).getPropertyValue(name)) || 0;
  }

  function px(node, property, value) {
    const next = value + 'px';
    if (node.style[property] !== next) node.style[property] = next;
  }
  function pageFrame(options) {
    const opts = options || {};
    const root = h('section.ap-page-frame', { 'data-account-part': 'page-frame' });
    const image = h('img.ap-page-wallpaper-image', {
      alt: '', 'aria-hidden': 'true', src: opts.backdropUrl || fallbackWallpaper
    });
    const wallpaper = h('div.ap-page-wallpaper', { 'aria-hidden': 'true' },
      image, h('div.ap-page-veil'));
    const scroller = h('div.ap-page-scroll', {
      tabindex: '-1', 'data-account-scroll-region': ''
    });
    const intro = h('div.ap-page-intro', {},
      h('div.ap-page-eyebrow', {}, opts.eyebrow || ''),
      h('div.ap-page-eyebrow-gap'),
      h('div.ap-page-headline', {}, opts.headline || ''),
      h('div.ap-page-headline-gap'),
      h('div.ap-page-rule', { 'aria-hidden': 'true' }),
      h('div.ap-page-rule-gap'),
      h('div.ap-page-detail', {}, opts.detail || ''));
    const panel = h('div.ap-page-panel');
    const panelContent = h('div.ap-page-panel-content');
    panel.appendChild(panelContent);
    scroller.append(intro, panel);
    root.append(wallpaper, scroller);
    append(panelContent, opts.content);

    let panelWidth = Number.isFinite(Number(opts.panelWidth))
      ? Number(opts.panelWidth) : null;
    let panelMinimumHeight = Number.isFinite(Number(opts.panelMinimumHeight))
      ? Number(opts.panelMinimumHeight) : null;
    let raf = 0;
    function syncGeometry() {
      raf = 0;
      const width = root.clientWidth;
      const height = root.clientHeight;
      if (!width || !height) return;

      const compact = width < metric(root, '--ap-compact-breakpoint');
      const headlineSize = compact
        ? metric(root, '--ap-headline-compact')
        : Math.min(metric(root, '--ap-headline-wide-max'),
          Math.max(metric(root, '--ap-headline-wide-min'),
            width * metric(root, '--ap-headline-scale')));
      root.style.setProperty('--ap-headline-size', headlineSize + 'px');
      const margin = compact
        ? metric(root, '--ap-compact-margin')
        : metric(root, '--ap-wide-margin');
      const desiredPanel = panelWidth == null
        ? metric(root, '--ap-panel-default-width') : Math.max(0, panelWidth);
      const minimumPanel = panelMinimumHeight == null
        ? metric(root, '--ap-panel-default-min-height') : Math.max(0, panelMinimumHeight);
      const actualPanel = Math.max(0, Math.min(desiredPanel, width - margin * 2));
      const introWidth = compact
        ? Math.max(0, width - margin * 2)
        : Math.max(metric(root, '--ap-intro-min-width'),
          width - margin * 3 - actualPanel - metric(root, '--ap-wide-extra-gap'));

      px(intro, 'left', margin);
      px(intro, 'width', introWidth);
      px(panel, 'width', actualPanel);
      px(panel, 'minHeight', minimumPanel);
      px(panel, 'left', compact ? (width - actualPanel) / 2 : width - margin - actualPanel);

      const introY = compact
        ? metric(root, '--ap-compact-intro-y')
        : Math.max(metric(root, '--ap-wide-intro-min-y'), (height - intro.offsetHeight) / 2);
      px(intro, 'top', introY);

      const panelY = compact
        ? introY + intro.offsetHeight + metric(root, '--ap-compact-panel-gap')
        : Math.max(metric(root, '--ap-wide-panel-min-y'), (height - panel.offsetHeight) / 2);
      px(panel, 'top', panelY);

      const bottom = compact
        ? metric(root, '--ap-compact-bottom-space')
        : metric(root, '--ap-wide-bottom-space');
      scroller.style.setProperty('--ap-content-height',
        Math.max(height, panelY + panel.offsetHeight + bottom) + 'px');
      root.dataset.compact = compact ? 'true' : 'false';
    }
    function scheduleGeometry() {
      if (raf) return;
      raf = requestAnimationFrame(syncGeometry);
    }

    const observer = new ResizeObserver(scheduleGeometry);
    observer.observe(root);
    observer.observe(intro);
    observer.observe(panel);
    scheduleGeometry();

    root.panelContent = panelContent;
    root.scrollRegion = scroller;
    root.panel = panel;
    root.intro = intro;
    root.syncGeometry = syncGeometry;
    root.setPanelWidth = value => {
      panelWidth = Number.isFinite(Number(value)) ? Number(value) : null;
      scheduleGeometry();
    };
    root.setPanelMinimumHeight = value => {
      panelMinimumHeight = Number.isFinite(Number(value)) ? Number(value) : null;
      scheduleGeometry();
    };
    root.dispose = () => {
      observer.disconnect();
      if (raf) cancelAnimationFrame(raf);
      raf = 0;
    };
    return root;
  }

  function panelHeader(options) {
    const opts = options || {};
    const root = h('div.ap-panel-header', { 'data-account-part': 'panel-header' },
      h('div.ap-panel-kicker', {}, opts.kicker || ''),
      h('div.ap-panel-kicker-gap'),
      h('div.ap-panel-title', {}, opts.title || ''));
    if (opts.copy) {
      root.append(h('div.ap-panel-copy-gap'), h('div.ap-panel-copy', {}, opts.copy));
    }
    return root;
  }
  function field(options) {
    const opts = options || {};
    const key = opts.key || opts.controlObjectName || '';
    const root = h('label.ap-field', { 'data-account-part': 'field' });
    const label = h('span.ap-field-label', {}, opts.label || '');
    root.appendChild(label);

    if (opts.hint) root.appendChild(h('span.ap-field-hint', {}, opts.hint));

    const control = h('span.ap-field-control');
    const input = h('input.ap-field-input', {
      id: opts.controlObjectName || null,
      type: opts.password && !opts.reveal ? 'password' : 'text',
      placeholder: opts.placeholderText || '',
      maxlength: Number.isFinite(Number(opts.maximumLength)) ? Number(opts.maximumLength) : null,
      inputmode: opts.inputMode || null,
      autocomplete: opts.autocomplete || (opts.inputMethodHints ? 'off' : null),
      spellcheck: opts.inputMethodHints ? 'false' : null,
      'data-focus': true,
      'data-key': key || null,
      'aria-label': opts.label || opts.placeholderText || ''
    });
    control.appendChild(input);

    let revealed = !!opts.reveal;
    let revealButton = null;
    function syncReveal() {
      if (!opts.password) return;
      input.type = revealed ? 'text' : 'password';
      revealButton.textContent = revealed ? '○' : '◉';
      revealButton.setAttribute('aria-label', revealed ? 'Hide password' : 'Show password');
    }
    if (opts.password) {
      revealButton = h('button.ap-field-reveal', {
        id: opts.controlObjectName ? opts.controlObjectName + 'Reveal' : null,
        type: 'button', 'data-focus': true,
        'data-key': key ? key + '.reveal' : null,
        onclick: () => { revealed = !revealed; syncReveal(); }
      });
      control.appendChild(revealButton);
      syncReveal();
    }
    root.appendChild(control);

    if (opts.value != null) input.value = String(opts.value);
    input.addEventListener('keydown', event => {
      if (event.key === 'Enter' && typeof opts.onAccepted === 'function') opts.onAccepted();
    });
    if (typeof opts.onInput === 'function') {
      input.addEventListener('input', () => opts.onInput(input.value));
    }

    root.input = input;
    root.revealButton = revealButton;
    root.clear = () => {
      input.value = '';
      revealed = false;
      syncReveal();
    };
    root.forceInputFocus = () => input.focus({ preventScroll: false });
    Object.defineProperty(root, 'text', {
      get: () => input.value,
      set: value => { input.value = value == null ? '' : String(value); }
    });
    Object.defineProperty(root, 'reveal', {
      get: () => revealed,
      set: value => { revealed = !!value; syncReveal(); }
    });
    return root;
  }
  function button(options) {
    const opts = options || {};
    const control = h('button.ap-button', {
      type: 'button',
      'data-account-part': 'button',
      'data-variant': opts.variant || 'secondary',
      'data-focus': opts.enabled === false ? null : true,
      'data-key': opts.key || opts.objectName || null,
      id: opts.objectName || null,
      disabled: opts.enabled === false ? true : null,
      onclick: typeof opts.onClicked === 'function' ? opts.onClicked : null
    }, h('span.ap-button-label', {}, opts.text || ''));
    if (opts.primaryInk) control.style.setProperty('--ap-primary-ink', opts.primaryInk);
    return control;
  }

  function choice(options) {
    const opts = options || {};
    return h('button.ap-choice', {
      type: 'button',
      'data-account-part': 'choice',
      'data-focus': opts.enabled === false ? null : true,
      'data-key': opts.key || opts.objectName || null,
      id: opts.objectName || null,
      disabled: opts.enabled === false ? true : null,
      onclick: typeof opts.onChosen === 'function' ? opts.onChosen : null
    },
    h('span.ap-choice-content', {},
      h('span.ap-choice-title', {}, opts.title || ''),
      h('span.ap-choice-detail', {}, opts.detail || '')));
  }

  CW.accountParts = Object.freeze({ pageFrame, panelHeader, field, button, choice });
})(window.CW = window.CW || {});
