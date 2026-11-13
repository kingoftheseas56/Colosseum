"""Exact pinned M80 VM matching plus M172 slash/flags conversion, native-only candidate."""
import argparse,hashlib,json,pathlib,subprocess,random
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True);p.add_argument('--oracle',required=True);p.add_argument('--index',required=True);p.add_argument('--node',default='node');p.add_argument('--diagnostic-runtime',action='store_true');p.add_argument('--grammar-fuzz',action='store_true',help='Compatibility alias: bounded grammar and class-set tests now always run');a=p.parse_args()
profile=json.loads(subprocess.run([a.node,'-p','JSON.stringify(process.versions)'],check=True,capture_output=True,text=True).stdout)
if not a.diagnostic_runtime:
    assert (profile['node'],profile['unicode'],profile['v8'])==('22.16.0','16.0','12.4.254.21-node.26'), 'Pinned regex qualification requires Node 22.16.0 / Unicode 16.0; use --diagnostic-runtime only for a separate non-qualifying comparison'
print('Regex oracle runtime:',profile['node'],'Unicode',profile['unicode'], 'DIAGNOSTIC' if a.diagnostic_runtime else 'PINNED',flush=True)
bundle=pathlib.Path(a.oracle).read_bytes();assert hashlib.sha256(bundle).hexdigest()=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f'
entry=next(m for m in json.loads(pathlib.Path(a.index).read_text())['modules'] if m['id']==80)
factory=bundle[entry['start_byte']:entry['end_byte']];assert hashlib.sha256(factory).hexdigest()==entry['sha256']
patterns=['movie','/movie/i','/^movie$/','/^movie$/m','/^movie/s','/./','/./s','/a*/g','/a*/gy','/b/y','/(a|(b))\\2/','/[\\s\\S]/','/[]/','/[^]/','/\\q/','/\\A/','/\\a/','/\\8/','/\\1/','/\\u0061/','/\\u{61}/u','/\\p{Letter}+/u','/\\p{Script=Greek}+/u','/k/iu','/k/i','/s/iu','/s/i','/(?<name>a)\\k<name>/','/(?<=a)b/','/(?<!a)b/','/a(?=b)/','/a(?!b)/','/a/ii','/a/uv','/a/z','/(/i','[','(?i)a','/a/d','/[a-z&&[^b]]/v','/[\\q{abc|def}]/v','/\\cA/','/\\x61/','/\\xQ/','/a$/','/^$/m']
patterns += ['/\ud800/', '/\ud800/u', '/😀/', '/😀/u', '/[\ud800]/']
subjects=['movie','MOVIE','movie\n','a','ab','b','aa','q','A','1','\x01','abc','def','a\nb','αβ','K','ſ','\u2028','\u00a0','😀','\ud800','']
cases=[[subject,pattern] for subject in subjects for pattern in patterns]
cases += [[chr(0x16ea0), '/\\p{Letter}/u'],[chr(0x16ea0), '/\\p{Script=Beria_Erfe}/u'],['a','/(?<x>a)|(?<x>b)/']]
cases += json.loads((pathlib.Path(__file__).parent/'fixtures/safe-file-regex-runtime.json').read_text())
# Repetition registers occupy execution slots beyond 2*capture_count.
cases += [['\n', '/\\w{1,2}b/gv'], ['aab', '/a{1,2}b/'], ['aaaa', '/(?:a{2,3}){1,2}/g'], ['aab', '/(?<x>a){1,2}b/u']]

# Deterministic grammar fuzz spans valid syntax, parser rejections, escaping,
# Unicode properties and M172's literal fallback, without unbounded subjects.
# Pinned Node22 regression: V8 folds the one-character q{}
# alternative but does not close its character range under /iv.
cases.append(['A ', '/[\\q{ab|a}]{1,2}(?<=a)/iv'])
set_atoms=['[a]','[A]','[a-z]','[A-Z]','[\\q{a}]','[\\q{A}]','[\\q{ab|a}]','[\\q{AB|A}]','[\\q{k}]','[\\q{K}]','[\\q{ſ}]','[\\q{ﬅ}]','[\\q{ﬆ}]','[\\p{ASCII}]','[\\P{ASCII}]','[\\p{Lowercase_Letter}]','[\\w]','[\\W]']
set_subjects=['a','A','ab','Ab','AB','z','Z','k','K','K','s','S','ſ','ﬅ','ﬆ','ß','ẞ','1','😀','']
for atom in set_atoms:
    for flags in ['v','iv']:
        for subject in set_subjects:
            cases.append([subject,'/'+atom+'/'+flags])
    for rhs in ['[a]','[A]','[\\q{a}]','[\\q{A}]','[a-z]','[\\p{ASCII}]']:
        for op in ['&&','--','']:
            for subject in set_subjects:
                cases.append([subject,'/['+atom+op+rhs+']/iv'])
