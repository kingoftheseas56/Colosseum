from pathlib import Path
B=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries')
p=B/'finished-mocks/16-libraries/style.css'
s=p.read_text(encoding='utf-8');s=s.replace('background:#99000000;z-index:34','background:rgba(0,0,0,.6);z-index:34');p.write_text(s,encoding='utf-8')
p=B/'finished-mocks/20-search-browse/app.js'
s=p.read_text(encoding='utf-8')
s=s.replace("<span class=\"score\">★ 8.4</span>","<span class=\"score\">★ '+(item.score||8.4).toFixed(1)+'</span>")
s=s.replace("const items=laneArt();const genre=S.browseGenre;","const genre=S.browseGenre;const items=laneArt().filter(x=>x.genre===genre);")
s=s.replace("items.map(i=>searchResult(i,false)).join('')+'</div>';","(items.length?items.map(i=>searchResult(i,false)).join(''):'<p class=\"no-results\">No results</p>')+'</div>';")
s=s.replace('data-action="move-down" data-value="\'+h(name)+\'" title="Drag to reorder" aria-label="Reorder shelf"','data-action="reorder-handle" data-value="\'+h(name)+\'" title="Drag to reorder · Ctrl+Shift+Up/Down" aria-label="Reorder \'+h(name)+\'"')
s=s.replace('id="shelf-\'+idx+\'"','data-shelf="\'+h(name)+\'" id="shelf-\'+idx+\'"')
s=s.replace("if(type==='continue-detail'||type==='continue-resume'||type==='context-detail'||type==='context-resume'){S.continueMenu=null;flagAction", "if(type==='continue-detail'||type==='continue-resume'||type==='context-detail'||type==='context-resume'){S.continueMenu=null;render();flagAction")
s=s.replace("if(S.removed.length===CONTINUE.length)","if(S.removed.length===CONTINUE.length)")
s=s.replace("(S.removed.length===CONTINUE.length?'Nothing to continue.':'Nothing here yet.')", "(CONTINUE.filter(x=>!S.removed.includes(x.id)).filter(x=>S.scope==='video'?x.kind==='video':S.scope==='book'?x.kind==='book':S.scope==='tankoban'?['manga','comic'].includes(x.kind):true).length===0?'Nothing to continue.':'Nothing here yet.')")
s=s.replace("function init(){", "function init(){\n initBatchKeyboardAndDrag();")
p.write_text(s,encoding='utf-8')
print('Patched confirmation scrim, local genre browse, score labels, scoped empty copy and handle identity.')
