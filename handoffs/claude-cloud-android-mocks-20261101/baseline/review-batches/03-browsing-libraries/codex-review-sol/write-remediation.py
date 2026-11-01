from pathlib import Path
import json,hashlib
b=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries');o=b/'codex-review-sol'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
tests=json.loads((o/'remediation-test-results.json').read_text(encoding='utf-8'))
focus=json.loads((o/'focus-identity-after.json').read_text(encoding='utf-8'))
beforeFocus=json.loads((o/'focus-identity-before.json').read_text(encoding='utf-8'))
proof=[]
for name in ['INPUT-VERSIONS-START.json','FINAL-ARTIFACT-VERSIONS.json']:
 j=json.loads((b/'evidence'/name).read_text(encoding='utf-8'));m=[]
 for x in j['files']:
  p=Path(x['path']);p=p if p.is_absolute() else b/p
  if sha(p)!=x['sha256']:m.append(str(p))
 proof.append({'manifest':name,'files':len(j['files']),'mismatches':m})
hashes=json.loads((o/'remediation-hashes-start.json').read_text(encoding='utf-8'))
for x in hashes.values():assert sha(Path(x['path']))==x['sha256']
hashes['patch']={'path':str(o/'search-correction.patch'),'sha256':sha(o/'search-correction.patch')}
hashes['driver']={'path':str(o/'test-search-correction.cjs'),'sha256':sha(o/'test-search-correction.cjs')}
hashes['focusDriver']={'path':str(o/'test-focus-identity.cjs'),'sha256':sha(o/'test-focus-identity.cjs')}
assert len(tests['checks'])==72 and not [x for x in tests['checks'] if not x['pass']]
assert not tests.get('fatal') and not tests['errors']
assert len(focus['checks'])==9 and all(x['pass'] for x in focus['checks']) and not focus['errors'] and not focus.get('fatal')
j={'status':'CORRECTED_CANDIDATE_READY_FOR_PRIMARY_REVIEW','initialFindings':{'SOL-B3-01':'fixed in candidate only; 6/6 lane/viewport pause and slow-typing checks pass','SOL-B3-02':'fixed in candidate only; 6/6 lane/viewport Enter intent/focus and continued-typing checks pass'},'hashes':hashes,'scope':'Only codex-review-sol candidate overlay; Pro original mocks/output/reports and initial receiving findings preserved. No other 15-page implementation changes.','runnableEntry':'candidate/search.html?capture=1&preset=portrait&lane=Biblio&state=empty','relativeBase':'Entry base points to ../../finished-mocks/20-search-browse/ for unchanged CSS/assets and batch runtime; script points back to complete candidate/app.js. Original batch directory must remain present.','changes':['Input value and clear visibility use S.query directly, independent of empty/results presentation.','Search rerender helper preserves selection start/end/direction and field focus only when that field was focused (or native-like Clear explicitly requests focus).','Pending debounce preserves intentionally focused existing result/control identity when still present and does not steal focus back from outside controls.','Clear cancels pending debounce and preserves cleared field focus.','Enter cancels debounce, renders matching results with appropriate fixture focus/caret and invokes the existing local top item-open action; no match emits no intent.','Initial inline-browse fixture explicitly clears query instead of relying on rendering to hide stored text.'],'nativeEnterEvidence':[{'file':'reference/desktop/qml/BiblioSearch.qml','lines':[190,202],'behavior':'Return/Enter stops debounce, searches, commits current query and opens top result.'},{'file':'reference/desktop/qml/SearchSurface.qml','lines':[211,218,280],'behavior':'Return/Enter stops debounce, searches and commits; local/no-provider route opens top. searchProvider may own activation.'}],'focusBoundary':'Retaining field focus after Enter is fixture-only because item-open stops at a status/intention boundary. Native title activation may navigate or hide Search; never force focus back into Search after real navigation. No native changes are proposed.','verification':{'browser':tests['browser'],'checks':len(tests['checks']),'failures':[],'lanes':['Biblio','Theatre','Tankoban'],'viewports':[[412,900],[900,412]],'casesPerCombination':12,'cases':['first-character pause','slow ordinary typing at 260 ms/key','fast replacement','Enter top-intent and selection retention','typing after Enter and Backspace','middle caret insertion/deletion','selection range retention','intentional result focus during debounce','intentional other-control focus during debounce','clear with pending work','Backspace through one character to empty','no-match Enter emits no action'],'syntax':'node --check candidate/app.js passed','screenshots':tests['captures'],'pageErrors':tests['errors'],'resourceFailures':tests['failedResources'],'results':'remediation-test-results.json','initialEncodingCorrection':'First local candidate used Python default decoding and displayed mojibake despite passing interactions. Explicit UTF-8 read/write corrected it; final 72-case suite and six captures rerun on corrected candidate. Earlier test record retained as remediation-test-results-before-utf8-correction.json.','regressionCommand':'node test-search-correction.cjs'},'preservedOriginalProof':proof,'cleanup':{'browserAndContextClosed':True,'loopbackServerClosed':True,'origin':tests['origin'],'closedUtc':tests['closed']},'nativeQualification':'Not performed; no device, QML/app repository, providers, persistence or actual title-navigation qualification. Final acceptance belongs to primary.'}
(o/'REMEDIATION.json').write_text(json.dumps(j,indent=2),encoding='utf-8')
md='''# Search candidate correction

**Corrected candidate ready for primary review.** SOL-B3-01 and SOL-B3-02 are fixed in the owned candidate only; the initial receiving report and Pro mocks remain unchanged.

Open [candidate/search.html](candidate/search.html?capture=1&preset=portrait&lane=Biblio&state=empty). Use `lane=Theatre` or `lane=Tankoban`; use `preset=landscape` for 900×412. The entry uses the original mock directory as its base for unchanged styles/assets/runtime and loads [the complete corrected app.js](candidate/app.js). Keep this folder within the existing batch. [Reviewable patch](search-correction.patch).

The input now always renders its query state; suggestions/results no longer erase one-character drafts. The focused rerender helper retains the field's caret/range, or preserves an intentionally selected existing result/control instead. Clear cancels pending work and returns focus as the source does. No CSS or visual design changes were made.

Frozen `BiblioSearch.qml:190,202` and `SearchSurface.qml:218,280` confirm Enter's search/commit/openTop intent. The candidate invokes the existing local `item-open` boundary for a matching top result; no match emits no action. **Field focus retention after Enter is fixture-only:** this demo reports an intent and stays on Search. Native navigation must follow its real owner and must not force focus back into a hidden Search field.

## Focused verification

Final **72/72 checks passed**: 12 cases × Biblio/Theatre/Tankoban × portrait/landscape. Actual browser keyboard/pointer events cover slow and fast typing, first-character pause, Enter then further typing, Backspace/clear, middle-of-query caret edits, selection-range retention, intentional focus moves during pending debounce, and no-match submission. Six final screenshots retain original presentation and typography. [Raw results and focus/resource observations](remediation-test-results.json), [contact sheet](corrected-search-contact-sheet.jpg).

Run the bounded regression with `node test-search-correction.cjs` from this folder. It serves only the existing local artifact tree, blocks external requests, owns an isolated headless browser/context and closes both browser and server. It does not edit mocks. `node --check candidate/app.js` also passed. Final page errors: zero; required-resource failures: zero (the implicit favicon 404 is separately recorded).

Visual review caught a UTF-8 copying error in the first local candidate. The copy step was corrected to explicit UTF-8; the final 72 checks and six screenshots were rerun. The earlier interaction result is retained under `remediation-test-results-before-utf8-correction.json` and is not final acceptance evidence.

## Exact versions and preservation

'''
for label,x in hashes.items():md+='- '+label+': `'+x['sha256']+'`\n'
md+='''
All 47 Pro finished-artifact hashes and 78 original-worker input hashes still match their recorded manifests. This includes the unchanged original Search JS. Candidate, entry, patch and driver hashes plus exact paths are in [REMEDIATION.json](REMEDIATION.json).

Owned browser/context and loopback server closed at '''+tests['closed']+'''. No source QML, application repository, other page, Git, device, account, provider or publication changes. Native metrics, keyboard/IME, actual navigation and service behavior remain unqualified; primary owns acceptance.
'''
(o/'REMEDIATION.md').write_text(md,encoding='utf-8')
print(json.dumps({'status':j['status'],'checks':len(tests['checks']),'hashes':hashes,'preservation':proof}))
