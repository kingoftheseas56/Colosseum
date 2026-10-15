// Only successful immutable snapshot attestations are retained. No request, I/O
// promise, credential or user state crosses invocation boundaries. Replacement
// bindings/revisions are new snapshots and must pass their own first check.
const checked = new WeakMap<D1Database, Set<string>>();
export async function libraryReady(db:D1Database,revision:string,observe?:(meta:D1Meta)=>void):Promise<boolean>{
  if(checked.get(db)?.has(revision))return true;
  const result=await db.prepare('SELECT revision,ready FROM _library_state WHERE id=1').all<{revision:string;ready:number}>();
  observe?.(result.meta);const state=result.results[0];
  if(!state||state.ready!==1||state.revision!==revision)return false;
  const revisions=checked.get(db)??new Set<string>();revisions.add(revision);checked.set(db,revisions);
  return true;
}
