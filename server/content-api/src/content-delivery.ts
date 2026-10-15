// Static content is versioned with the Worker deployment. No code or database
// files can be supplied by the request; the application only receives JSON data.
export async function contentDelivery(request: Request, assets: Fetcher): Promise<Response | null> {
  const url = new URL(request.url);
  let target = '';
  if (request.method === 'POST' && url.pathname === '/v1/catalogue/content/bundle') {
    target = '/internal/content/rpc-bundle.json';
  } else if (request.method === 'GET' && url.pathname === '/v1/content/manifest') {
    target = '/internal/content/manifest.json';
  } else if (request.method === 'GET') {
    const match = url.pathname.match(/^\/v1\/content\/documents\/([a-z][a-z0-9-]{0,100})$/);
    if (match) target = '/internal/content/documents/' + match[1] + '.json';
  }
  if (!target) return null;
  const response = await assets.fetch(new Request(new URL(target, url.origin), {
    headers: request.headers.has('If-None-Match') ? {'If-None-Match': request.headers.get('If-None-Match')!} : {},
  }));
  if (response.status !== 200 && response.status !== 304)
    return Response.json({error: 'CONTENT_NOT_AVAILABLE'}, {status: 503});
  const headers = new Headers(response.headers);
  headers.set('Content-Type', 'application/json; charset=utf-8');
  headers.set('X-Content-Type-Options', 'nosniff');
  headers.set('Cache-Control', 'private, max-age=300');
  return new Response(response.body, {status: response.status, headers});
}
