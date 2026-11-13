"""Generate data-only input cases, never expected oracle results.

Sources and hashes are documented in the standalone regex README.
"""
from pathlib import Path
import re,json,random,argparse
p=argparse.ArgumentParser(description="Generate Unicode16 regex differential samples from official UCD data; no oracle-derived answers.")
p.add_argument("--ucd",type=Path,required=True)
p.add_argument("--definitions",type=Path,required=True)
p.add_argument("--age17",type=Path,required=True)
p.add_argument("--output",type=Path,required=True)
args=p.parse_args()
u=args.ucd;cases=[]
def add(subject,pattern): cases.append([subject,pattern])
# Every assigned script's extremes and immediate exterior boundaries, plus each
# supported short/long/legacy alias. Data is sampled before consulting the engine.
scripts={}
for line in (u/'Scripts.txt').read_text().splitlines():
 line=line.split('#')[0].strip()
 if not line:continue
 interval,script=map(str.strip,line.split(';')); a,_,b=interval.partition('..');a=int(a,16);b=int(b or f'{a:X}',16)
 scripts.setdefault(script,[]).append((a,b))
for line in (u/'PropertyValueAliases.txt').read_text().splitlines():
 fields=[x.strip() for x in line.split('#')[0].split(';')]
 if fields[0]!='sc':continue
 name=fields[2];ranges=scripts.get(name,[(0x10ffff,0x10ffff)])
 for alias in fields[1:]:
  for cp in sorted({max(0,ranges[0][0]-1),ranges[0][0],ranges[0][1],ranges[-1][0],ranges[-1][1],min(0x10ffff,ranges[-1][1]+1)}):
   add(chr(cp),f'/\\p{{Script={alias}}}/u')
  add(chr(ranges[0][0]),f'/\\p{{scx={alias}}}/v')
# All binary/general-category long and short aliases, sampled at range edges
# and with deterministic independent code points.
defs=args.definitions.read_text()
block=defs[defs.index('/* Prop list exported to JS */'):defs.index('/* internal tables with index */')]
props=[]
for name,aliases in re.findall(r'DEF\((\w+),\s*"([^"]*)"\)',block):props += [name]+[a for a in aliases.split(',') if a]
block=defs[:defs.index('#ifdef UNICODE_SCRIPT')]
for name,aliases in re.findall(r'DEF\((\w+),\s*"([^"]*)"\)',block):props += [name]+[a for a in aliases.split(',') if a]
rng=random.Random(221600)
points=[0,0x41,0x61,0x390,0x1fd3,0x345,0x200d,0xd800,0x16ea0,0x1cc00,0x10ffff]+[rng.randrange(0x110000)for _ in range(8)]
for prop in props:
 for cp in points:add(chr(cp),f'/\\p{{{prop}}}/u')
 for cp in points[1:4]:add(chr(cp),f'/\\P{{{prop}}}/iv')
# Every Unicode17-added range, not just the original Beria Erfe example.
newpoints=set()
for line in args.age17.read_text().splitlines():
 fields=line.split('#')[0].strip().split(';')
 if len(fields)!=2 or fields[1].strip()!='17.0':continue
 a,_,b=fields[0].strip().partition('..');a=int(a,16);b=int(b or f'{a:X}',16)
 newpoints.update([max(0,a-1),a,b,min(0x10ffff,b+1)])
for cp in sorted(newpoints):
 for prop in ['Letter','Assigned','Cn','Alphabetic','ID_Start','Emoji','Extended_Pictographic']:
  add(chr(cp),f'/\\p{{{prop}}}/u')
for script in ['Beria_Erfe','Berf','Sidetic','Sidt','Tai_Yo','Tayo','Tolong_Siki','Tols']:
 for cp in [0x16ea0,0x10940,0x1e6c0,0x11db0]:
  for flags in ['u','v']:
   for prefix in ['Script','scx']:
    add(chr(cp),f'/\\p{{{prefix}={script}}}/{flags}')
# All simple Unicode16 case folding pairs and a nearby non-equivalent control.
for line in (u/'CaseFolding.txt').read_text().splitlines():
 fields=[s.strip()for s in line.split('#')[0].split(';')]
 if len(fields)<3 or fields[1] not in ['C','S']:continue
 cp=int(fields[0],16);fold=int(fields[2],16)
 for flags in ['iu','iv']:
  add(chr(fold),f'/\\u{{{cp:X}}}/{flags}')
  add(chr(cp),f'/[\\u{{{fold:X}}}]/{flags}')
# Parse rules, Unicode group-name escapes and valid neighbors; slash fallback
# is part of the source behavior and is not replaced by an arbitrary 'invalid'.
patterns=['(?<x>a)|(?<x>b)','(?<x>a)(?<x>b)','(?:(?<x>a)|(?<x>b))','(?<x>a)|(?<\\u0078>b)','(?<x>a)|(?<\\u{78}>b)','(?<x>a)|(?<y>b)','(?<x>a)\\k<x>','(?<a>a)(?<ab>b)','(?i:a)','(?m:^a$)','(?s:.)','(?i-m:a)','(?:a)','(?=a)','(?<=a)','(?<!a)','[()]','\\(\\?i:a\\)']
for p in patterns:
 for flags in ['', 'u','v']:
  for subject in ['a','b','ab','A','\n',f'/{p}/{flags}']:
   add(subject,f'/{p}/{flags}')
 for subject in ['a','A']:add(subject,p)
for cp in [0x16ea0,0x1e4d0,0x105c0,0x1c89,0x10ffff]:
 for flags in ['', 'u','v']:
  add('a',f'/(?<{chr(cp)}>a)/{flags}')
# Valid v-mode string properties, including exact Emoji16 additions.
for prop in ['Basic_Emoji','Emoji_Keycap_Sequence','RGI_Emoji_Modifier_Sequence','RGI_Emoji_Flag_Sequence','RGI_Emoji_Tag_Sequence','RGI_Emoji_ZWJ_Sequence','RGI_Emoji']:
 for subject in ['a','😀','1️⃣','👩🏽','🇺🇸','👨\u200d👩\u200d👦','\U0001fae9','\U0001fac8']:
  for flags in ['u','v']:add(subject,f'/\\p{{{prop}}}/{flags}')
cases=list(dict.fromkeys(map(tuple,cases)))
args.output.write_text(json.dumps(cases,ensure_ascii=True,separators=(',',':'))+'\n')
print(len(cases),'profile cases')
