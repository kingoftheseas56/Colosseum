// Executes functions extracted from production QML, with explicit fake native objects.
// This is source-extracted JS evidence, not a Windows app / mpv / Lanista test.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
function extract(path,name) {
 const text=fs.readFileSync(path,'utf8');const start=text.indexOf('function '+name+'(');assert(start>=0,name);
 const brace=text.indexOf('{',start);let depth=0,quote='',escape=false,line=false,block=false;
 for(let i=brace;i<text.length;i++) {
  const c=text[i],n=text[i+1];
  if(line){if(c==='\n')line=false;continue;}if(block){if(c==='*'&&n==='/'){block=false;i++;}continue;}
  if(quote){if(escape)escape=false;else if(c==='\\')escape=true;else if(c===quote)quote='';continue;}
  if(c==='/'&&n==='/'){line=true;i++;continue;}if(c==='/'&&n==='*'){block=true;i++;continue;}
  if(c==='"'||c==="'"){quote=c;continue;}if(c==='{')depth++;if(c==='}'&&!--depth)return text.slice(start,i+1);
 }
 throw new Error('unterminated '+name);
}
let passed=0;
function test(name,fn){fn();console.log('PASS '+name);passed++;}
const logic=vm.createContext({});vm.runInContext(fs.readFileSync('qml/AudiobookSessionLogic.js','utf8').replace(/^\.pragma library\s*$/m,''),logic);
const clock={now:1000};const mpv={position:20,duration:100,speed:1,pause:false,volume:75,chapters:[]};
const session={ready:true,pendingResumeSec:-1,multiFile:true,currentIndex:0,chapterModel:[{time:0},{time:0}],sleepMode:'off',sleepDeadlineMs:0,sleepVolumeFactor:1,sleepStoppedAtBoundary:false,sleepNeedsChapterAnchor:false,recordProgress(){this.records=(this.records||0)+1},playIndex(i){this.currentIndex=i;this.advances=(this.advances||0)+1},activityNaturalEof(){this.completed=true}};
const ctx=vm.createContext({session,mpv,SessionLogic:logic,Date:{now:()=>clock.now},Number,Math});
for(const name of ['playingChapterIndex','setSleepMode','finishSleep','tickSleep']) {vm.runInContext(extract('qml/AudiobookSession.qml',name),ctx);session[name]=(...args)=>ctx[name](...args);}
vm.runInContext(extract('qml/AudiobookSession.qml','onEndFile'),ctx);
vm.runInContext(extract('qml/AudiobookSession.qml','onPositionChanged'),ctx);
test('timed fade, pause and restore factor',()=>{session.setSleepMode(15);clock.now=896000;session.tickSleep();assert.equal(session.sleepVolumeFactor,0.5);clock.now=901000;session.tickSleep();assert.equal(mpv.pause,true);assert.equal(session.sleepVolumeFactor,1);assert.equal(session.sleepMode,'off');});
test('chapter EOF does not advance',()=>{mpv.pause=false;session.files=['a','b'];session.setSleepMode('chapterEnd');ctx.onEndFile('eof');assert.equal(session.currentIndex,0);assert.equal(session.advances||0,0);assert.equal(mpv.pause,true);});
test('queued EOF after position boundary does not advance',()=>{mpv.pause=false;session.setSleepMode('chapterEnd');session.finishSleep();ctx.onEndFile('eof');assert.equal(session.advances||0,0);});
test('ordinary EOF still advances',()=>{session.sleepStoppedAtBoundary=false;session.sleepMode='off';ctx.onEndFile('eof');assert.equal(session.currentIndex,1);});
test('embedded chapter transition pauses',()=>{session.multiFile=false;session.chapterModel=[{time:0},{time:30}];mpv.position=20;mpv.pause=false;session.setSleepMode('chapterEnd');mpv.position=30;ctx.onPositionChanged();assert.equal(mpv.pause,true);});
let saved={},writes=0,lookups=[];
const shell={bookPath:'a.epub',bookId:'a',bookReady:true,personalStateSealed:false,currentGen:2,audioLookupGeneration:4,audioOffer:{offerState:'available'},audioOfferChipVisible:false,audioLookupKey:'book|author',audioLookupTitle:'Book',audioLookupAuthor:'Author',audioAttached:false};
const reader=vm.createContext({shell,chrome:{awake:true},Reader2Bridge:{personalStateGeneration:3,settingsGet:()=>JSON.parse(JSON.stringify(saved)),settingsSaveForGeneration:(value,gen)=>{assert.equal(gen,3);saved=value;writes++;}},Abb:{resolveAudiobook:(title,author,done)=>lookups.push(done)},Number});
for(const name of ['audioOfferContext','ownsAudioOffer','maybeShowAudioChip','lookupAudioOffer']) {vm.runInContext(extract('qml/reader2/ReaderShell.qml',name),reader);shell[name]=(...args)=>reader[name](...args);}
test('chip is persisted once per book',()=>{shell.maybeShowAudioChip();assert.equal(shell.audioOfferChipVisible,true);assert.equal(writes,1);shell.audioOfferChipVisible=false;shell.maybeShowAudioChip();assert.equal(shell.audioOfferChipVisible,false);assert.equal(writes,1);});
test('stale lookup cannot overwrite new route',()=>{shell.lookupAudioOffer();const old=shell.audioLookup;shell.currentGen++;lookups.pop()({rows:[{title:'wrong'}],error:false});assert.equal(shell.audioLookup,old);});
test('profile generation fences lookup',()=>{shell.lookupAudioOffer();const old=shell.audioLookup;reader.Reader2Bridge.personalStateGeneration++;lookups.pop()({rows:[],error:true});assert.equal(shell.audioLookup,old);});
test('current lookup failure stays unknown',()=>{shell.lookupAudioOffer();lookups.pop()({rows:[],error:true});assert.equal(shell.audioLookup.error,true);assert.equal(shell.audioLookup.rows.length,0);});

