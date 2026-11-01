from pathlib import Path
p=Path(r"D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\02-detail-pages\finished-mocks\17-theatre-details\app.js")
s=p.read_text(encoding="utf-8")
s=s.replace('scroll.scrollTop = Math.max(0,section.offsetTop - 78);','scroll.scrollTop += section.getBoundingClientRect().top - app.getBoundingClientRect().top - (parseFloat(getComputedStyle(app).getPropertyValue("--safe-top"))||0) - 78;')
s=s.replace('scroll.scrollTop = Math.max(0,row.offsetTop - (chrome ? chrome.offsetHeight : 0) - 82);','scroll.scrollTop += row.getBoundingClientRect().top - app.getBoundingClientRect().top - (parseFloat(getComputedStyle(app).getPropertyValue("--safe-top"))||0) - (chrome ? chrome.offsetHeight : 0) - 80;\\n  row.focus({preventScroll:true});'.replace("\\n","\n"))
p.write_text(s,encoding="utf-8")
p=p.with_name("style.css")
s=p.read_text(encoding="utf-8")+"""
/* Fit the existing legacy ledger controls in two rows on short phone landscapes. */
#app.legacy.phone-landscape .season-chrome{display:grid;grid-template-columns:max-content minmax(0,1fr);column-gap:22px;align-items:center}
#app.legacy.phone-landscape .order-row{grid-column:1;min-height:44px;padding-top:6px}
#app.legacy.phone-landscape .season-pick-row{grid-column:2;min-height:44px}
#app.legacy.phone-landscape .ledger-head{grid-column:1/-1;min-height:0;padding:4px 0 8px}
#app.phone-landscape .episode-name{white-space:normal;overflow:visible;text-overflow:clip}
"""
p.write_text(s,encoding="utf-8")
print("Fixed row coordinates for Go to episode; compacted existing landscape ledger controls.")
