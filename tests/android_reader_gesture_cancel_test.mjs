import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import vm from 'node:vm'

// Execute the active vendor's actual gesture methods and listener registration. The
// surrounding DOM/clock/scroll container is deterministic; this is not WebView layout,
// native momentum, selection-handle or Android lifecycle qualification.
const vendor = new URL('../resources/reader2/vendor/foliate-anx/src/', import.meta.url)
const paginatorSource = await readFile(new URL('paginator.js', vendor), 'utf8')
const viewSource = await readFile(new URL('view.js', vendor), 'utf8')

function block(source, marker, optional = false) {
  const start = source.indexOf(marker)
  if (start < 0 && optional) return ''
  assert.ok(start >= 0, `Missing production block: ${marker}`)
  const opening = source.indexOf('{', start)
  let depth = 1
  for (let i = opening + 1; i < source.length; ++i) {
    if (source[i] === '{') ++depth
    if (source[i] === '}' && --depth === 0) return source.slice(start, i + 1)
  }
  throw new Error(`Unterminated production block: ${marker}`)
}

class Target {
  listeners = new Map()
  events = []
  addEventListener(name, listener) {
    const list = this.listeners.get(name) || []
    list.push(listener)
    this.listeners.set(name, list)
  }
  dispatchEvent(event) {
    this.events.push(event)
    for (const listener of this.listeners.get(event.type) || []) listener(event)
    return !event.defaultPrevented
  }
}
class CustomEvent {
  constructor(type, options = {}) { this.type = type; Object.assign(this, options) }
}
function event(type, x = 240, y = 200, timeStamp = 0) {
  const point = { screenX: x, screenY: y }
  return {
    type, changedTouches: [point],
    touches: type === 'touchend' || type === 'touchcancel' ? [] : [point],
    timeStamp, defaultPrevented: false, propagationStopped: false,
    preventDefault() { this.defaultPrevented = true },
    stopPropagation() { this.propagationStopped = true },
  }
}

function paginator({ scrolled = false, axis = 'scrollLeft' } = {}) {
  const registrationStart = paginatorSource.indexOf('const opts = { passive: false }')
  const registrationEnd = paginatorSource.indexOf('this.#mediaQueryListener', registrationStart)
  assert.ok(registrationStart >= 0 && registrationEnd > registrationStart)
  const registration = paginatorSource.slice(registrationStart, registrationEnd)
  const frames = []
  const context = vm.createContext({
    Target, CustomEvent,
    window: { getSelection: () => ({ toString: () => '' }) },
    visualViewport: { scale: 1 },
    requestAnimationFrame: callback => { frames.push(callback); return frames.length },
  })
  const methods = ['#onTouchStart(e)', '#onTouchMove(e)', '#onTouchEnd(e)', '#afterScroll(reason)']
    .map(marker => block(paginatorSource, '  ' + marker + ' {'))
  methods.push(block(paginatorSource, '  #onTouchCancel(e) {', true))
  methods.push(block(paginatorSource, '  async #scrollTo(offset, reason, smooth) {'))
  const easing = paginatorSource.match(/^const easeOutSine = .+$/m)?.[0]
  assert.ok(easing)
  const Fixture = vm.runInContext(`${easing};\n(class extends Target {
    #touchState; #touchScrolled; #pendingRelocate = null;
    #container = { scrollLeft: 600, scrollTop: 400 };
    #anchor; #justAnchored = false; #index = 0;
    #view = { document: {} }; #ignoreNativeScroll = false; #vertical = false;
    scrolled = false; scrollProp = 'scrollLeft';
    size = 600; pages = 10; viewSize = 6000; page = 1;
    snapCalls = [];
    constructor() { super(); ${registration} }
    get start() { return Math.abs(this.#container[this.scrollProp]) }
    get container() { return this.#container }
    get state() { return this.#touchState }
    get touchScrolled() { return this.#touchScrolled }
    get pendingRelocate() { return this.#pendingRelocate }
    #getVisibleRange() { return { start: this.start, end: this.start + this.size } }
    afterScroll() { this.#afterScroll('scroll') }
    snap(vx, vy, state) { this.snapCalls.push({ vx, vy, state }) }
    ${methods.join('\n')}
  })`, context)
  const target = new Fixture()
  target.scrolled = scrolled
  target.scrollProp = axis
  return {
    target, viewport: context.visualViewport,
    async frames() {
      for (const frame of frames.splice(0)) frame(16)
      await Promise.resolve()
      await Promise.resolve()
    },
  }
}

