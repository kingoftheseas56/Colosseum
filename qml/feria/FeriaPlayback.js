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
    "    if (typeof window !== 'undefined') window.__feriaProbeMedia = m || null",
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
    "        try { var target = Math.min(position, Math.max(0,m.duration-1)); m.currentTime = target; return Math.abs(m.currentTime-target) < 2 } catch (_) { return false }",
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
// Single-write compatibility acknowledgement only; production settlement uses seekMedia + FeriaResume.
function seek(position) { return '(' + pageProbeSource + ')("seek",' + Math.max(0,Number(position)||0) + ',null)' }
function restoreReading(locator) { return '(' + pageProbeSource + ')("restore-reading",null,' + JSON.stringify(locator) + ')' }

// Qualified destination matching is narrow; generic/reading URL rules are unchanged.
function mediaKey(provider, href) {
    var text = String(href || "")
    var address = destinationKey(text).split("#")[0]
    if (provider === "ytmusic" && /^https:\/\/music\.youtube\.com(?::443)?\/watch[?]/.test(address)) {
        try {
            var pairs = address.split("?")[1].split("&").filter(function(pair) {
                return decodeURIComponent(pair.split("=")[0]) === "v"
            })
            var id = pairs.length === 1 ? decodeURIComponent(pairs[0].slice(pairs[0].indexOf("=")+1)) : ""
            if (/^[A-Za-z0-9_-]{11}$/.test(id)) return "ytmusic\n" + id
        } catch (e) {}
    }
    return destinationKey(text)
}
var mediaEnvelopeSource = [
    "function envelope(probe, connector, mode, provider, token) {",
    " if(Number.isFinite(window.__feriaRequestToken) && token<window.__feriaRequestToken)",
    "   return {outcome:'hold',reason:'stale-request'}",
    " window.__feriaRequestToken = token",
    " var selected, result = mode === 'read' ? {outcome:'not-applicable'} : connector(provider,function(){return probe(mode,null,null)})",
    " if (result.outcome === 'not-applicable') {",
    "   result = {outcome:'observation',sample:probe(mode,null,null)}; selected=window.__feriaProbeMedia",
    " } else if (result.outcome === 'observation') selected=result.fallback ? window.__feriaProbeMedia : window.__feriaConnectorMedia",
    " if (result.outcome !== 'observation' || !result.sample) return result",
    " var sample=result.sample; if(sample.kind==='book') return result",
    " var key=sample.providerMediaId ? provider+'\\n'+sample.providerMediaId : sample.href",
    " if(!sample.providerMediaId) try {",
    "   var clean=new URL(sample.href)",
    "   Array.from(clean.searchParams.keys()).forEach(function(name){",
    "     if(/^utm_/i.test(name) || ['fbclid','gclid','si','feature','trackid','tctx'].indexOf(name.toLowerCase())>=0)",
    "       clean.searchParams.delete(name)",
    "   });clean.searchParams.sort();key=clean.href",
    " } catch(e) {}",
    " var src=selected && (selected.currentSrc || selected.src || '')",
    " var tracker=window.__feriaMediaEpoch",
    " if(selected && (!tracker || tracker.media!==selected)) {",
    "   tracker={media:selected,epoch:0,started:selected.readyState>=1,loading:false}",
    "   window.__feriaMediaEpoch=tracker",
    "   if(selected.addEventListener) ['loadedmetadata','emptied','loadstart'].forEach(function(type){",
    "     selected.addEventListener(type,function(){",
    "       if(type==='loadedmetadata') {",
    "         if(tracker.started && !tracker.loading) ++tracker.epoch",
    "         tracker.started=true;tracker.loading=false",
    "       } else {",
    "         if(tracker.started && !tracker.loading) ++tracker.epoch",
    "         tracker.loading=true",
    "       }",
    "     },{passive:true})",
    "   })",
    " }",
    " var mediaEpoch=tracker && tracker.media===selected ? tracker.epoch : 0",
    " var old=window.__feriaFrame, generation=old ? old.generation : 0",
    " if(!old || old.key!==key || (old.media && old.media!==selected)",
    "   || (old.src && old.src!==src) || (old.media===selected && old.mediaEpoch!==mediaEpoch)) ++generation",
    " var documentToken=old ? old.documentToken : Date.now().toString(36)+Math.random().toString(36)",
    " window.__feriaFrame={key:key,media:selected,src:src,mediaEpoch:mediaEpoch,generation:generation,documentToken:documentToken}",
    " sample.mediaGeneration=documentToken+':'+String(generation); sample.sourceKey=key",
    " return result",
    "}"
].join("\n")
function invalidateMediaRequest(token) {
    return '(function(){var token=' + Number(token) + ';'
        + 'if(!Number.isFinite(window.__feriaRequestToken)||window.__feriaRequestToken<token)'
        + 'window.__feriaRequestToken=token})()'
}
function mediaObservation(mode, provider, connector, token) {
    return '(' + mediaEnvelopeSource + ')(' + pageProbeSource + ',' + connector + ','
        + JSON.stringify(mode) + ',' + JSON.stringify(provider) + ',' + Number(token) + ')'
}
function seekMedia(position, sample, mode, provider, connector, token) {
    return '(function(){if(window.__feriaRequestToken!==' + Number(token) + ') return false;'
        + 'var frame=' + mediaObservation(mode,provider,connector,token) + ';'
        + 'if(!frame || frame.outcome!=="observation") return false;var s=frame.sample;'
        + 'if(s.mediaGeneration!==' + JSON.stringify(sample.mediaGeneration)
        + '||s.sourceKey!==' + JSON.stringify(sample.sourceKey) + ') return false;'
        + 'var m=window.__feriaFrame.media;if(!m||m.isConnected===false||m.paused||m.readyState<3||s.ad) return false;'
        + 'try{m.currentTime=Math.min(' + Number(position) + ',Math.max(0,m.duration-1));return true}catch(e){return false}})()'
}

