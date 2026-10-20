// Shared by the scrolling recreations. It keeps the wheel working over floating bars, and with ?auto=1 in the address the page tours itself:
// each tab in turn, gliding down its content and back. The website uses it for the frames in the
// Showcase scroll. The site stops the tour when a visitor clicks into a frame and restarts it when
// they leave, by posting {colosseumTour: "stop" | "start"} to the page.
(function () {
  // the page's own scrolling area: whichever element has the most to scroll
  var found = null;
  function scroller() {
    if (found && found.isConnected && found.scrollHeight - found.clientHeight > 40) return found;
    var best = null, room = 40;
    document.querySelectorAll("*").forEach(function (el) {
      var r = el.scrollHeight - el.clientHeight;
      if (r > room && /(auto|scroll)/.test(getComputedStyle(el).overflowY)) { best = el; room = r; }
    });
    return (found = best);
  }
  // The wheel is applied here by hand. Left to the browser, a turn inside a scaled frame was
  // sometimes spent on the website behind it, and turns over the floating bars scrolled nothing.
  var aim = null, aimTimer = 0;
  function ownScroller(node, el) {            // a list inside the page that scrolls by itself
    for (; node && node !== el && node.nodeType === 1; node = node.parentElement)
      if (node.scrollHeight - node.clientHeight > 4 && /(auto|scroll)/.test(getComputedStyle(node).overflowY)) return true;
    return false;
  }
  addEventListener("wheel", function (e) {
    var el = scroller();
    if (!el || e.ctrlKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)) return;
    if (el.contains(e.target) && ownScroller(e.target, el)) return;
    var dy = e.deltaMode === 1 ? e.deltaY * 32 : e.deltaY, max = el.scrollHeight - el.clientHeight;
    var from = aim === null ? el.scrollTop : aim;
    if ((dy < 0 && from <= 0) || (dy > 0 && from >= max - 1)) return;   // at an end: let the website scroll on
    e.preventDefault();
    aim = Math.max(0, Math.min(max, from + dy));
    if (Math.abs(dy) >= 50) el.scrollTo({ top: aim, behavior: "smooth" });   // a wheel notch
    else el.scrollTop = aim;                                                // a trackpad's fine steps
    clearTimeout(aimTimer); aimTimer = setTimeout(function () { aim = null; }, 400);
  }, { passive: false });

  if (!new URLSearchParams(location.search).has("auto")) return;
  if (matchMedia("(prefers-reduced-motion: reduce)").matches) return;

  var SPEED = 110;   // pixels a second while cruising
  var RAMP = 0.7;    // seconds to reach that speed, and to come to rest

  var visible = true, run = 0;   // run: which tour is current; a stale one ends at its next step
  if ("IntersectionObserver" in window)
    new IntersectionObserver(function (e) { visible = e[0].isIntersecting; }, { threshold: 0.3 })
      .observe(document.documentElement);
  function running() { return visible && !document.hidden; }

  // One step per painted frame. Where frames never arrive (a capture tool), a timer stands in.
  function step(fn) {
    var done = false;
    function go(now) { if (done) return; done = true; fn(now || performance.now()); }
    requestAnimationFrame(go);
    setTimeout(go, 120);
  }
  function wait(ms, mine) {
    return new Promise(function (done, stop) {
      var left = ms, last = performance.now();
      (function tick(now) {
        if (mine !== run) { stop(); return; }
        if (running()) left -= Math.min(now - last, 100);
        last = now;
        if (left <= 0) done(); else step(tick);
      })(last);
    });
  }
  // Steady travel with a soft start and stop: distance covered after t seconds of a trip of d pixels.
  function travelled(t, d, speed) {
    var ramp = Math.min(RAMP, d / speed);          // short trips never reach full speed
    var total = d / speed + ramp;
    if (t >= total) return d;
    if (t < ramp) return speed * t * t / (2 * ramp);
    if (t > total - ramp) { var r = total - t; return d - speed * r * r / (2 * ramp); }
    return speed * (t - ramp / 2);
  }
  function glide(el, to, speed, mine) {
    return new Promise(function (done, stop) {
      var from = el.scrollTop, d = Math.abs(to - from), dir = to > from ? 1 : -1;
      if (d < 1) { done(); return; }
      var t = 0, last = performance.now();
      (function tick(now) {
        if (mine !== run) { stop(); return; }
        if (running()) t += Math.min(now - last, 50) / 1000;   // a stall never becomes a jump
        last = now;
        var x = travelled(t, d, speed);
        el.scrollTop = from + dir * x;
        if (x < d) step(tick); else done();
      })(last);
    });
  }
  function tabNames() {
    var seen = [];
    document.querySelectorAll("[data-tab]").forEach(function (b) {
      if (seen.indexOf(b.dataset.tab) < 0) seen.push(b.dataset.tab);
    });
    return seen;
  }

  // resume: carry on from wherever the visitor left the page, on the tab they left open
  async function tour(mine, resume) {
    try {
      await wait(1200, mine);
      for (;;) {
        var tabs = tabNames();
        if (!tabs.length) tabs = [null];
        for (var i = 0; i < tabs.length; i++) {
          if (tabs[i] && tabs.length > 1 && !resume) {
            var b = document.querySelector('[data-tab="' + tabs[i] + '"]');
            if (b) b.click();
            await wait(900, mine);
          }
          var el = scroller();
          if (!el) { await wait(2500, mine); continue; }
          el.style.scrollBehavior = "auto";        // the page's own smooth scrolling would fight each step
          await wait(1400, mine);
          await glide(el, el.scrollHeight - el.clientHeight, SPEED, mine);
          await wait(1200, mine);
          await glide(el, 0, SPEED * 9, mine);
          await wait(400, mine);
          if (resume) { resume = false; break; }   // then the usual lap, from the first tab
        }
      }
    } catch (stopped) {}
  }
  function start(resume) { run++; tour(run, resume); }
  function stop() {
    run++;
    var el = scroller(); if (el) el.style.scrollBehavior = "";
  }
  addEventListener("message", function (e) {
    var m = e.data && e.data.colosseumTour;
    if (m === "stop") stop(); else if (m === "start") start(true);
  });
  if (document.readyState === "complete") start(false); else addEventListener("load", function () { start(false); });
})();
