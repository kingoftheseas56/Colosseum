"""Exact pinned M80 VM matching plus M172 slash/flags conversion, native-only candidate."""
import argparse,hashlib,json,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True);p.add_argument('--oracle',required=True);p.add_argument('--index',required=True);p.add_argument('--node',default='node');a=p.parse_args()
bundle=pathlib.Path(a.oracle).read_bytes();assert hashlib.sha256(bundle).hexdigest()=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f'
entry=next(m for m in json.loads(pathlib.Path(a.index).read_text())['modules'] if m['id']==80)
factory=bundle[entry['start_byte']:entry['end_byte']];assert hashlib.sha256(factory).hexdigest()==entry['sha256']
patterns=['movie','/movie/i','/^movie$/','/^movie$/m','/^movie/s','/./','/./s','/a*/g','/a*/gy','/b/y','/(a|(b))\\2/','/[\\s\\S]/','/[]/','/[^]/','/\\q/','/\\A/','/\\a/','/\\8/','/\\1/','/\\u0061/','/\\u{61}/u','/\\p{Letter}+/u','/\\p{Script=Greek}+/u','/k/iu','/k/i','/s/iu','/s/i','/(?<name>a)\\k<name>/','/(?<=a)b/','/(?<!a)b/','/a(?=b)/','/a(?!b)/','/a/ii','/a/uv','/a/z','/(/i','[','(?i)a','/a/d','/[a-z&&[^b]]/v','/[\\q{abc|def}]/v','/\\cA/','/\\x61/','/\\xQ/','/a$/','/^$/m']
patterns += ['/\ud800/', '/\ud800/u', '/😀/', '/😀/u', '/[\ud800]/']
subjects=['movie','MOVIE','movie\n','a','ab','b','aa','q','A','1','\x01','abc','def','a\nb','αβ','K','ſ','\u2028','\u00a0','😀','\ud800','']
cases=[[subject,pattern] for subject in subjects for pattern in patterns]
cases += [[chr(0x16ea0), '/\\p{Letter}/u'],[chr(0x16ea0), '/\\p{Script=Beria_Erfe}/u'],['a','/(?<x>a)|(?<x>b)/']]
cases += [['a'*30000+'!','/(a+)+$/'],['a'+'b'*30000+'!','/(?:a|(b+)+$)/g']]
script='const vm=require("vm"), module={exports:{}};('+factory.decode()+')(module,module.exports,id=>{if(id===665)return vm;throw Error(id)});console.log=()=>{};\n'
script+='for(const [s,input] of '+json.dumps(cases)+'){let re=input;const m=/^\\/(.*)\\/(.*)$/.exec(input);if(m){try{re=new RegExp(m[1],m[2])}catch(e){}}try{if(typeof re==="string")re=new RegExp(re);process.stdout.write(JSON.stringify({valid:true,matches:module.exports(s,re,500)})+"\\n")}catch(e){process.stdout.write(JSON.stringify({valid:false,matches:null})+"\\n")}}'
reference=subprocess.run([a.node,'-e',script],check=True,capture_output=True,text=True,timeout=30).stdout.splitlines()
actual=subprocess.run([a.candidate],input=''.join(json.dumps(c)+'\n'for c in cases),check=True,capture_output=True,text=True,timeout=30).stdout.splitlines()
assert len(reference)==len(actual)==len(cases)
errors=[]
for case,want,got in zip(cases,reference,actual):
 if json.loads(want)!=json.loads(got):errors.append((case,want,got))
assert not errors,errors[:15]
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
