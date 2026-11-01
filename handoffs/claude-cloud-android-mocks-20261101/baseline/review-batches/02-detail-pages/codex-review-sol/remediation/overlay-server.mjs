
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const own=path.dirname(fileURLToPath(import.meta.url));
const root=path.resolve(own,'../../../..');
const override='/review-batches/02-detail-pages/finished-mocks/17-theatre-details/app.js';
export function startOverlay(){const server=http.createServer((req,res)=>{try{const rel=decodeURIComponent(new URL(req.url,'http://localhost').pathname);let f=path.resolve(root,'.'+rel);if(!f.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}if(rel===override)f=path.join(own,'app.js');const bytes=fs.readFileSync(f);const ext=path.extname(f);res.writeHead(200,{'Content-Type':({'.js':'text/javascript','.html':'text/html; charset=utf-8','.css':'text/css','.ttf':'font/ttf','.otf':'font/otf','.svg':'image/svg+xml','.png':'image/png','.jpg':'image/jpeg'}[ext]||'application/octet-stream'),'Cache-Control':'no-store'});res.end(bytes);}catch{res.writeHead(404);res.end();}});return new Promise(resolve=>server.listen(0,'127.0.0.1',()=>resolve({server,origin:'http://127.0.0.1:'+server.address().port})));}
if(process.argv[1]===fileURLToPath(import.meta.url)){const {server,origin}=await startOverlay();console.log('Owned corrected overlay (Ctrl+C closes):');for(const page of ['film','series'])console.log(origin+'/review-batches/02-detail-pages/finished-mocks/17-theatre-details/'+page+'-detail.html?renderer=rebuilt');process.on('SIGINT',()=>server.close(()=>process.exit()));}
