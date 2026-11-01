from pathlib import Path
b=Path(__file__).parent
p=b/'assemble-handoff.py';s=p.read_text(encoding='utf-8')
s=s.replace("'universes-final-layout-retest.json']:","'universes-final-layout-retest.json','chrome-theatre-final-retest.json','chrome-comic-final-retest.json']:")
s=s.replace("'series-finished-season-landscape-stress'","'series-finished-verified-season-landscape-stress'")
s=s.replace("'series-finished-last-episode-landscape-small-stress'","'series-finished-verified-last-episode-landscape-small-stress'")
s=s.replace('All seventeen grouped repair findings','All eighteen grouped repair findings')
s=s.replace("'fixed_verified_findings':17","'fixed_verified_findings':18")
s=s.replace('and source 72px tablet titles.','and source 72px tablet titles.\\n- Restored the approved ChromeScrim recipe and Theatre’s phone/pinned scroll shading after final visual comparison exposed the missing/approximate source treatment.')
s=s.replace('with the Biblio tablet and Universe Continue-control retests.','with the Biblio tablet, Universe Continue-control and Theatre/Comics ChromeScrim retests.')
s=s.replace("for p in pages:\n cp=next(iter(B.glob('CHECKPOINT-'+p['task'].split('-')[0]+'-*')),None)\n","")
p.write_text(s,encoding='utf-8')
print('Assembly input updated to latest verified source-chrome retests.')
