'use strict';
const fs=require('fs');const path=require('path');
const approved=path.resolve(__dirname,'../../reference/desktop/qml');
const mapping=[['GenreApi.js','OFFICIAL_MANGA_DESCS'],['BiblioGenreApi.js','OFFICIAL_BIBLIO_DESCS'],['TheatreGenreApi.js','OFFICIAL_THEATRE_DESCS']];
const out=[];
for(const [name,target] of mapping){ const raw=fs.readFileSync(path.join(approved,name),'utf8');const begin=raw.indexOf('var GENRE_DESC = {');if(begin<0)throw new Error('Missing source: '+name);const end=raw.indexOf('\n};',begin);if(end<0)throw new Error('Malformed source: '+name);const decl=raw.slice(begin,end+3).replace('var GENRE_DESC =','const '+target+' =');out.push('// Exact editorial standfirst from approved '+name+' (read-only)\n'+decl+'\n'); }
const app=path.join(__dirname,'app.js');fs.appendFileSync(app,'\n'+out.join('\n'),'utf8');console.log('Appended exact source GENRE_DESC objects from '+mapping.length+' approved read-only files');