vm.runInContext(extract('qml/AudiobookSession.qml','openFor'),ctx);
ctx.Audiobooks={localFiles:()=>['a.mp3','b.mp3']};ctx.Progress={get:()=>({resume:{fileIndex:1,position:12,speed:1.5}})};
session.rebuildChapters=()=>{};session.activityBeginIfNeeded=()=>{};mpv.loadFile=path=>{mpv.loaded=path};
test('restored speed and pending seek remain book-owned',()=>{session.ready=false;ctx.openFor('one',{title:'One'},true);assert.equal(mpv.speed,1.5);assert.equal(mpv.loaded,'b.mp3');assert.equal(session.pendingResumeSec,12);assert.equal(mpv.pause,true);ctx.Progress.get=()=>({});ctx.openFor('two',{title:'Two'},true);assert.equal(mpv.speed,1);});
vm.runInContext(extract('qml/AudiobookSession.qml','recordProgress'),ctx);
test('resume writes speed without losing position',()=>{let record;ctx.Progress.record=value=>record=value;session.overallProgress=()=>0.2;mpv.speed=1.5;mpv.position=12;session.pendingResumeSec=-1;ctx.recordProgress();assert.equal(record.resume.speed,1.5);assert.equal(record.resume.position,12);assert.equal(record.resume.pairKey,'two');});
let started=0,confirmations=0,hashReply;
const detail={sourceGeneration:1,pairKey:'book|author',book:{title:'Book',author:'Author'},localPath:'book.epub',audioLocal:true,abEntry:{releaseSlug:'old'},abStatus:{state:'done'},abHashResolving:false,collectionRef:null,refreshAudioState(){},refreshLocal(){},_inFlight:s=>['resolving','downloading','queued'].includes(s)};
const page=vm.createContext({detail,Audiobooks:{downloadAudiobook(){started++},replaceAudiobook(){started++}},Reader2Bridge:{bookKey:()=> 'book-id'},replaceAudioFocus:{forceActiveFocus(){confirmations++}},Qt:{PopupFocusReason:1},abCol:{},Abb:{fetchInfoHash:(slug,done)=>hashReply=done}});
for(const name of ['activateAudiobookRow','startAudiobookRow','listenToBook']){vm.runInContext(extract('qml/BiblioBook.qml',name),page);detail[name]=(...args)=>page[name](...args);}
test('replacement never starts before confirmation',()=>{detail.activateAudiobookRow({slug:'new'});assert.equal(confirmations,1);assert.equal(started,0);assert.equal(detail.abReplaceIndex,0);assert.equal(detail.abReplaceOpen,true);});
test('superseded hash cannot acquire for the new book',()=>{detail.audioLocal=false;detail.abStatus={state:'none'};detail.startAudiobookRow({slug:'new'},false);detail.sourceGeneration++;hashReply({infoHash:'a'.repeat(40)});assert.equal(started,0);});
test('Listen routes through the reader with a metadata copy',()=>{let open;detail.readRequested=(path,book)=>open={path,book};detail.listenToBook();assert.equal(open.path,'book.epub');assert.equal(open.book.openAudio,true);assert.equal(open.book.pairKey,'book|author');assert.equal(detail.book.openAudio,undefined);});

