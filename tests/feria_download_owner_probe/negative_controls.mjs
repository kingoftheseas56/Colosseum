// Deliberately break one owner fence at a time in isolated temporary copies.
// Each variant must fail its named positive assertion; no product source edits.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const source = fs.readFileSync(new URL('../../qml/feria/FeriaBrowserDownloads.qml', import.meta.url),'utf8');
const testFile = new URL('./owner_model.test.mjs',import.meta.url).pathname;
const variants = [
    ['unsafe-source-getter','native admission never invokes',
        'var source = sourceForRequest(request)','var source = request.view'],
    ['duplicate-admission','duplicate delivery',
        'if ((key.length && seenNativeIds[key]) || items.some(function(item) { return item.request === request })) return','if (false) return'],
    ['early-byte-commit','completion signal coalesces', 'Qt.callLater(update)','update()'],
    ['consume-failed-grant','retryable write retains','metadataState = result.status','metadataState = result.status; releaseAuthorization()'],
    ['unknown-total-clamp','unknown byte total','totalBytes: request.totalBytes','totalBytes: Math.max(0, request.totalBytes)'],
    ['cross-profile-admission','wrong profile parent',
        '&& !policy.requestBelongsToProfile(request, profile)', '&& false'],
    ['action-replay','action operation replay',
        'if (actionResults[operationId])','if (false)'],
    ['drop-private-terminal-row','settled private row survives',
        'entry.detached = true', 'entry.detached = false'],
    ['ignore-settled-dependency','pending settled snapshot prevents',
        'if (pageSaveIntent || items.some(function(entry) { return entry.dependsOnProfile() })) return false','if (false) return false'],
    ['rebind-old-account-profile','account scope rebind',
        '|| profile.accountRoot !== policy.storageRoot', '|| false'],
    ['reuse-invalidated-page-slot','invalidated PageSave slot',
        'if (pageSaveIntent) {','if (pageSaveIntent && !pageSaveIntent.invalidated) {']
];
const directory=fs.mkdtempSync(path.join(os.tmpdir(),'feria-download-negative-'));
let failures=0;
try {
    for(const [id,assertion,from,to] of variants) {
        if(!source.includes(from))throw new Error(`Mutation anchor missing: ${id}`);
        const mutated=path.join(directory,id+'.qml');fs.writeFileSync(mutated,source.replace(from,to));
        const result=spawnSync(process.execPath,['--test','--test-name-pattern',assertion,testFile],
            {encoding:'utf8',env:{...process.env,FERIA_DOWNLOAD_OWNER:mutated}});
        const output=result.stdout+result.stderr;
        const detected=result.status!==0 && output.includes(assertion) && !output.includes('SyntaxError');
        console.log(JSON.stringify({negativeControl:id,assertion,detected,exitCode:result.status}));
        if(!detected){++failures;console.error(output);}
    }
} finally {fs.rmSync(directory,{recursive:true,force:true});}
if(failures)process.exit(1);
