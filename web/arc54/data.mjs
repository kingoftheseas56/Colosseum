export const CLIENT_VERSION='arc54-1';
export class DataError extends Error {
  constructor(code,status=0){super(code);this.name='DataError';this.code=code;this.status=status;}
}
export function createHosted(fetcher=fetch,origin=location.origin) {
  const base=new URL('/api/v1/',origin);
  async function get(path,signal) {
    let response;
    try {response=await fetcher(new URL(path,base),{headers:{Accept:'application/json'},signal,cache:'no-store'});}
    catch(e){if(e.name==='AbortError')throw e;throw new DataError('HOST_UNREACHABLE');}
    let data={};try {data=await response.json();}catch(e){if(e.name==='AbortError')throw e;}
    if(!response.ok)throw new DataError(data?.error||'HOST_NOT_CONFIGURED',response.status);
    if(data?.schema!==1)throw new DataError('INCOMPATIBLE_HOST',response.status);
    return data;
  }
  return {
    capabilities: signal=>get('capabilities',signal),
    browse:(tab,signal)=>get('theatre/browse?tab='+encodeURIComponent(tab),signal),
    detail:(id,signal)=>get('theatre/title/'+encodeURIComponent(id),signal)
  };
}
export function errorCopy(e) {
  if(e.code==='TITLE_NOT_FOUND')return 'Title not found.';
  if(e.code==='TAB_UNAVAILABLE')return 'This tab needs a compatible host.';
  if(e.code==='INCOMPATIBLE_HOST')return 'This host speaks a different API version.';
  if(e.code?.startsWith('UPSTREAM'))return 'The catalogue service couldn’t answer. Try again.';
  return 'No compatible catalogue host answered. Check the connection, then retry.';
}
