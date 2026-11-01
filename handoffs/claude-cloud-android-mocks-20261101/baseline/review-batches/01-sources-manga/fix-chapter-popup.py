from pathlib import Path
b=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\01-sources-manga')
p=b/'finished-mocks/04-manga-series/app.js';s=p.read_text(encoding='utf-8')
old='const h=Math.min(count*44+12,kind==="page"?320:232,Math.max(120,win.height-24));'
new='const nativeDesktopPage=kind==="page"&&sourceRoute==="reading-room"&&!document.getElementById("device").classList.contains("phone");const h=Math.min(count*(nativeDesktopPage?36:44)+12,kind==="page"?(nativeDesktopPage?300:320):232,Math.max(120,win.height-24));popup.dataset.kind=kind;'
assert old in s;s=s.replace(old,new);p.write_text(s,encoding='utf-8')
p=b/'finished-mocks/04-manga-series/style.css';s=p.read_text(encoding='utf-8')
s+='\n/* Native default page-menu geometry; retain larger phone hit regions. */\n.app[data-route=reading-room] .popup[data-kind=page] .popup-option{border-radius:7px;padding-left:10px}.app[data-route=reading-room] .popup[data-kind=page] .popup-option.current{background:rgba(255,255,255,.14)}.app[data-route=reading-room] .popup[data-kind=page] .popup-option:hover:not(.current){background:rgba(255,255,255,.10)}.device:not(.phone) .app[data-route=reading-room] .popup[data-kind=page] .popup-option{min-height:36px;height:36px}\n'
p.write_text(s,encoding='utf-8')
print('Restored default desktop page menu dimensions and source glass states.')
