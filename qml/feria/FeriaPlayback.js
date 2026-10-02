// The same read-only observation runs in both engines. No cookies, credentials,
// storage, or arbitrary page messages enter the activity store.
var sample = "(() => { const media = Array.from(document.querySelectorAll('video,audio')).sort((a,b) => Number(!b.paused)-Number(!a.paused) || b.duration-a.duration)[0]; const meta = navigator.mediaSession && navigator.mediaSession.metadata; return {href:location.href,title:meta && meta.title || document.title,kind:media && media.tagName === 'AUDIO' ? 'audio' : 'video',position:media && media.currentTime,duration:media && Number.isFinite(media.duration) ? media.duration : 0,paused:!media || media.paused,ended:!!media && media.ended,rate:media ? media.playbackRate : 1,ad:!!document.querySelector('.ad-showing,.ad-interrupting')}; })()"

function sameDestination(actual, requested) {
    // Never seek a login redirect or another title reached while signing in.
    return String(actual) === String(requested)
}

function seek(position) {
    return "(() => { const m = Array.from(document.querySelectorAll('video,audio')).sort((a,b) => Number(!b.paused)-Number(!a.paused) || b.duration-a.duration)[0]; if (!m || !Number.isFinite(m.duration) || m.duration < 30 || document.querySelector('.ad-showing,.ad-interrupting')) return false; m.currentTime = Math.min(" + Math.max(0, Number(position) || 0) + ", Math.max(0,m.duration-1)); return true; })()"
}
