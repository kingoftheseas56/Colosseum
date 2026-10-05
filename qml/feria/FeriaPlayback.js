// Shared observation and restore code for both browser engines. Page data never
// carries credentials or commands into the native activity store.
function destinationKey(value) {
    var parts = String(value).split("#")
    var address = parts.shift().split("?")
    var base = address.shift().replace(/^(https?:\/\/)([^/]+)/i, function(_, scheme, host) {
        return scheme.toLowerCase() + host.toLowerCase()
    })
    var query = (address.join("?") || "").split("&").filter(function(pair) {
        var key = pair.split("=")[0].toLowerCase()
        return pair && !/^utm_/.test(key) && ["fbclid","gclid","si","feature","trackid","tctx"].indexOf(key) < 0
    }).sort().join("&")
    return base + (query ? "?" + query : "") + (parts.length ? "#" + parts.join("#") : "")
}
function sameDestination(actual, requested) {
    return destinationKey(actual) === destinationKey(requested)
}
function canResume(sample) {
    return sample && !sample.paused && sample.readyState >= 3
            && sample.duration >= 30 && !sample.ad
}
var pageProbeSource = [
    "function pageProbe(mode, position, locator) {",
    "    var roots = [], media = [], seen = []",
    "    function visit(root, depth) {",
    "        if (!root || !root.querySelectorAll || depth > 5 || seen.indexOf(root) >= 0) return",
    "        seen.push(root); roots.push(root)",
    "        Array.from(root.querySelectorAll('video,audio')).forEach(function(m) { media.push({media:m,root:root}) })",
    "        Array.from(root.querySelectorAll('iframe,frame')).slice(0,32).forEach(function(frame) {",
    "            try { visit(frame.contentDocument, depth + 1) } catch (_) {} // Same-origin access only.",
    "        })",
    "        Array.from(root.querySelectorAll('*')).slice(0,3000).forEach(function(node) {",
    "            if (node.shadowRoot) visit(node.shadowRoot, depth + 1)",
    "        })",
    "    }",
    "    visit(document, 0)",
    "    media.sort(function(a,b) {",
    "        return Number(!b.media.paused) - Number(!a.media.paused) || (b.media.duration || 0) - (a.media.duration || 0)",
    "    })",
    "    var selected = media[0], m = selected && selected.media",
    "    var ad = !!document.querySelector('.ad-showing,.ad-interrupting')",
    "        || !!(selected && selected.root.querySelector('.ad-showing,.ad-interrupting'))",
    "    // MANGA Plus appends pages as the internal surface scrolls. Anchor to",
    "    // a page, not a fraction of that changing extent. Vertical reader only.",
    "    var manga = typeof location !== 'undefined' && /^https:\\/\\/mangaplus\\.shueisha\\.co\\.jp\\/viewer\\/\\d+(?:[?#]|$)/i.test(location.href)",
    "    var surface = manga && document.querySelector('.zao-surface')",
    "    var pages = surface ? Array.from(surface.querySelectorAll('.zao-container')) : []",
    "    var counter = manga && document.body && (document.body.innerText || '').match(/\\b(\\d+)\\s*\\/\\s*(\\d+)\\b/)",
    "    var total = counter ? Number(counter[2]) : 0",
    "    function loadedPage(index) {",
    "        return pages.slice(0,index+1).every(function(page) {",
    "            var images = Array.from(page.querySelectorAll('img'))",
    "            return page.getBoundingClientRect().height > 1 && images.length > 0",
    "                && images.every(function(image) { return image.complete && image.naturalWidth > 0 })",
    "        })",
    "    }",
    "    if (mode === 'seek') {",
    "        if (!m || m.paused || m.readyState < 3 || !Number.isFinite(m.duration) || m.duration < 30 || ad) return false",
    "        try { m.currentTime = Math.min(position, Math.max(0,m.duration-1)); return Math.abs(m.currentTime-position) < 2 } catch (_) { return false }",
    "    }",
    "    if (mode === 'restore-reading') {",
    "        if (locator && locator.type === 'mangaplus-page') {",
    "            if (!surface || surface.scrollHeight <= surface.clientHeight + 100",
    "                || !Number.isInteger(locator.page) || !Number.isInteger(locator.total)",
    "                || locator.page < 0 || locator.page >= locator.total || locator.total > 10000",
    "                || !Number.isFinite(locator.offset) || locator.offset < 0 || locator.offset > 1",
    "                || total !== locator.total) return false",
    "            if (!pages[locator.page]) { // Trigger the reader's next lazy batch.",
    "                surface.scrollTop = surface.scrollHeight - surface.clientHeight; return false",
    "            }",
    "            if (!loadedPage(locator.page)) return false",
    "            var rect = pages[locator.page].getBoundingClientRect()",
    "            var top = surface.scrollTop + rect.top - surface.getBoundingClientRect().top + locator.offset * rect.height",
    "            var previous = window.__feriaMangaRestore",
    "            surface.scrollTop = top",
    "            window.__feriaMangaRestore = {page:locator.page,offset:locator.offset,total:total,top:top}",
    "            return !!previous && previous.page === locator.page && previous.offset === locator.offset",
    "                && previous.total === total && Math.abs(previous.top-top) < 2 && Math.abs(surface.scrollTop-top) < 2",
    "        }",
    "        if (!locator || locator.type !== 'scroll') return false",
    "        var target",
    "        try { target = locator.selector ? document.querySelector(locator.selector) : document.scrollingElement } catch (_) { return false }",
    "        if (!target || target.scrollHeight - target.clientHeight < 1) return false",
    "        target.scrollTop = Math.max(0, Math.min(1, locator.fraction)) * (target.scrollHeight - target.clientHeight)",
    "        return Math.abs(target.scrollTop / (target.scrollHeight-target.clientHeight) - locator.fraction) < 0.03",
    "    }",
    "    var meta = typeof navigator !== 'undefined' && navigator.mediaSession && navigator.mediaSession.metadata",
    "    var result = {href:location.href, title:meta && meta.title || document.title,",
    "        kind:mode === 'listen' || m && m.tagName === 'AUDIO' ? 'audio' : 'video', position:m ? m.currentTime : 0,",
    "        duration:m && Number.isFinite(m.duration) ? m.duration : 0, paused:!m || m.paused, readyState:m ? m.readyState : 0,",
    "        ended:!!m && m.ended, rate:m ? m.playbackRate : 1, ad:ad}",
    "    if (mode !== 'read') return result",
    "    // Reader routes, not shop/catalogue pages. Canvas readers still retain the",
    "    // chapter URL; a percentage is saved only when an actual scroll range exists.",
    "    if (!/\\/(?:viewer|reader|read|chapter|chapters|episode)(?:[/?#-]|$)|[?&](?:asin|episode_no|chapter|pg)=/i.test(location.href)",
    "        && !document.querySelector('[data-reader],.reader-container,#reader')) return result",
    "    if (!window.__feriaReadingActivity) {",
    "        window.__feriaReadingActivity = {lastInput:0}",
    "        ;['pointerdown','keydown','wheel','touchstart'].forEach(function(type) {",
    "            window.addEventListener(type, function(event) {",
    "                if (event.isTrusted) window.__feriaReadingActivity.lastInput = Date.now()",
    "            }, {passive:true,capture:true})",
    "        })",
    "    }",
    "    var target = document.scrollingElement, selector = ''",
    "    Array.from(document.querySelectorAll('[data-reader],.reader-container,#reader,main,article,[role=\"main\"]')).forEach(function(node) {",
    "        if (node.scrollHeight > node.clientHeight + 100 && node.clientHeight > 150",
    "                && (!target || target.scrollHeight-target.clientHeight < node.scrollHeight-node.clientHeight)",
    "                && node.id && /^[A-Za-z][\\w-]*$/.test(node.id)) { target=node; selector='#'+node.id }",
    "    })",
    "    var range = target ? target.scrollHeight-target.clientHeight : 0",
    "    var fraction = range > 0 ? Math.max(0,Math.min(1,target.scrollTop/range)) : 0",
    "    result.kind = 'book'; result.position = range > 0 ? fraction * 100 : 0",
    "    result.duration = range > 0 ? 100 : 0; result.ad = false; result.ended = false",
    "    result.paused = document.visibilityState !== 'visible' || Date.now()-window.__feriaReadingActivity.lastInput > 60000",
    "    result.locator = range > 0 ? {type:'scroll',selector:selector,fraction:fraction} : {type:'url'}",
    "    if (surface && surface.scrollHeight > surface.clientHeight + 100 && pages.length",
    "            && total > 0 && total <= 10000 && pages.length <= total) {",
    "        var surfaceTop = surface.getBoundingClientRect().top, pageIndex = 0",
    "        pages.forEach(function(page,index) { if (page.getBoundingClientRect().top <= surfaceTop + 1) pageIndex = index })",
    "        if (loadedPage(pageIndex)) {",
    "            var pageRect = pages[pageIndex].getBoundingClientRect()",
    "            var offset = Math.max(0,Math.min(1,(surfaceTop-pageRect.top)/pageRect.height))",
    "            result.locator = {type:'mangaplus-page',page:pageIndex,offset:offset,total:total}",
    "            result.position = 100 * (pageIndex + offset) / total; result.duration = 100",
    "        }",
    "    }",
    "    return result",
    "}"
].join("\n")

function observation(mode) { return '(' + pageProbeSource + ')(' + JSON.stringify(mode || 'watch') + ',null,null)' }
var sample = observation('watch')
function seek(position) { return '(' + pageProbeSource + ')("seek",' + Math.max(0,Number(position)||0) + ',null)' }
function restoreReading(locator) { return '(' + pageProbeSource + ')("restore-reading",null,' + JSON.stringify(locator) + ')' }
