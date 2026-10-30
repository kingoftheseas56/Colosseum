import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

// Run the actual pure adapters and named-collection owner methods with bounded
// provider doubles. The native canonicalizer has its own QtCore test target.
const qml = name => fs.readFileSync(new URL(`../qml/${name}`, import.meta.url), 'utf8');
function method(source, name) {
    const start = source.indexOf(`function ${name}(`);
    assert(start >= 0, `Missing owner method ${name}`);
    const first = source.indexOf('{', start);
    let depth = 0, quote = '', lineComment = false, blockComment = false;
    for (let i = first; i < source.length; ++i) {
        const c = source[i], next = source[i + 1];
        if (lineComment) { if (c === '\n') lineComment = false; continue; }
        if (blockComment) { if (c === '*' && next === '/') { blockComment = false; ++i; } continue; }
        if (quote) { if (c === '\\') ++i; else if (c === quote) quote = ''; continue; }
        if (c === '/' && next === '/') { lineComment = true; ++i; continue; }
        if (c === '/' && next === '*') { blockComment = true; ++i; continue; }
        if (c === '"' || c === "'" || c === '`') { quote = c; continue; }
        if (c === '{') ++depth;
        if (c === '}' && --depth === 0) return source.substring(start, i + 1);
    }
    throw new Error(`Unclosed method ${name}`);
}
const plain = data => JSON.parse(JSON.stringify(data));
const builders = {};
vm.createContext(builders);
vm.runInContext(qml('CollectionsReferences.js').replace(/^\.pragma library\s*/, ''), builders);
const book = builders.book({id:123,title:'Quoted <&> "book"',author:'Author',isAudiobook:true});
assert.equal(book.sourceId, '123');
assert.equal(book.route, 'book-detail');
assert.equal(book.editionId, 'audiobook');
assert.equal(book.title, 'Quoted <&> "book"');
assert.equal(builders.manga({id:'mal:13:color',payload:{sourceRequiredMarkers:['colored']}},'',0).editionId, 'color');
const theatre = builders.theatre({collectionEntry:() => ({id:'kitsu:13783'}),mediaType:'series',resolvedId:'tt1234567',title:'Title',cover:'cover',banner:'art',year:'2026'});
assert.equal(theatre.sourceId, 'kitsu:13783');
assert.equal(theatre.payload.canonicalSourceId, 'tt1234567');
const failedTheatre = builders.theatre({collectionEntry:() => ({id:'kitsu:13783'}),mediaType:'series',resolvedId:'',loading:false,title:'Timed-out metadata'});
assert.equal(failedTheatre.sourceId,'','timeout does not create an interim provider-only collection key');
const directImdb = builders.theatre({collectionEntry:() => ({id:'tt2543164'}),mediaType:'movie',resolvedId:'',loading:true,title:'Arrival'});
assert.equal(directImdb.sourceId,'tt2543164','an explicit IMDb door remains usable without metadata');
const pendingArtist = builders.artist({online:true,artist:{name:'Name-only result'},name:'Name-only result'});
assert.equal(pendingArtist.sourceId, '', 'artist name never invents a source identity');

const back = {};
vm.createContext(back);
vm.runInContext(qml('ShellBackPolicy.js').replace(/^\.pragma library\s*/, ''), back);
assert.equal(back.actionFor({collectionsModalOpen:true,taskbarOpen:true}), 'collections');
assert.equal(back.actionFor({collectionsActive:true,historyStatsActive:true}), 'collections');
assert.equal(back.actionFor({collectionsActive:true,playerOpen:true}), 'player');
assert.equal(back.actionFor({collectionsModalOpen:true,identityCeremonyOpen:true}), 'cancelIdentityCeremony');

