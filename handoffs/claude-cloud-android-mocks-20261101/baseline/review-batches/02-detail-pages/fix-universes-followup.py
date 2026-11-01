from pathlib import Path
base=Path(r"D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\02-detail-pages\finished-mocks")
p=base/"19-generic-universes/app.js";s=p.read_text(encoding="utf-8")
s=s.replace('state.kind==="phone"?40:state.kind==="tablet"?64:72','state.kind==="phone"?40:72')
s=s.replace('''host.onfocus=()=>{if(!items.length)return;const current=items[host._index||0];''','''host.onfocus=()=>{if(!items.length)return;if(P==="saga"){const selected=items.findIndex(x=>x.dataset.select===state.selected);if(selected>=0)host._index=selected;}const current=items[host._index||0];''')
s=s.replace('''  host.onkeydown=e=>{
   const arrows''','''  host.onfocusin=e=>{const i=items.indexOf(e.target);if(i>=0)host._index=i;};
  host.onblur=e=>{if(!host.contains(e.relatedTarget))items.forEach(x=>x.classList.remove("keyboard-current"));};
  host.onkeydown=e=>{
   const arrows''')
s=s.replace('''   let index=host._index||0;''','''   let index=host._index||0;const direct=items.indexOf(e.target.closest("[data-select]"));if(direct>=0)index=direct;''')
s=s.replace('''t.classList.toggle("active",i===active);t.setAttribute("aria-current",i===active?"true":"false");''','''const was=t.classList.contains("active");t.classList.toggle("active",i===active);t.setAttribute("aria-current",i===active?"true":"false");if(i===active&&!was&&state.dock!=="original"){const track=t.parentElement,a=track.getBoundingClientRect(),r=t.getBoundingClientRect();if(r.left<a.left)track.scrollLeft+=r.left-a.left;else if(r.right>a.right)track.scrollLeft+=r.right-a.right;}''')
p.write_text(s,encoding="utf-8")
p=base/"19-generic-universes/style.css";s=p.read_text(encoding="utf-8").replace('--hero-size:64;--hero-height:400px','--hero-size:72;--hero-height:400px');p.write_text(s,encoding="utf-8")
p=base/"18-comic-details/app.js";s=p.read_text(encoding="utf-8")
s=s.replace('''key:"comic:Batman",title:"Batman",kind:"comic"''','''key:"comic:"+(page==="archive"?st.archiveBox:"Batman"),title:page==="archive"?st.archiveBox:"Batman",kind:"comic"''')
p.write_text(s,encoding="utf-8")
print("Final keyboard focus, source tablet type size and archive collection identity corrected.")