function imageHandler() {
  let nextTimer = 1
  const timers = new Map()
  const context = vm.createContext({
    window: { ontouchstart: null },
    setTimeout(callback) { const id = nextTimer++; timers.set(id, callback); return id },
    clearTimeout(id) { timers.delete(id) },
  })
  const Fixture = vm.runInContext(`(class {
    events = [];
    #emit(name, detail) { this.events.push({ name, detail }) }
    attach(doc) { this.#handleImage(doc) }
    ${block(viewSource, '  #handleImage(doc) {')}
  })`, context)
  const host = new Fixture()
  const image = Object.assign(new Target(), { style: {}, closest: () => null })
  host.attach({ querySelectorAll: () => [image] })
  return {
    host, image,
    runTimers() { for (const callback of [...timers.values()]) callback(); timers.clear() },
  }
}

const tests = []
function test(name, run) { tests.push({ name, run }) }

for (const targetName of ['host', 'loaded document']) {
  test(`paginator cancellation on ${targetName} releases progress without a page turn`, async () => {
    const fixture = paginator()
    const renderer = fixture.target
    const target = targetName === 'host' ? renderer : new Target()
    if (target !== renderer) renderer.dispatchEvent(new CustomEvent('load', { detail: { doc: target } }))
    target.dispatchEvent(event('touchstart'))
    target.dispatchEvent(event('touchmove', 120, 200, 20))
    renderer.container.scrollLeft = 720 // the browser performed native scrolling
    renderer.afterScroll()
    assert.ok(renderer.pendingRelocate, 'progress waits while an ordinary swipe is active')
    target.dispatchEvent(event('touchcancel', 120, 200, 25))
    await fixture.frames()
    assert.equal(renderer.state, null, 'cancel must release the original touch state')
    assert.equal(renderer.touchScrolled, false)
    assert.equal(renderer.pendingRelocate, null)
    assert.equal(renderer.snapCalls.length, 0, 'cancel must not turn/fling to another page')
    assert.equal(renderer.container.scrollLeft, 600, 'paginated cancel restores the press position')
    const relocations = renderer.events.filter(e => e.type === 'relocate')
    assert.equal(relocations.length, 1)
    assert.equal(relocations[0].detail.reason, 'cancel')
    assert.equal(relocations[0].detail.range.start, 600, 'discard the partial-page pending range')
    renderer.afterScroll()
    assert.equal(renderer.events.filter(e => e.type === 'relocate').length, 2,
                 'later native settling must not be held behind canceled state')
    target.dispatchEvent(event('touchstart', 300, 200, 40))
    target.dispatchEvent(event('touchmove', 200, 200, 60))
    target.dispatchEvent(event('touchend', 200, 200, 80))
    await fixture.frames()
    assert.equal(renderer.snapCalls.length, 1, 'a fresh completed swipe still uses the existing snap')
    assert.equal(renderer.state, null)
  })
}
test('continuous scroll cancellation preserves native position and ordinary touchend', async () => {
  const fixture = paginator({ scrolled: true, axis: 'scrollTop' })
  const renderer = fixture.target
  renderer.dispatchEvent(event('touchstart'))
  renderer.dispatchEvent(event('touchmove', 240, 100, 20))
  renderer.container.scrollTop = 480
  renderer.dispatchEvent(event('touchcancel', 240, 100, 30))
  assert.equal(renderer.state, null)
  assert.equal(renderer.container.scrollTop, 480)
  assert.equal(renderer.snapCalls.length, 0)
  renderer.dispatchEvent(event('touchstart', 240, 100, 40))
  renderer.dispatchEvent(event('touchend', 240, 100, 60))
  await fixture.frames()
  assert.equal(renderer.state, null)
  assert.equal(renderer.snapCalls.length, 0)
})
test('ordinary paginated touchend still snaps once with the captured velocity', async () => {
  const fixture = paginator()
  const renderer = fixture.target
  renderer.dispatchEvent(event('touchstart'))
  renderer.dispatchEvent(event('touchmove', 120, 200, 20))
  renderer.dispatchEvent(event('touchend', 120, 200, 40))
  await fixture.frames()
  assert.equal(renderer.snapCalls.length, 1)
  assert.equal(renderer.snapCalls[0].vx, 6)
  assert.equal(renderer.state, null)
})
test('canceling a vertical drag preserves the existing horizontal direction lock', async () => {
  const fixture = paginator()
  const renderer = fixture.target
  renderer.dispatchEvent(event('touchstart'))
  renderer.dispatchEvent(event('touchmove', 240, 100, 20))
  assert.equal(renderer.state.lockedOffset, 600)
  renderer.afterScroll()
  renderer.dispatchEvent(event('touchcancel', 240, 100, 40))
  await fixture.frames()
  assert.equal(renderer.state, null)
  assert.equal(renderer.container.scrollLeft, 600)
  assert.equal(renderer.snapCalls.length, 0)
  assert.equal(renderer.pendingRelocate, null)
})
for (const { name, axis, initial, partial } of [
  { name: 'RTL', axis: 'scrollLeft', initial: -600, partial: -720 },
  { name: 'vertical writing', axis: 'scrollTop', initial: 600, partial: 720 },
]) {
  test(`paginated ${name} cancellation restores its captured axis offset`, async () => {
    const fixture = paginator({ axis })
    const renderer = fixture.target
    renderer.container[axis] = initial
    renderer.dispatchEvent(event('touchstart'))
    renderer.dispatchEvent(event('touchmove', axis === 'scrollLeft' ? 120 : 240, 100, 20))
    renderer.container[axis] = partial
    renderer.afterScroll()
    renderer.dispatchEvent(event('touchcancel', 120, 100, 40))
    await fixture.frames()
    assert.equal(renderer.container[axis], initial)
    assert.equal(renderer.snapCalls.length, 0)
    assert.equal(renderer.state, null)
  })
}
test('pinch cancellation keeps native zoom positioning without snapping', async () => {
  const fixture = paginator()
  const renderer = fixture.target
  renderer.dispatchEvent(event('touchstart'))
  fixture.viewport.scale = 2
  renderer.dispatchEvent(event('touchmove', 120, 200, 20))
  renderer.container.scrollLeft = 720
  renderer.afterScroll()
  renderer.dispatchEvent(event('touchcancel', 120, 200, 40))
  await fixture.frames()
  assert.equal(renderer.container.scrollLeft, 720)
  assert.equal(renderer.snapCalls.length, 0)
  assert.equal(renderer.state, null)
  assert.equal(renderer.pendingRelocate, null)
})
test('an added contact keeps the original page until the complete gesture ends or cancels', async () => {
  const fixture = paginator()
  const renderer = fixture.target
  renderer.dispatchEvent(event('touchstart'))
  renderer.dispatchEvent(event('touchmove', 120, 200, 20))
  renderer.container.scrollLeft = 720
  const second = event('touchstart', 140, 200, 30)
  second.touches.push({ screenX: 120, screenY: 200 })
  renderer.dispatchEvent(second)
  assert.equal(renderer.state.startScroll, 600, 'a second contact must not recapture a partial page')
  const partialEnd = event('touchend', 140, 200, 40)
  partialEnd.touches = [{ screenX: 120, screenY: 200 }]
  renderer.dispatchEvent(partialEnd)
  await fixture.frames()
  assert.equal(renderer.snapCalls.length, 0, 'a remaining contact still owns the gesture')
  assert.ok(renderer.state)
  renderer.afterScroll()
  renderer.dispatchEvent(event('touchcancel', 120, 200, 50))
  assert.equal(renderer.container.scrollLeft, 600)
  assert.equal(renderer.state, null)
})
test('remaining-contact end after cancel cannot clear a fresh gesture on a later frame', async () => {
  const fixture = paginator()
  const renderer = fixture.target
  renderer.dispatchEvent(event('touchstart'))
  renderer.dispatchEvent(event('touchcancel'))
  renderer.dispatchEvent(event('touchend', 240, 200, 10))
  renderer.dispatchEvent(event('touchstart', 300, 200, 15))
  const freshState = renderer.state
  await fixture.frames()
  assert.equal(renderer.state, freshState)
  assert.equal(renderer.snapCalls.length, 0)
})
test('a completed gesture cleanup does not erase a newer active gesture', async () => {
  const fixture = paginator()
  const renderer = fixture.target
  renderer.dispatchEvent(event('touchstart'))
  renderer.dispatchEvent(event('touchend', 240, 200, 10))
  renderer.dispatchEvent(event('touchstart', 300, 200, 15))
  const freshState = renderer.state
  await fixture.frames()
  assert.equal(renderer.state, freshState, 'only the completed gesture may be released')
  assert.equal(renderer.snapCalls.length, 1, 'the previous valid end still uses its original settle path')
})
test('image cancel stops long-press action and a fresh hold still works', () => {
  const fixture = imageHandler()
  fixture.image.dispatchEvent(event('touchstart'))
  fixture.image.dispatchEvent(event('touchcancel'))
  fixture.runTimers()
  assert.equal(fixture.host.events.length, 0, 'cancel must not emit click-image later')
  fixture.image.dispatchEvent(event('touchstart'))
  fixture.runTimers()
  assert.equal(fixture.host.events.length, 1)
  const click = event('click')
  fixture.image.dispatchEvent(click)
  assert.equal(click.defaultPrevented, true, 'a completed long press still suppresses its click')
})
test('a second image contact cancels the first timer and cannot arm a pinch hold', () => {
  const fixture = imageHandler()
  fixture.image.dispatchEvent(event('touchstart'))
  const second = event('touchstart', 260, 200)
  second.touches.push({ screenX: 240, screenY: 200 })
  fixture.image.dispatchEvent(second)
  fixture.image.dispatchEvent(event('touchcancel'))
  fixture.runTimers()
  assert.equal(fixture.host.events.length, 0)
  fixture.image.dispatchEvent(second)
  fixture.runTimers()
  assert.equal(fixture.host.events.length, 0, 'two contacts alone must never arm the timer')
  fixture.image.dispatchEvent(event('touchcancel'))
  fixture.image.dispatchEvent(event('touchstart'))
  fixture.runTimers()
  assert.equal(fixture.host.events.length, 1, 'a fresh single-finger hold still works')
})
for (const finish of ['touchmove', 'touchend']) {
  test(`image ${finish} still stops the hold timer without swallowing a short click`, () => {
    const fixture = imageHandler()
    fixture.image.dispatchEvent(event('touchstart'))
    fixture.image.dispatchEvent(event(finish))
    fixture.runTimers()
    assert.equal(fixture.host.events.length, 0)
    const click = event('click')
    fixture.image.dispatchEvent(click)
    assert.equal(click.defaultPrevented, false)
  })
}

let failures = 0
for (const { name, run } of tests) {
  try { await run(); console.log(`PASS ${name}`) }
  catch (error) { ++failures; console.error(`FAIL ${name}: ${error.message}`) }
}
assert.equal(failures, 0, `${failures} reader gesture cancellation checks failed`)
console.log('ANDROID_READER_GESTURE_CANCEL_OK')
