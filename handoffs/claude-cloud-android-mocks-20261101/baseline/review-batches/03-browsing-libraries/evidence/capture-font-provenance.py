from pathlib import Path
p=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries\evidence\browser-session.mjs')
s=p.read_text(encoding='utf-8')
s=s.replace('fonts:document.fonts.status,scrolls:',"fonts:document.fonts.status,fontScaleVariables:root?Object.fromEntries(['--font-scale','--fs','--s'].map(k=>[k,getComputedStyle(root).getPropertyValue(k).trim()])):{},fontSamples:[...document.querySelectorAll('button,h1,h2,p')].filter(visible).slice(0,12).map(e=>({text:e.textContent.trim().slice(0,36),size:getComputedStyle(e).fontSize,family:getComputedStyle(e).fontFamily})),scrolls:")
p.write_text(s,encoding='utf-8')
print('Added explicit font-scale and computed-font provenance to capture metadata.')
