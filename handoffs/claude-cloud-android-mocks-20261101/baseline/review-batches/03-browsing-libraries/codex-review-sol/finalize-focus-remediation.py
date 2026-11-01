from pathlib import Path
import json,runpy
p=Path(__file__).parent
runpy.run_path(str(p/'write-remediation.py'))
j=json.loads((p/'REMEDIATION.json').read_text(encoding='utf-8'));focus=json.loads((p/'focus-identity-after.json').read_text(encoding='utf-8'));before=json.loads((p/'focus-identity-before.json').read_text(encoding='utf-8'))
v=j['verification'];v.pop('checks');v['previousCandidateChecks']=72;v['previousCandidateSha256']='2b956240a6970ba6d020c3fe9e5567dbbe1c49313a791be66edf4c9115164044';v['latestCandidateChecks']=9;v['latestCandidateResults']='focus-identity-after.json';v['latestCandidateFailures']=[];v['latestCandidateViewport']=[1280,700];v['latestCandidateScope']='Minimize/Fullscreen/Close exact focus via pointer and keyboard across pending debounce; result focus; Clear focus; slow first-character/complete query/Enter smoke.'
j['initialFindings']['REM-F01']='fixed in latest candidate; stable action/value/aria-label identity distinguishes desktop noop controls'
j['changes'].append('Non-input focus identity includes aria-label in addition to action/value, so desktop Minimize/Fullscreen/Close do not collide.')
j['focusIdentityCorrection']={'reproduction':'At 1280x700 start a debounced query update, focus Fullscreen or Close via pointer or keyboard, then wait. Before correction focus moved to Minimize.','before':'focus-identity-before.json','beforeFailures':len([x for x in before['checks'] if not x['pass']]),'after':'focus-identity-after.json','afterChecks':9,'afterFailures':0,'screenshot':'focus-identity-after.png','implementation':'renderSearchUpdate action identity and match now include getAttribute(aria-label); exact existing control labels retained.','reasonForBoundedRetest':'Only non-input identity matching changed; 9 focused desktop cases and short query smoke cover the affected path. Earlier 72 lane/orientation checks remain evidence for the preceding candidate, not claimed rerun on the final hash.'}
j['cleanup']['previousRun']=dict(j['cleanup']);j['cleanup'].update({'origin':focus['origin'],'closedUtc':focus['closed'],'browserAndContextClosed':True,'loopbackServerClosed':True})
(p/'REMEDIATION.json').write_text(json.dumps(j,indent=2),encoding='utf-8')
md=(p/'REMEDIATION.md').read_text(encoding='utf-8')
md=md.replace('Final **72/72 checks passed**:', '**72/72 checks passed on the preceding candidate** (`2b956240…`):')
md=md.replace('Six final screenshots retain original presentation and typography.', 'Six screenshots of that preceding candidate retain original presentation and typography. The subsequent control-identity correction does not change rendered styling.')
md=md.replace('The earlier interaction result is retained under', 'That UTF-8 correction preceded the later control-identity fix. The earlier interaction result is retained under')
md=md.replace('Final page errors: zero;', 'Page errors in both focused runs: zero;')
md=md.replace('Final 72 checks and six screenshots were rerun.', 'The 72 checks and six screenshots were rerun on the UTF-8-corrected preceding candidate.')
md=md.replace('Owned browser/context and loopback server closed at ', 'Previous 72-case browser/context and loopback server closed at ')
md+='''
## Primary receiving correction: exact desktop control focus

**REM-F01 fixed in the latest candidate.** At 1280×700, both pointer and keyboard focus on Fullscreen or Close moved to Minimize after pending debounce: 4 failures reproduced in [before evidence](focus-identity-before.json). All three controls share `data-action=noop` and no value. The helper now matches `aria-label` as well as action/value, preserving the exact existing control.

**Latest candidate: 9/9 focused checks passed.** Minimize, Fullscreen and Close each retain exact identity with pointer and keyboard focus; result focus and Clear focus remain intentional; first-character pause, slow complete typing and Enter-followed-by-typing smoke pass. This bounded retest is [focus-identity-after.json](focus-identity-after.json), with [desktop screenshot](focus-identity-after.png). The earlier 72-case suite was not repeated after this two-expression identity refinement. Native navigation/focus boundaries above still apply.

Run `node test-focus-identity.cjs after` for this focused regression. Final candidate hashes above and in REMEDIATION.json supersede the prior candidate hash. Latest owned browser/context and loopback server closed at '''+focus['closed']+'''. Pro's 47 finished artifacts and 78 original-worker inputs remain hash-identical.
'''
(p/'REMEDIATION.md').write_text(md,encoding='utf-8')
print(json.dumps({'latestCandidateSha256':j['hashes']['candidate']['sha256'],'latestFocusedChecks':9,'failures':0,'closed':focus['closed']}))