// Volatile, document-owned provenance only. Inaccessible cross-origin frames
// are deliberately untouched: inability to inspect playback never authorizes play.
var presentationMediaSource = [
    "function presentationMedia(suspended, epoch, serial) {",
    " var ledger=window.__feriaPresentationMedia",
    " if(ledger && (ledger.epoch>epoch || (ledger.epoch===epoch && ledger.serial>serial))) return false",
    " if(!ledger) {",
    "   ledger={epoch:epoch,serial:serial,suspended:false,records:new WeakMap(),roots:new WeakSet(),paused:new Set()}",
    "   window.__feriaPresentationMedia=ledger",
    " }",
    " if(ledger.epoch!==epoch) {",
    "   ledger.paused.forEach(function(record){record.owned=false});ledger.paused.clear();ledger.epoch=epoch",
    " }",
    " ledger.serial=serial;ledger.suspended=suspended",
    " function revoke(record) {record.owned=false;ledger.paused.delete(record)}",
    " function watch(m) {",
    "   var record=ledger.records.get(m)",
    "   if(record) return record",
    "   record={media:m,doc:m.ownerDocument,src:'',owned:false,ownPauseEvent:false,pause:m.pause,play:m.play}",
    "   ledger.records.set(m,record)",
    "   // pause() on an already auto-paused player emits no DOM event. Catch",
    "   // explicit provider/user commands too, rather than guessing on return.",
    "   try {",
    "     m.pause=function(){revoke(record);return record.pause.apply(this,arguments)}",
    "     m.play=function(){revoke(record);return record.play.apply(this,arguments)}",
    "   } catch(_) {}",
    "   m.addEventListener('pause',function(){",
    "     if(record.ownPauseEvent) {record.ownPauseEvent=false;return} revoke(record)",
    "   })",
    "   m.addEventListener('play',function(){if(!ledger.suspended)revoke(record)})",
    "   ;['ended','emptied','loadstart'].forEach(function(type){m.addEventListener(type,function(){revoke(record)})})",
    "   ;['pointerdown','keydown'].forEach(function(type){m.addEventListener(type,function(){revoke(record)},true)})",
    "   return record",
    " }",
    " function hold(m) {",
    "   var record=watch(m)",
    "   if(m.paused || m.ended || m.isConnected===false) return",
    "   record.src=m.currentSrc || m.src || '';record.ownPauseEvent=true",
    "   try{record.pause.call(m);record.owned=m.paused;if(record.owned)ledger.paused.add(record)}catch(_){record.ownPauseEvent=false}",
    " }",
    " var seen=[],media=[]",
    " function visit(root,depth) {",
    "   if(!root || !root.querySelectorAll || depth>5 || seen.indexOf(root)>=0) return",
    "   seen.push(root)",
    "   if(!ledger.roots.has(root)) {",
    "     ledger.roots.add(root)",
    "     // Media created or started after the transition is held by events.",
    "     // This runs entirely in the document and never wakes a Frozen view.",
    "     root.addEventListener('play',function(event){",
    "       var m=event.target;if(ledger.suspended && m && /^(VIDEO|AUDIO)$/.test(m.tagName))hold(m)",
    "     },true)",
    "     // Custom controls may hold a native pause method captured before us.",
    "     // Any page interaction while suspended conservatively revokes resume.",
    "     ;['pointerdown','keydown'].forEach(function(type){root.addEventListener(type,function(){",
    "       if(ledger.suspended) {ledger.paused.forEach(revoke);ledger.paused.clear()}",
    "     },true)})",
    "   }",
    "   Array.from(root.querySelectorAll('video,audio')).slice(0,100).forEach(function(m){media.push(m)})",
    "   Array.from(root.querySelectorAll('iframe,frame')).slice(0,32).forEach(function(frame){try{visit(frame.contentDocument,depth+1)}catch(_){}})",
    "   Array.from(root.querySelectorAll('*')).slice(0,3000).forEach(function(node){if(node.shadowRoot)visit(node.shadowRoot,depth+1)})",
    " }",
    " visit(document,0)",
    " if(suspended) {",
    "   media.forEach(hold)",
    " } else {",
    "   var paused=Array.from(ledger.paused);ledger.paused.clear()",
    "   paused.forEach(function(record){",
    "     var m=record.media,owned=record.owned;record.owned=false",
    "     if(!owned || !m.paused || m.ended || m.isConnected===false || m.ownerDocument!==record.doc",
    "       || (m.currentSrc || m.src || '')!==record.src) return",
    "     try{var promise=record.play.call(m);if(promise && promise.catch)promise.catch(function(){})}catch(_){}",
    "   })",
    " }",
    " return true",
    "}"
].join("\n")
function presentationMedia(suspended, documentEpoch, serial) {
    return '(' + presentationMediaSource + ')(' + (suspended ? 'true' : 'false') + ','
        + Number(documentEpoch) + ',' + Number(serial) + ')'
}
