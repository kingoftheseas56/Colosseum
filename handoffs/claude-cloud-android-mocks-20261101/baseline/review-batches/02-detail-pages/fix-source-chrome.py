from pathlib import Path
b=Path(__file__).parent/'finished-mocks'
p=b/'18-comic-details/style.css';s=p.read_text(encoding='utf-8')
old='top:-28px;height:92px;background:linear-gradient(#000b,#0000)'
new='top:calc(-28px - var(--inset-top,0px));height:calc(120px + var(--inset-top,0px));background:linear-gradient(rgba(0,0,0,.55),transparent)'
assert old in s;p.write_text(s.replace(old,new),encoding='utf-8')
p=b/'17-theatre-details/app.js';s=p.read_text(encoding='utf-8')
func='''function syncSourceChrome() {
  const bar=$("bar-chrome");if(bar)bar.classList.toggle("solid",scroll.scrollTop>40);
  const safe=parseFloat(getComputedStyle(app).getPropertyValue("--safe-top"))||0;
  const narrow=app.classList.contains("android-host")&&app.clientWidth<840;
  app.style.setProperty("--phone-scrim-opacity",narrow?String(Math.min(1,Math.max(0,(scroll.scrollTop-32)/40))):"0");
  app.style.setProperty("--sticky-chrome-color",narrow?"#08090d":"rgba(4,5,9,.93)");
  const header=content.querySelector(".season-chrome");
  const pinned=!!header&&scroll.scrollTop>0&&header.getBoundingClientRect().top<=app.getBoundingClientRect().top+72+safe+1;
  app.classList.toggle("sticky-scrim",pinned);
}
'''
s=s.replace('function render(preserveScroll = true) {',func+'\nfunction render(preserveScroll = true) {',1)
old='  app.classList.toggle("sticky-scrim",!!$("episode-section") && scroll.scrollTop >= $("episode-section").offsetTop - 90);'
assert old in s;s=s.replace(old,'  syncSourceChrome();',1)
s=s.replace('  app.classList.add("sticky-scrim");','  syncSourceChrome();',1)
old='''  const bar=$("bar-chrome");if(bar)bar.classList.toggle("solid",scroll.scrollTop>40);
  const section=$("episode-section");
  app.classList.toggle("sticky-scrim",!!section && scroll.scrollTop>=section.offsetTop-90);'''
assert old in s;s=s.replace(old,'  syncSourceChrome();',1);p.write_text(s,encoding='utf-8')
p=b/'17-theatre-details/style.css';s=p.read_text(encoding='utf-8')
old='''#app.legacy #chrome::before { content: ""; position: absolute; top: 0; left: 0; right: 0; height: calc(72px + var(--safe-top)); background: #08090d; opacity: 0; pointer-events: none; }
#app.legacy.sticky-scrim #chrome::before { opacity: 1; }'''
new='''/* Approved ChromeScrim.qml plus TheatreSeries.qml narrow-phone and pinned-ledger shading. */
#app.legacy #chrome::before{content:"";position:absolute;top:0;left:0;right:0;height:calc(120px + var(--safe-top));background:linear-gradient(rgba(0,0,0,.55),transparent);pointer-events:none;z-index:-2}
#app.legacy #chrome::after{content:"";position:absolute;top:0;left:0;right:0;height:calc(86px + var(--safe-top));background:#08090d;opacity:var(--phone-scrim-opacity,0);pointer-events:none;z-index:-1}
#app.legacy .season-chrome{background:transparent}
#app.legacy.sticky-scrim #chrome::after{height:calc(72px + var(--safe-top));opacity:1;background:var(--sticky-chrome-color)}
#app.legacy.sticky-scrim .season-chrome{background:var(--sticky-chrome-color)}'''
assert old in s;p.write_text(s.replace(old,new),encoding='utf-8')
print('Matched approved ChromeScrim and Theatre phone/pinned-ledger source conditions.')
