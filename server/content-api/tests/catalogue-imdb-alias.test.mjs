import test from 'node:test';import assert from 'node:assert/strict';import {DatabaseSync}from'node:sqlite';
import {imdbMethod}from'../src/catalogue-semantic.ts';
const adapt=db=>({prepare(sql){return{bind(...args){return{async all(){return{success:true,results:db.prepare(sql).all(...args),meta:{}}}}}}}});
const titles="CREATE TABLE lib_imdb_title(tt TEXT,type TEXT,title TEXT,year INTEGER,endYear INTEGER,runtimeMin INTEGER,genres TEXT,rating REAL,votes INTEGER,episodes INTEGER,origLang TEXT,isAnime INTEGER,norm_title TEXT);"
 +"INSERT INTO lib_imdb_title VALUES('tt0807832','series','Mushi-Shi',2005,2006,25,'[]',8.6,50000,26,'ja',1,'mushi shi'),('tt0862946','movie','Mushi-Shi: The Movie',2006,0,131,'[]',7.5,9000,0,'ja',1,'mushi shi the movie'),('tt1','movie','Plain',2001,0,90,'[]',7,100,0,'en',0,'plain'),('tt2','movie','Other Plain',2001,0,90,'[]',7,100,0,'en',0,'other plain');";
const aliases="CREATE TABLE lib_imdb_title_alias(tt TEXT,norm_title TEXT,title TEXT,region TEXT,language TEXT,source TEXT);"
 +"INSERT INTO lib_imdb_title_alias VALUES('tt0807832','mushishi','Mushishi','','','title.basics.tsv:originalTitle'),('tt0807832','mushishi','Mushishi','JP','ja','title.akas.tsv'),('tt0862946','mushishi','Mushishi','','','title.basics.tsv:originalTitle'),('tt1','plain','Plain','US','','title.akas.tsv'),('tt1','alt plain','Alt Plain','GB','','title.akas.tsv');";
const open=(...sql)=>{const db=new DatabaseSync(':memory:');for(const s of sql)db.exec(s);return db;};
const tts=rows=>rows.map(r=>r.tt);
test('matchByTitle matches title_alias, one row per canonical tt, ordered like the exact query',async()=>{
 const db=adapt(open(titles,aliases));
 assert.deepEqual(tts(await imdbMethod(db,'matchByTitle',['mushishi',0])),['tt0862946','tt0807832']); // year DESC; dup alias rows collapse
 assert.deepEqual(tts(await imdbMethod(db,'matchByTitle',['mushishi',2005])),['tt0807832']);          // year filter applies to aliases
 assert.deepEqual(tts(await imdbMethod(db,'matchByTitle',['alt plain',0])),['tt1']);
 assert.deepEqual(tts(await imdbMethod(db,'matchByTitle',['plain',0])),['tt1']);                       // exact + alias on the same tt: not duplicated
 assert.deepEqual(await imdbMethod(db,'matchByTitle',['nothing',0]),[]);
 const row=(await imdbMethod(db,'matchByTitle',['mushishi',2005]))[0];assert.equal(row.title,'Mushi-Shi');assert.equal(row.synopsis,'');
});
test('matchByTitle keeps working when the alias table was not published',async()=>{
 const db=adapt(open(titles));
 assert.deepEqual(tts(await imdbMethod(db,'matchByTitle',['plain',0])),['tt1']);
 assert.deepEqual(await imdbMethod(db,'matchByTitle',['mushishi',0]),[]);
});
test('search is unchanged by aliases',async()=>{
 const db=adapt(open(titles,aliases));
 assert.deepEqual(tts(await imdbMethod(db,'search',['alt plain',20])),[]);
});
