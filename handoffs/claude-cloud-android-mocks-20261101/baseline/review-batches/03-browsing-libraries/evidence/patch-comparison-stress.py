from pathlib import Path
p=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries\compare.html')
s=p.read_text(encoding='utf-8')
s=s.replace('Artwork and data are declared fixtures; no native app or provider is running.','Artwork and data are declared fixtures; no native app or provider is running. Stress controls use original inspector inputs where older workers ignore URL parameters. If local file security blocks this, use a local HTTP origin.')
s=s.replace('<script>','<script>\n$("before");' if False else '<script>',1)
needle='for(const id of ["page","size","stress"])'
insert='''$("before").addEventListener("load",()=>{if(!$("stress").checked)return;try{const w=$("before").contentWindow,d=w.document,job=$("page").value.split("/")[0];if(job==="15-world-home"){for(const [id,v]of Object.entries({fontScale:"1.3",insetTop:"24",insetBottom:"24"})){const el=d.getElementById(id);if(el){el.value=v;el.dispatchEvent(new w.Event("change",{bubbles:true}));}}}if(job==="20-search-browse")w.eval("S.font=1.3;S.top=24;S.bottom=24;render();");}catch(e){document.querySelector(".note").textContent="Local file security blocked the original worker inspector adjustment. Use a local HTTP origin for matched stress controls; saved final stress captures remain available.";}});\n'''
s=s.replace(needle,insert+needle)
p.write_text(s,encoding='utf-8');print('Live comparison now applies original inspector stress controls without editing original files.')