console.log('PASS '+passed+' production-source wiring checks');

for (const name of ['prepareAttachedAudio','restoreAudioSpeedChoice','ensureAudioLoaded','audioCycleSpeed','openAudioPanel']) {
 vm.runInContext(extract('qml/reader2/ReaderShell.qml',name),reader);shell[name]=(...args)=>reader[name](...args);
}
reader.SessionLogic=logic;reader.Audiobooks={isDownloaded:()=>true};reader.Progress={get:()=>({resume:{speed:1.5}})};
reader.chrome.openPanelTo=()=>{};shell.audioSessionBook=()=>({title:'B'});
shell.audioAttached=true;shell.audioPairKey='B';shell.audioSpeeds=[1,1.25,1.5,1.75,2,0.75];
let loads=0;
shell.audioSession={ready:true,activePairKey:'A',paused:false,speed:1.25,
 openFor(pk,book,paused){loads++;this.activePairKey=pk;this.paused=paused;this.speed=1.5;this.ready=true},
 setRate(speed){this.speed=speed}};
Object.defineProperty(shell,'audioSessionLive',{get(){return shell.audioSession.ready&&shell.audioSession.activePairKey===shell.audioPairKey}});
test('book A playing, open paired book B: A keeps playing',()=>{
 shell.restoreAudioSpeedChoice();assert.equal(shell.prepareAttachedAudio(),false);shell.openAudioPanel();
 assert.equal(loads,0);assert.equal(shell.audioSession.activePairKey,'A');assert.equal(shell.audioSession.paused,false);
 const source=fs.readFileSync('qml/reader2/ReaderShell.qml','utf8');
 assert(source.includes('shell.restoreAudioSpeedChoice()\n                if (shell.audioAttached) shell.prepareAttachedAudio()'));
});
test('speed before play does not load over A; explicit play applies B choice',()=>{
 shell.audioCycleSpeed();assert.equal(shell.audioChosenSpeed,1.75);assert.equal(loads,0);assert.equal(shell.audioSession.speed,1.25);
 shell.ensureAudioLoaded(false);assert.equal(loads,1);assert.equal(shell.audioSession.activePairKey,'B');assert.equal(shell.audioSession.speed,1.75);assert.equal(shell.audioSession.paused,false);
});
test('idle preload and same-book speed remain available',()=>{
 shell.audioSession.ready=false;shell.restoreAudioSpeedChoice();assert.equal(shell.prepareAttachedAudio(),true);assert.equal(shell.audioSession.paused,true);
 shell.audioCycleSpeed();assert.equal(shell.audioSession.speed,1.75);
});
vm.runInContext(extract('qml/BiblioBook.qml','finishAudiobookReplace'),page);
page.audiobookList={forceActiveFocus(){}};page.Qt={PopupFocusReason:1};
test('replacement No cancels; Yes starts exactly once',()=>{
 let replaces=0;detail.startAudiobookRow=(row,replace)=>{assert(replace);replaces++};
 detail.abReplaceOpen=true;detail.abReplaceRow={slug:'new'};page.finishAudiobookReplace(false);assert.equal(replaces,0);
 detail.abReplaceOpen=true;detail.abReplaceRow={slug:'new'};page.finishAudiobookReplace(true);page.finishAudiobookReplace(true);assert.equal(replaces,1);
});
console.log('PASS '+passed+' total production-source wiring checks (v2)');

// ── v2 defect 1: the book page's downloaded flag must be DERIVED from pairKey, not captured once ──
vm.runInContext(extract('qml/BiblioBook.qml','audioIsDownloaded'),page);
vm.runInContext(extract('qml/BiblioBook.qml','refreshAudioState'),page);
const biblioSource=fs.readFileSync('qml/BiblioBook.qml','utf8');
test('audioLocal is a binding on pairKey + revision, never assigned',()=>{
 assert(biblioSource.includes('property bool audioLocal: (detail.abRevision, detail.audioIsDownloaded())'));
 assert(!/detail\.audioLocal\s*=[^=]/.test(biblioSource),'imperative assignment would break the binding');
});
test('late-settling pairKey still reports the downloaded audiobook',()=>{
 page.Audiobooks={isDownloaded:k=>k==='late|key'};
 detail.pairKey='stale|key';assert.equal(page.audioIsDownloaded(),false);
 detail.pairKey='late|key';assert.equal(page.audioIsDownloaded(),true);
 detail.abRevision=0;page.refreshAudioState();assert.equal(detail.abRevision,1);
});
console.log('PASS '+passed+' total production-source wiring checks (v3)');