const mainSource = qml('Main.qml');
const declaredMainIds = new Set([...mainSource.matchAll(/\bid:\s*([A-Za-z_]\w*)/g)].map(match => match[1]));
assert(!/\buniverseHallLayer\b/.test(mainSource), 'The retired Hall has no owner in Main.qml');
const fullWindowBinding = mainSource.match(/readonly property bool fullWindowPageOpen:\s*([\s\S]*?)(?:\n\s*\n)/)?.[1];
assert(fullWindowBinding, 'Missing full-window page binding');
const pageOwners = {};
for (const match of fullWindowBinding.matchAll(/\b([A-Za-z_]\w*)\.(active|showing)\b/g)) {
    assert(declaredMainIds.has(match[1]), `Full-window page owner ${match[1]} must exist in Main.qml`);
    pageOwners[match[1]] ??= {};
    pageOwners[match[1]][match[2]] = false;
}
assert.equal(vm.runInNewContext(fullWindowBinding, pageOwners), false);
pageOwners.universeLayer.active = true;
assert.equal(vm.runInNewContext(fullWindowBinding, pageOwners), true, 'The actual universe detail owns the overlay');
pageOwners.universeLayer.active = false;
pageOwners.collectionsPanel.showing = true;
assert.equal(vm.runInNewContext(fullWindowBinding, pageOwners), true, 'Collections owns its full-window page');
function shell() {
    const calls = [], messages = [];
    const context = {
        namedCollectionsNavigation:null, namedCollectionsRouteBypass:false, namedCollectionsReturn:null,
        namedCollectionsOpening:null, namedCollectionsVaultPending:null, bookReturnSnapshot:null,
        vinylWorld:null, vinylEnabled:true, vinylProfileActive:true, androidHost:false,
        CollectionsIdentity:{collectionReference:r => r.invalid ? {ok:false,error:'Unresolved source'} : {ok:true,reference:r}},
        CatalogDelivery:{available:catalog => catalog.available},
        ComicsCatalog:{available:false,series:() => null},
        Comics:{packVolumes:() => ({mains:[],extras:[]})},
        VaultLibrary:{browseDetail:() => ({found:false})},
        openModes:{count:0,get:() => null,append:item => calls.push(['appendWorld',plain(item)])},
        phoneShell:{closeProfilePage:() => calls.push(['closeProfile'])},
        Qt:{BacktabFocusReason:1},
        collectionsPanel:{pendingChanges:false, showing:true, pickerActive:false, deleteActive:false, collectionId:'saved-collection',
            showMessage:message => messages.push(message), forceActiveFocus:() => {}}
    };
    for (const name of ['seriesLayer','westernLayer','comicSeriesLayer','theatreSeriesLayer','bookLayer','searchLayer',
        'worldSearchLayer','universeLayer','onePieceArcLayer','locgPublisherLayer','comicIndexLayer','comicBoardLayer',
        'continueSeeAllLayer','genreLayer','genreIndexLayer','biblioGenreLayer','biblioGenreIndexLayer','theatreGenreLayer','theatreGenreIndexLayer','vaultLayer']) {
        assert(declaredMainIds.has(name), `Route test double ${name} must have an actual QML owner`);
        context[name] = {active:false,item:null};
    }
    for (const name of ['openWorld','openTheatreSeries','openBook','openSeries','_openComicSeriesReady',
        '_openPinnedGcdSeries','_openGcdSeriesReady','openUniverseComic','openPackSeries','openVaultPage','clearSeriesEditionProfile'])
        context[name] = (...args) => calls.push([name,...plain(args)]);
    context._openWesternReady = () => { throw new Error('Collection routes must never use title fallback'); };
    context.win = context;
    vm.createContext(context);
    for (const name of ['guardNamedCollectionsNavigation','cancelNamedCollectionsOpen','clearNamedCollectionsUnderlyingPages',
        'acceptNamedCollectionsRoute','restoreNamedCollectionsReturn','openNamedCollectionReference'])
        vm.runInContext(method(mainSource,name),context);
    context.collectionsPanel.requestClose = () => {
        if (context.collectionsPanel.pendingChanges) return false;
        context.collectionsPanel.showing = false;
        const continuation = context.namedCollectionsNavigation;
        context.namedCollectionsNavigation = null;
        if (continuation) continuation();
        return true;
    };
    return {context,calls,messages};
}
const reference = (route,sourceId,payload={}) => ({key:'validated-by-native-test',world:'tankoban',kind:'comic',sourceId,route,payload,title:'Same display title',cover:'cover',year:'2026'});
const theatreReference = (sourceId,canonicalSourceId,kind='series') => ({
    ...reference('theatre-detail',sourceId,canonicalSourceId ? {canonicalSourceId} : {}),
    world:'theatre',kind,title:'Saved title'
});
const theatrePageSources = ['TheatreSeries.qml','TheatreSeriesView.qml'].map(name => ({name,source:qml(name)}));
function resolveTheatrePage(source,item,metadataById) {
    const metaCalls = [], castCalls = [];
    const c = {
        itemData:plain(item), resolvedId:'', mediaType:'movie', title:'', cover:'', banner:'', logo:'', year:'',
        genresLine:'', rating:'', runtime:'', synopsis:'', factRows:[], castPeople:[], moreLikeCards:[],
        sourceVideos:[], animeOrder:{}, episodeOrderMode:'', seasons:[], activeSeason:0, requestedSourceId:'',
        loading:true, errorMsg:'', tmdbId:0, animeDoor:'', _moreLikeCancel:null,
        cancelMoreLike() {}, defaultAnimeOrder:() => ({}), rebuildAnimeOrder() {}, onMetaLoaded() {},
        revealGuard:{restart() {},stop() {}},
        TheatreApi:{
            loadMeta:(type,id,done) => { metaCalls.push({type,id}); done(metadataById[id] || null); },
            normalizeArtUrl:url => url || '',
            loadAnimeCast:(id,done) => { castCalls.push(id); done(null); },
            moreLikeThis:(...args) => { args.at(-1)([]); return null; }
        },
        TheatreFacts:{factRows:() => []}
    };
    c.page = c;
    vm.createContext(c);
    for (const name of ['currentId','collectionEntry','resolve']) vm.runInContext(method(source,name),c);
    c.resolve();
    return {page:c,metaCalls,castCalls};
}
{
    const {context:c} = shell();
    c.universeLayer.active = true;
    c.onePieceArcLayer.active = true;
    c.clearNamedCollectionsUnderlyingPages();
    assert.equal(c.universeLayer.active, false);
    assert.equal(c.onePieceArcLayer.active, false);
}
{
    const {context:c,calls,messages} = shell();
    c.openNamedCollectionReference({...reference('comic-locg-detail','locg:12345'),invalid:true});
    assert.equal(c.collectionsPanel.showing,true);
    assert.equal(calls.length,0);
    assert.equal(messages.at(-1),'Unresolved source');
}
{
    const {context:c,calls} = shell();
    c.openNamedCollectionReference(reference('comic-tag-detail','gc:batman',{tag:'batman',tagId:'55'}));
    assert.equal(c.westernLayer.tagSlug,'batman');
    assert.equal(c.westernLayer.tagId,55);
    assert.equal(c.westernLayer.resumeChapterId,'');
    assert.equal(c.westernLayer.active,true);
    assert.equal(c.collectionsPanel.showing,false);
    assert(calls.some(row => row[0] === 'openWorld' && row[1] === 'Tankoban'));
    assert.equal(c.restoreNamedCollectionsReturn(),true);
    assert.equal(c.collectionsPanel.collectionId,'saved-collection');
    assert.equal(c.collectionsPanel.showing,true);
}
{
    const {context:c,calls} = shell();
    c.openNamedCollectionReference(reference('comic-gcd-detail','gcd:119237'));
    assert.equal(c.collectionsPanel.showing,true,'unavailable GCD reference stays visible');
    assert.equal(calls.length,0,'unavailable GCD must not substitute a title search');
    c.openNamedCollectionReference(reference('comic-gcd-detail','gcd:119237',{posts:['101','202']}));
    const call = calls.find(row => row[0] === '_openPinnedGcdSeries');
    assert.equal(call[1].gcdId,119237);
    assert.deepEqual(call[1].posts,['101','202']);
}
{
    const {context:c,calls} = shell();
    c.openNamedCollectionReference(reference('comic-pack-detail','pack:9'));
    assert.equal(c.collectionsPanel.showing,true);
    c.Comics.packVolumes = () => ({mains:[{id:'pack:9:1'}],extras:[]});
    c.openNamedCollectionReference(reference('comic-pack-detail','pack:9'));
    assert.equal(calls.find(row => row[0] === 'openPackSeries')[1].seriesId,'pack:9');
}
{
    const {context:c,messages} = shell();
    vm.runInContext(method(mainSource,'_openGcdSeriesReady'),c);
    let deferred;
    c.ComicsCatalog.available = true;
    c.ComicsCatalog.series = () => ({gcdId:119237,title:'Exact run'});
    c._openPinnedGcdSeries = () => false;
    c.collectionsPanel.requestClose = () => {
        c.collectionsPanel.showing = false;
        deferred = c.namedCollectionsNavigation;
        c.namedCollectionsNavigation = null;
        return true;
    };
    c.openNamedCollectionReference(reference('comic-gcd-detail','gcd:119237'));
    assert.equal(typeof deferred,'function');
    c.ComicsCatalog.available = false; // provider changes before Qt.callLater dispatch
    deferred();
    assert.equal(c.collectionsPanel.showing,true,'late GCD unavailability restores the same collection');
    assert.equal(c.collectionsPanel.collectionId,'saved-collection');
    assert.match(messages.at(-1),/unavailable/);
}
{
    const {context:c,calls} = shell();
    c.openNamedCollectionReference(reference('manga-weebcentral-detail','01JEXACT',{seriesUrl:'https://weebcentral.com/series/01JEXACT'}));
    assert.equal(c.seriesLayer.exactCollectionReference.sourceId,'01JEXACT');
    assert.equal(c.seriesLayer.active,true);
    assert(!calls.some(row => row[0] === 'openSeries'),'exact WeebCentral source never enters MAL/name discovery');
}
for (const {name:sourceName,source} of theatrePageSources) {
    {
        const {context:c,calls} = shell();
        c.openNamedCollectionReference(theatreReference('kitsu:17','tt1111111'));
        const routed = calls.find(row => row[0] === 'openTheatreSeries')?.[1];
        assert(routed, `${sourceName}: a pinned Theatre item opens its detail page`);
        assert.equal(routed.id,'tt1111111',`${sourceName}: reopen through the saved canonical anchor`);
        assert.equal(routed.canonicalSourceId,'tt1111111');
        assert.equal(routed.sourceId,'kitsu:17',`${sourceName}: retain the original provider door separately`);
        const resolved = resolveTheatrePage(source,routed,{
            'kitsu:17':{id:'tt2222222',name:'Remapped title',genres:[]},
            'tt1111111':{id:'tt1111111',name:'Pinned title',genres:[],videos:[]}
        });
        assert.deepEqual(resolved.metaCalls,[{type:'series',id:'tt1111111'}],
                         `${sourceName}: metadata resolution must use the canonical anchor, not the remapped provider door`);
        assert.equal(resolved.page.title,'Pinned title');
        assert.equal(resolved.page.requestedSourceId,'kitsu:17');
        assert.equal(resolved.page.animeDoor,'tt1111111');
        assert.deepEqual(resolved.castCalls,['tt1111111'],`${sourceName}: provider-backed details stay scoped to the pinned title`);
        assert.equal(resolved.page.collectionEntry().id,'kitsu:17',`${sourceName}: page saves with its original provider door`);
        const savedAgain = builders.theatre(resolved.page);
        assert.equal(savedAgain.sourceId,'kitsu:17');
        assert.equal(savedAgain.payload.canonicalSourceId,'tt1111111');
    }
    {
        const {context:c,calls} = shell();
        c.openNamedCollectionReference(theatreReference('kitsu:17','tt1111111'));
        const routed = calls.find(row => row[0] === 'openTheatreSeries')?.[1];
        const resolved = resolveTheatrePage(source,routed,{
            'tt1111111':{id:'tt1111111',name:'Pinned title',genres:[],videos:[]}
        });
        assert.deepEqual(resolved.metaCalls,[{type:'series',id:'tt1111111'}],
                         `${sourceName}: an unavailable provider door does not block reopening by the saved anchor`);
        assert.equal(resolved.page.title,'Pinned title');
        assert.equal(resolved.page.collectionEntry().id,'kitsu:17');
    }
    {
        const {context:c,calls} = shell();
        c.openNamedCollectionReference(theatreReference('tt2543164','','movie'));
        const routed = calls.find(row => row[0] === 'openTheatreSeries')?.[1];
        assert(routed, `${sourceName}: direct IMDb reference remains openable`);
        assert.equal(routed.id,'tt2543164');
        assert.equal(routed.canonicalSourceId,'tt2543164');
        assert.equal(routed.sourceId,'tt2543164');
        assert.equal(routed.type,'movie');
        const resolved = resolveTheatrePage(source,routed,{
            'tt2543164':{id:'tt2543164',name:'Arrival',genres:[],videos:[]}
        });
        assert.deepEqual(resolved.metaCalls,[{type:'movie',id:'tt2543164'}]);
        assert.equal(resolved.page.title,'Arrival');
    }
    {
        const {context:c,calls,messages} = shell();
        c.openNamedCollectionReference(theatreReference('kitsu:17',''));
        assert.equal(calls.some(row => row[0] === 'openTheatreSeries'),false,
                     `${sourceName}: legacy provider-only references cannot guess a replacement title`);
        assert.equal(c.collectionsPanel.showing,true);
        assert.match(messages.at(-1),/canonical identity/i);
    }
    {
        const {context:c,calls,messages} = shell();
        c.openNamedCollectionReference(theatreReference('tt1111111','tt2222222'));
        assert.equal(calls.some(row => row[0] === 'openTheatreSeries'),false,
                     `${sourceName}: incompatible direct and canonical IMDb identities fail closed`);
        assert.equal(c.collectionsPanel.showing,true);
        assert.match(messages.at(-1),/identit/i);
    }
    {
        const pinned = {id:'tt1111111',canonicalSourceId:'tt1111111',sourceId:'kitsu:17',type:'series',title:'Saved title'};
        const resolved = resolveTheatrePage(source,pinned,{
            'tt1111111':{id:'tt2222222',name:'Wrong title',genres:[],videos:[]}
        });
        assert.equal(resolved.page.title,'Saved title',`${sourceName}: mismatched metadata cannot replace the saved title`);
        assert.equal(resolved.page.resolvedId,'');
        assert.equal(resolved.page.loading,false);
        assert.match(resolved.page.errorMsg,/canonical identity/i);
    }
}
{
    const {context:c,calls} = shell();
    let opened = null;
    c.vinylWorld = {cancelNamedCollectionOpen:() => {},openNamedCollectionReference:r => { opened = r; }};
    c.openNamedCollectionReference(reference('vinyl-song-detail','vault:track',{albumId:'vinyl:local-album:a',browseId:'vinyl:local-album:a'}));
    assert(opened);
    assert.equal(c.collectionsPanel.showing,true,'music read keeps the collection visible until exact source acceptance');
    assert.equal(calls.length,0,'music read does not switch the world or queue before acceptance');
}
{
    const {context:c} = shell();
    c.collectionsPanel.pendingChanges = true;
    let navigated = false;
    assert.equal(c.guardNamedCollectionsNavigation(() => { navigated = true; }),true);
    assert.equal(navigated,false,'navigation must wait for the draft decision');
    assert.equal(c.collectionsPanel.showing,true);
}

