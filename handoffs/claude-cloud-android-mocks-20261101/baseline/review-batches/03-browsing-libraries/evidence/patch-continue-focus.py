from pathlib import Path
p=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries\finished-mocks\20-search-browse\app.js')
s=p.read_text(encoding='utf-8').replace("const host=document.getElementById('appFrame');let dragging=null;","const host=document.getElementById('appFrame');let dragging=null,menuScroll=0;")
s=s.replace("host.querySelector('.tile-context button')?.focus({preventScroll:true});};","host.querySelector('.tile-context button')?.focus({preventScroll:true});menuScroll=host.querySelector('.scroll')?.scrollTop||0;};")
s=s.replace("S.continueMenu&&e.target.classList?.contains('scroll')","S.continueMenu&&e.target.classList?.contains('scroll')&&Math.abs(e.target.scrollTop-menuScroll)>1")
p.write_text(s,encoding='utf-8')
print('Preserved context menu across pending no-op scroll events.')