rng=random.Random(221600)
atoms=['a','b','.','\\w','\\d','\\s','[a-z]','[^a]','[\\q{ab|a}]','\\p{Letter}','\\P{Lowercase_Letter}','\\u{16EA0}','(?<x>a)','(?<y>b)','(?:a|b)','(?=a)','(?<=a)','(?i:a)']
for _ in range(2048):
    left,right=rng.choices(atoms,k=2)
    pattern=rng.choice([left+right,'(?:'+left+'|'+right+')',left+rng.choice(['?','+','{1,2}'])+right,'^'+left+right+'$'])
    flags=rng.choice(['','u','v','i','iu','iv','g','gu','gv','y'])
    subject=''.join(rng.choices('abA01 \nα😀',k=rng.randrange(9)))
    cases.append([subject,'/'+pattern+'/'+flags])

cases += [['a'*30000+'!','/(a+)+$/'],['a'+'b'*30000+'!','/(?:a|(b+)+$)/g']]
script='const vm=require("vm"), module={exports:{}};('+factory.decode()+')(module,module.exports,id=>{if(id===665)return vm;throw Error(id)});console.log=()=>{};\n'
script+='for(const [s,input] of JSON.parse(require("fs").readFileSync(0,"utf8"))){let re=input;const m=/^\\/(.*)\\/(.*)$/.exec(input);if(m){try{re=new RegExp(m[1],m[2])}catch(e){}}try{if(typeof re==="string")re=new RegExp(re);process.stdout.write(JSON.stringify({valid:true,matches:module.exports(s,re,500)})+"\\n")}catch(e){process.stdout.write(JSON.stringify({valid:false,matches:null})+"\\n")}}'
reference=subprocess.run([a.node,'-e',script],check=True,input=json.dumps(cases),capture_output=True,text=True,timeout=30).stdout.splitlines()
actual=subprocess.run([a.candidate],input=''.join(json.dumps(c)+'\n'for c in cases),check=True,capture_output=True,text=True,timeout=30).stdout.splitlines()
assert len(reference)==len(actual)==len(cases)
errors=[]
for case,want,got in zip(cases,reference,actual):
 if json.loads(want)!=json.loads(got):errors.append((case,want,got))
assert not errors,{'mismatchCount':len(errors),'firstMismatches':errors[:15]}
print(f'PASS {len(cases)} exact M172/M80 ECMAScript regex cases including interrupted global and catastrophic matches')
# Execute the exact nested Array.find expression, including falsy original
# elements. Empty-string predicates mutate the index but do not stop outer find.
selection_cases=[[['one','two','three'],patterns] for patterns in [[],[''],['','two'],['two',''],['/two/i'],['nomatch'],['['],['/(/i']]]
text=bundle.decode();start=text.index('engine.files.find((function(file, idx) {',text.index('function handleTorrent'))
end=text.index('})), isNaN(i)',start)+3
expression=text[start:end]
selection_script='const vm=require("vm"), module={exports:{}};('+factory.decode()+')(module,module.exports,id=>vm);const safeStatelessRegex=module.exports;console.log=()=>{};\n'
selection_script+='for(const [names,input] of '+json.dumps(selection_cases)+'){let i=null;const engine={files:names.map(name=>({name}))};const fileMustInclude=input.map(el=>{const m=/^\\/(.*)\\/(.*)$/.exec(el);if(m){try{return new RegExp(m[1],m[2])}catch(e){}}return el;});try{'+expression+';process.stdout.write(JSON.stringify({selected:i})+"\\n")}catch(e){process.stdout.write(JSON.stringify({invalid:true})+"\\n")}}'
expected=subprocess.run([a.node,'-e',selection_script],check=True,capture_output=True,text=True).stdout.splitlines()
actual=subprocess.run([a.candidate],input=''.join(json.dumps(c)+'\n'for c in selection_cases),check=True,capture_output=True,text=True).stdout.splitlines()
assert len(actual)==len(expected)==len(selection_cases)
assert [json.loads(s) for s in actual]==[json.loads(s) for s in expected],(expected,actual)
print(f'PASS {len(selection_cases)} exact nested file-selection cases')