const vinylSource = qml('vinyl/VinylLiveWorld.qml');
function vinyl() {
    const outcomes = [], calls = [];
    const c = {
        namedCollectionPending:{route:'vinyl-song-detail',sourceId:'vault:track-2',editionId:'edition-b',payload:{albumId:'album-a'}},
        namedCollectionRequestId:5, namedCollectionReturn:null, page:'library', routeStack:[],
        collectionOpenTimeout:{stop:() => {}}, namedCollectionOpened:(ok,message) => outcomes.push({ok,message}),
        session:{plainData:plain,validAlbum:record => !!record && Array.isArray(record.editions),
            normalizeEditions:record => record.editions.map(edition => ({...edition,albumId:record.id})),
            cancelBrowse:() => {}, shownEdition:null,shownDisc:0},
        pushRoute:() => { c.routeStack.push({page:'library'}); },
        showBrowseSong:(record,index,edition) => { calls.push({record:plain(record),index,edition}); c.page='song'; },
        recentSearchMenu:{close:() => {}},searchOpen:false,artistSearchOpen:false,Qt:{callLater:() => {}},parkFocus:() => {}
    };
    c.root = c;
    vm.createContext(c);
    for (const name of ['finishNamedCollectionOpen','acceptNamedCollectionAlbum']) vm.runInContext(method(vinylSource,name),c);
    return {context:c,outcomes,calls};
}
const album = {id:'album-a',editions:[{id:'edition-a',tracks:[{id:'vault:track-1'}]},
    {id:'edition-b',tracks:[{id:'vault:track-1'},{id:'vault:track-2'}]}]};
{
    const {context:c,outcomes,calls} = vinyl();
    c.acceptNamedCollectionAlbum(album,'');
    assert.equal(outcomes[0].ok,true);
    assert.equal(calls[0].edition,'edition-b');
    assert.equal(calls[0].index,1);
    assert.equal(c.session.shownEdition,null,'song browse must not replace playing/selected album state');
}
for (const change of [record => {record.id='different-album';},record => {record.editions.splice(1,1);},
    record => {record.editions[1].tracks.splice(1,1);}]) {
    const {context:c,outcomes,calls} = vinyl(), record = plain(album);
    change(record);
    c.acceptNamedCollectionAlbum(record,'');
    assert.equal(outcomes[0].ok,false);
    assert.equal(calls.length,0,'wrong album, edition or track cannot substitute another item');
    assert.equal(c.page,'library');
}
console.log('collections_routes_test: adapters, exact routes, draft guard, Back precedence, and Vinyl availability cases passed');

