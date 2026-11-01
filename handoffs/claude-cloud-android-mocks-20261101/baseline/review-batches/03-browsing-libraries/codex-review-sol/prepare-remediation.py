from pathlib import Path
import difflib,hashlib,json
b=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries');own=b/'codex-review-sol';candidate=own/'candidate';candidate.mkdir(exist_ok=True)
p=b/'finished-mocks/20-search-browse/app.js';original=p.read_text(encoding='utf-8');s=original
def replace(a,z):
 global s
 assert s.count(a)==1,(a[:80],s.count(a))
 s=s.replace(a,z)
replace("h(empty||browsing?'':S.query)","h(S.query)")
replace("(!empty&&!browsing?'<button class=\"clear\"", "(S.query.length?'<button class=\"clear\"")
replace("if(type==='search-clear'){S.query='';S.searchState='empty';render(true);return;}","if(type==='search-clear'){window.clearTimeout(window.mockSearchTimer);S.query='';S.searchState='empty';renderSearchUpdate(true,true);return;}")
replace("function onAppInput(event){",'''// Search input text is independent of the suggestions/results presentation.
// Rebuild the fixture without taking focus from an intentionally selected control.
function renderSearchUpdate(resetScroll=false,focusInput=false){
 const frame=document.getElementById('appFrame'),input=document.getElementById('searchInput'),active=document.activeElement;
 const keepInput=focusInput||active===input;
 const selection=input?[input.selectionStart,input.selectionEnd,input.selectionDirection]:[0,0,'none'];
 const action=!keepInput&&frame.contains(active)&&active.dataset.action?{type:active.dataset.action,value:active.dataset.value||'',label:active.getAttribute('aria-label')}:null;
 render(resetScroll);
 if(keepInput){
  const replacement=document.getElementById('searchInput');
  replacement.focus({preventScroll:true});
  replacement.setSelectionRange(selection[0],selection[1],selection[2]);
 }else if(action){
  const replacement=[...frame.querySelectorAll('[data-action]')].find(e=>e.dataset.action===action.type&&(e.dataset.value||'')===action.value&&e.getAttribute('aria-label')===action.label);
  if(replacement)replacement.focus({preventScroll:true});
 }
}
function onAppInput(event){''')
replace("window.mockSearchTimer=window.setTimeout(()=>{S.searchState=S.query.trim().length>1?'results':'empty';S.expanded={};const n=event.target.selectionStart;render();const replacement=document.getElementById('searchInput');if(replacement){replacement.focus();try{replacement.setSelectionRange(n,n)}catch(e){}}},210);","window.mockSearchTimer=window.setTimeout(()=>{S.searchState=S.query.trim().length>1?'results':'empty';S.expanded={};renderSearchUpdate();},210);")
replace("if(e.key==='Enter'&&e.target.id==='searchInput'){window.clearTimeout(window.mockSearchTimer);S.searchState=S.query.trim().length>1?'results':'empty';render();}","if(e.key==='Enter'&&e.target.id==='searchInput'){e.preventDefault();window.clearTimeout(window.mockSearchTimer);S.searchState=S.query.trim().length>1?'results':'empty';renderSearchUpdate();document.querySelector('.top-match [data-action=\"item-open\"]')?.click();}")
replace("if(STATE==='browse')S.searchState='browse';", "if(STATE==='browse'){S.searchState='browse';S.query='';}")
(candidate/'app.js').write_text(s,encoding='utf-8',newline='\n')
html=(b/'finished-mocks/20-search-browse/search.html').read_text(encoding='utf-8').replace('<head>','<head><base href="../../finished-mocks/20-search-browse/">').replace('src="app.js"','src="../../codex-review-sol/candidate/app.js"')
(candidate/'search.html').write_text(html,encoding='utf-8',newline='\n')
patch=''.join(difflib.unified_diff(original.splitlines(True),s.splitlines(True),fromfile='finished-mocks/20-search-browse/app.js',tofile='codex-review-sol/candidate/app.js'))
(own/'search-correction.patch').write_text(patch,encoding='utf-8')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
(own/'remediation-hashes-start.json').write_text(json.dumps({'original':{'path':str(p),'sha256':sha(p)},'candidate':{'path':str(candidate/'app.js'),'sha256':sha(candidate/'app.js')},'entry':{'path':str(candidate/'search.html'),'sha256':sha(candidate/'search.html')}},indent=2))
print(patch)
