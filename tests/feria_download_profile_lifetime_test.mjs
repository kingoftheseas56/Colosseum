// Modeled owner-policy regression: evaluates production deck functions, not engine behavior.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';
const source = readFileSync(new URL('../qml/feria/FeriaSessionDeck.qml', import.meta.url), 'utf8');
function body(name) {
  const start = source.indexOf('function '+name+'(');
  assert(start >= 0);
  const open = source.indexOf('{', start);
  let depth=1, end=open+1;
  for (; depth && end < source.length; ++end) {
    if(source[end]==='{') ++depth;
    if(source[end]==='}') --depth;
  }
  return source.slice(start,end);
}
function fixture({transfers=false, views=0, newPrivateSpace=false, quiescing=false}={}) {
  const calls=[];
  const ctx={quiescing,privateProfileRetiring:true,
    privateProfile:{destroy(){calls.push('destroy');}},
    policy:{runtimeViewsForProfile(){return views;}},
    sessions:{browsers(){return newPrivateSpace?[{target:{privateBrowsing:true}}]:[];}},
    privateDownloads:{hasTransferDependencies:transfers,cancelActive(){calls.push('cancel');},detachCompletedProfile(){calls.push('detach');return true;}},
  };
  runInNewContext(body('releasePrivateProfile')+'; releasePrivateProfile()',ctx);
  return {calls,ctx};
}
assert.deepEqual(fixture({transfers:true}).calls,[], 'last private view must not cancel or destroy an active profile transfer');
assert.deepEqual(fixture({views:1}).calls,[], 'live native view retains profile');
assert.deepEqual(fixture({quiescing:true}).calls,[], 'account retirement uses separate quiescence owner');
assert.deepEqual(fixture({newPrivateSpace:true}).calls,[], 'new private space cancels pending idle-profile retirement');
assert.equal(fixture({newPrivateSpace:true}).ctx.privateProfileRetiring,false);
assert.deepEqual(fixture().calls,['detach','destroy'], 'idle dependency-free private profile retires without canceling completed files');
console.log('PASS modeled production deck private transfer lifetime, new space and account teardown separation');
for (const [ready,quiescing,accountRoot,expected] of [
  [true,false,'new',2], [false,false,'new',0], [true,true,'new',0], [true,false,'old',0]
]) {
  let calls=0;
  const ctx={ready,quiescing,sharedProfile:{accountRoot},privateProfile:{accountRoot},
    policy:{storageRoot:'new'},regularDownloads:{rebindAccountScope(){++calls;}},
    privateDownloads:{rebindAccountScope(){++calls;}}};
  runInNewContext(body('rebindDownloadScopes')+'; rebindDownloadScopes()',ctx);
  assert.equal(calls,expected,'rebind only after native account scope settles on matching non-retiring profiles');
}
console.log('PASS modeled canonical account binder ordering and old-profile reactivation fences');