// Regression: the legacy source page renders ReadingRoom, not legacyCorridor.
const legacyController = qml('MangaSeriesThumbnailMock.qml');
const liveRoom = qml('MangaReadingRoomThumbnailMock.qml');
const liveBinding = legacyController.slice(legacyController.indexOf('MangaReadingRoomThumbnailMock {'));
assert.match(liveBinding, /namedCollectionReference:\s*CollectionsReferences\.weebcentral\(page\)/);
assert.match(liveRoom, /objectName:\s*"weebcentralAddToCollections"/);
assert.match(liveRoom, /reference:\s*root\.namedCollectionReference/);
assert.equal((legacyController.match(/weebcentralAddToCollections/g) || []).length, 0,
             'An action in an uninstantiated corridor is not live coverage');
assert.match(mainSource, /if \(item\.openExtensionsRequested\)\s*item\.openExtensionsRequested\.connect/,
             'Optional signals must not abort legacy page lifecycle wiring');

// The real Vault row kind is video; episode/clip leaf details remain ineligible.
const filmDetail={found:true,key:'C:/Media/Arrival',identitySource:'IMDB',identitySourceId:'imdb:tt2543164',displayTitle:'Arrival'};
assert.equal(builders.vault(filmDetail,'video').sourceId,'imdb:tt2543164');
assert.equal(builders.vault({...filmDetail,isFileDetail:true},'video').sourceId,'');
const savedVault={sourceId:'imdb:tt2543164',payload:{vaultKey:'C:/Media/Arrival'}};
const nativeKey='C:/media/arrival';
const owner={series:kind=>{assert.equal(kind,'video');return[{key:nativeKey,title:'Ignored'}]},browseDetail:key=>key===nativeKey?{...filmDetail,key:nativeKey}:{found:false}};
assert.equal(builders.vaultDetail(owner,savedVault,true).key,nativeKey);
assert.equal(builders.vaultDetail(owner,savedVault,false),null);
assert.equal(builders.vaultDetail({...owner,browseDetail:key=>({...filmDetail,key,identitySourceId:'imdb:tt9999999'})},savedVault,true),null);
assert.equal(builders.vaultDetail({...owner,series:()=>[{key:nativeKey},{key:'C:/MEDIA/ARRIVAL'}]},savedVault,true),null);
assert.match(qml('VaultDetailSheet.qml'), /sheet\.mediaKind === "video"/);

assert(qml("VaultDetailSheet.qml").indexOf('objectName: "vaultAddToCollections"') < qml("VaultDetailSheet.qml").indexOf("id: copyDelegate"), "Collections belongs with the real primary detail actions, before the long copies list");
