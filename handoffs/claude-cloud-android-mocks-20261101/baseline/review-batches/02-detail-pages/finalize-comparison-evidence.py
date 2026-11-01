from pathlib import Path
import json
B=Path(__file__).parent
p=B/'assemble-handoff.py';s=p.read_text(encoding='utf-8')
s=s.replace("rowh=max(x.height for x in ims)+99","rowh=max(x.height for x in ims)+119")
old="x=pad+col*(cell+gap);d.text((x,y+42),lbl,font=fsmall,fill='#e3e5e9');out.paste(im,(x+(cell-im.width)//2,y+66));m=meta(nm);d.text((x,y+70+im.height),nm[:75],font=fsmall,fill='#b7bdc7')"
new="x=pad+col*(cell+gap);d.text((x,y+42),lbl,font=fsmall,fill='#e3e5e9');raw=Image.open(C/(nm+'.png'));d.text((x,y+61),str(raw.width)+' x '+str(raw.height)+' capture; settings in linked metadata',font=fsmall,fill='#aeb8c6');out.paste(im,(x+(cell-im.width)//2,y+86));m=meta(nm);d.text((x,y+90+im.height),nm[:75],font=fsmall,fill='#b7bdc7')"
assert old in s;s=s.replace(old,new)
s=s.replace("'film-original-rebuilt-hidden-more','film-finished-rebuilt-full-synopsis-stress'","'film-original-synopsis-visible-portrait-small-stress','film-finished-synopsis-visible-portrait-small-stress'")
s=s.replace("'series':[('Open season picker","'series':[('Opt-in full synopsis at matched portrait stress','series-original-synopsis-visible-portrait-small-stress','series-finished-synopsis-visible-portrait-small-stress'),\n('Open season picker")
s=s.replace("Alternate source page at landscape stress","Source scrolling: original top / finished final results at landscape stress")
s=s.replace("at matched scroll positions.","after the same first-tab action; each mode uses its own correct landing offset. Earlier fixed-scroll captures are preserved.")
old="a=slug+'-tabs-original-'+size+'-stress';z=slug+'-tabs-proposed-'+size+'-stress'"
new="middle='-bond-landing-' if slug=='shelves' else '-landing-';a=slug+'-tabs-original'+middle+size+'-stress';z=slug+'-tabs-proposed'+middle+size+'-stress'"
assert old in s;s=s.replace(old,new)
s=s.replace("Both attempts are runnable and have matched evidence.","Both attempts are runnable and have matched action-based evidence. Actual tab landings clear their headings in all 24 final cases (Saga, Bond shelves and magazine shelves; both modes at four phone sizes).")
p.write_text(s,encoding='utf-8')
pack=B/'pack-content.json';d=json.loads(pack.read_text(encoding='utf-8'))
key='DECISION-B2-018.md';t=d['docs'][key]
t=t.replace('with the same scale, insets and scrolled position.','with the same scale and insets, after activating the same first tab. Each mode uses its own landing offset so its heading remains below its chrome.')
t=t.replace('Evidence ledgers: saga-finished-stress.json and shelves-finished-stress.json.','Final action-based evidence: universe-tab-landing-verified.json (Saga and magazine shelves, 16 cases) and shelves-bond-tab-landing-verified.json (Bond shelves, 8 cases). All 24 cases land headings below the tab bar; all proposed bars remain contained. Earlier fixed-scroll evidence remains in saga-finished-stress.json and shelves-finished-stress.json. A fixed scroll position may naturally place content behind sticky chrome; it is not the tab-action landing.')
d['docs'][key]=t
d['docs']['ASSETS-AND-PROVENANCE.md']+='\n## Final comparison clarification\n\nThe decision contact sheets now use actual first-tab actions, with 24 verified landing cases across Saga, Bond shelves and magazine shelves. Earlier identical-scroll-position captures remain available; they can show a heading passing behind sticky chrome and do not represent a tab landing. The two Theatre synopsis comparisons now show the paragraph at matched 354x792, 1.3 text and 24px insets, proving the original hidden three-line clamp versus complete final copy. Theatre-synopsis-visible-comparisons.json records both states. Contact-sheet panels label their capture dimensions; some key-fix transitions intentionally show different states, as named.\n'
for page in d['spec']['pages']:
 if page['slug'] in ['film','series']:page['evidence']+=';theatre-synopsis-visible-comparisons.json'
 if page['slug'] in ['saga','shelves']:page['evidence']+=';universe-tab-landing-verified.json;shelves-bond-tab-landing-verified.json'
for row in d['spec']['findings']:
 if row[0]=='B2-003':
  row[7]+='; captures/film-original-synopsis-visible-portrait-small-stress.png'
  row[8]+='; theatre-synopsis-visible-comparisons.json; captures/film-finished-synopsis-visible-portrait-small-stress.png'
 if row[0]=='B2-018':row[8]+='; universe-tab-landing-verified.json; shelves-bond-tab-landing-verified.json'
pack.write_text(json.dumps(d,indent=2,ensure_ascii=False),encoding='utf-8')
print('Comparison evidence and labels updated; runtime unchanged.')

