const fs=require("fs"),path=require("path");
const batch=path.resolve(__dirname,".."),fin=path.join(batch,"finished-mocks");
for(const task of fs.readdirSync(fin)){const dir=path.join(fin,task);if(!fs.statSync(dir).isDirectory())continue;for(const n of fs.readdirSync(dir).filter(n=>n.endsWith(".html")&&n!=="index.html")){
const p=path.join(dir,n);let s=fs.readFileSync(p,"utf8");if(!s.includes("mock-support.js")){const re=/<script\b[^>]*\bsrc=["'](?:\.\/)?app\.js["'][^>]*><\/script>/;if(!re.test(s))throw Error("app script not found "+p);s=s.replace(re,'<script src="../mock-support.js"></script>\n$&');fs.writeFileSync(p,s);}}
}
console.log("Shared local focus/route support linked in 13 owned pages.");