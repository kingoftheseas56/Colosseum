from pathlib import Path
b=Path(__file__).parent;p=b/'assemble-handoff.py';s=p.read_text(encoding='utf-8')
s=s.replace("'chrome-comic-final-retest.json']:","'chrome-comic-final-retest.json','handoff-theatre-layouts.json','handoff-comics-layouts.json']:")
s=s.replace("'comic-edition-finished-alt-landscape-stress'","'comic-edition-verified-alt-landscape-stress'")
for state in ['weak','incomplete','combined','ambiguous']:
 s=s.replace("'comic-edition-finished-source-"+state+"-","'comic-edition-verified-source-"+state+"-")
s=s.replace('Four phone stress sizes use actual 1.3 text scale','The comic source page also passes 34 final state/size cases, including all original outcome states on desktop and the Android Back boundary. Possible/strong/weak actions and the source queued-state indicator were retested through real row entry, with focus and visibility restored.\\n\\nFour phone stress sizes use actual 1.3 text scale')
p.write_text(s,encoding='utf-8')
print('Assembly now uses final frozen runtime captures and verified source outcome evidence.')
