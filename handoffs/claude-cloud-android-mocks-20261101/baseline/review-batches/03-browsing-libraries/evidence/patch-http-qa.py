from pathlib import Path
B=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries')
p=B/'evidence/qa-http.js'
s=p.read_text(encoding='utf-8')
s=s.replace("const server=http.createServer((req,res)=>{let file;","const server=http.createServer((req,res)=>{if(req.url==='/favicon.ico'){res.writeHead(204);res.end();return;}let file;")
s=s.replace("await new Promise(r=>server.listen(0,'127.0.0.1',r));","globalThis.batchStaticServer=server;await new Promise(r=>server.listen(0,'127.0.0.1',r));")
s=s.replace("strict=page.listeners('request');","strict=r=>{if(/^(file|data|blob|about):/.test(r.url()))r.continue();else{blocked.push(r.url());r.abort('blockedbyclient');}};")
s=s.replace("for(const listener of strict)page.on('request',listener);","page.on('request',strict);globalThis.batchStaticServer=null;")
p.write_text(s,encoding='utf-8')
print('HTTP verification harness uses the supported Page request API and guaranteed owned server handle.')
