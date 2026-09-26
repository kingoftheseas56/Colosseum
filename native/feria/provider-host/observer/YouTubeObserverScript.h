#pragma once

// Installed before navigation. The observer reads only the A2 allowlisted
// playback fields and never controls the page or sends data off the machine.
inline constexpr wchar_t kYouTubeObserverScript[] = LR"FERIA(
(() => {
  const sample = () => {
    const href = location.href;
    const host = new URL(href).hostname;
    if (host !== 'youtube.com' && !host.endsWith('.youtube.com')) return;
    const media = document.querySelector('video, audio');
    const player = document.querySelector('#movie_player');
    const metadata = navigator.mediaSession && navigator.mediaSession.metadata;
    chrome.webview.postMessage({
      kind: 'feria-youtube-observation-v1',
      href,
      media: media ? {
        currentTime: media.currentTime,
        duration: media.duration,
        paused: media.paused,
        ended: media.ended
      } : null,
      title: metadata ? metadata.title : null,
      adShowing: !!(player && player.classList.contains('ad-showing')),
      adInterrupting: !!(player && player.classList.contains('ad-interrupting'))
    });
  };
  setInterval(sample, 1000);
  document.addEventListener('ended', sample, true);
  sample();
})()
)FERIA";
