import QtQuick
import "VinylWorldLists.js" as Lists

// Presentation only. Collection records and the music session remain authoritative.
// Taste is owned by the active profile preferences.
Item {
    id: root
    objectName: "vinylLiveWorldData"
    required property var session
    property var personalLibrary: null
    property var recommendationService: null
    property var homeListSnapshot: null
    property string profile: "local"
    property var preferencesStore: typeof ProfilePreferences !== "undefined" ? ProfilePreferences : null
    property var searchHistoryStore: typeof SearchHistory !== "undefined" ? SearchHistory : null
    property string personalError: ""
    property bool fallbackPickerAnswered: false
    property bool fallbackTastePickerOpen: false
    property bool fallbackEditing: false
    property var fallbackPicks: []
    property var fallbackPickerDraft: []
    property string fallbackPickerQuery: ""
    property string artistSearchText: ""
    readonly property bool usesRecommendationService: !!recommendationService && !!preferencesStore
        && typeof preferencesStore.setVinylTasteSelections === "function"
    readonly property bool canEnterVinyl: usesRecommendationService
        ? recommendationService.canEnterVinyl === true
        : fallbackPickerAnswered && fallbackPicks.length >= 5
    readonly property int minimumArtistCount: usesRecommendationService
        ? Number(recommendationService.minimumArtistCount) || 5 : 5
    readonly property int homeRevision: usesRecommendationService
        ? Number(recommendationService.homeRevision) || 0 : 0
    readonly property bool homePublicDiscovery: false
    readonly property string homeStatus: homePublicDiscovery
        ? discoveryLoading ? "loading" : discoveryError ? publicHomeRows.some(row=>row.items.length) ? "stale" : "error"
            : publicHomeRows.some(row=>row.items.length) ? "ready" : "empty"
        : usesRecommendationService ? String(recommendationService.homeStatus || "empty") : "empty"
    readonly property string homeError: homePublicDiscovery ? discoveryError
        : usesRecommendationService ? String(recommendationService.homeError || "") : ""
    readonly property var recommendationHomeSnapshot: {
        var revision = homeRevision;
        if(!usesRecommendationService || !recommendationService.homeSnapshot) return ({});
        var snapshot = session.plainData(recommendationService.homeSnapshot);
        return snapshot && typeof snapshot === "object" ? snapshot : ({});
    }
    readonly property string homeHistoryFence: JSON.stringify([
        recommendationHomeSnapshot.historyEnabled,
        recommendationHomeSnapshot.activityResetGeneration,
        recommendationHomeSnapshot.historyResetGeneration])
    onHomeHistoryFenceChanged: {
        // Stable See-all copies contain private ranking explanations too.
        // Public catalogue snapshots remain independent of listening privacy.
        if(homeListSnapshot)homeListSnapshot=null;
    }
    readonly property var recommendationHomeSections: {
        var revision = homeRevision;
        var sections = usesRecommendationService ? recommendationService.homeSections || [] : [];
        sections = session.plainData(sections);
        return Array.isArray(sections) ? sections : [];
    }
    readonly property string onboardingState: usesRecommendationService
        ? recommendationService.onboardingState
        : fallbackEditing ? "editing" : fallbackTastePickerOpen ? "draft" : fallbackPickerAnswered ? "answered" : "unanswered"
    readonly property bool pickerAnswered: usesRecommendationService
        ? onboardingState === "answered" : fallbackPickerAnswered
    readonly property bool tastePickerOpen: usesRecommendationService
        ? activeTab === "home" && (onboardingState === "draft" || onboardingState === "editing")
        : fallbackTastePickerOpen
    readonly property var picks: usesRecommendationService
        ? recommendationService.selectedArtists : fallbackPicks
    readonly property var pickerDraft: usesRecommendationService
        ? (recommendationService.draftArtists || []).map(function(artist) { return artist.key || artist.id; })
        : fallbackPickerDraft
    readonly property string pickerQuery: usesRecommendationService
        ? artistSearchText : fallbackPickerQuery
    readonly property string persistenceError: personalError
        || (usesRecommendationService ? recommendationService.error || ""
            : preferencesStore ? preferencesStore.vinylPersistenceError || "" : "")
    readonly property var recentSearches: {
        var revision = searchHistoryStore ? searchHistoryStore.revision : 0;
        return searchHistoryStore ? JSON.parse(JSON.stringify(searchHistoryStore.list("Vinyl"))) : [];
    }
    function rememberSearch() { if(searchHistoryStore) searchHistoryStore.record("Vinyl",query); }
    function loadTaste() {
        if(usesRecommendationService) return;
        personalError = "";
        fallbackPicks = preferencesStore ? JSON.parse(JSON.stringify(preferencesStore.vinylTasteArtistIds || [])) : [];
        fallbackPickerAnswered = !!preferencesStore && !!preferencesStore.vinylTasteAnswered
            && fallbackPicks.length>=minimumArtistCount;
        fallbackPickerDraft = fallbackPicks.slice(); fallbackPickerQuery = "";
        fallbackTastePickerOpen = activeTab==="home" && !fallbackPickerAnswered;
    }
    onPreferencesStoreChanged: loadTaste()
    Component.onCompleted: loadTaste()
    Connections { target: root.preferencesStore; ignoreUnknownSignals:true; function onVinylTasteChanged() { root.loadTaste(); } }

    // Transient presentation state only: the three destinations do not share
    // filters or search results. Collection and the session still own the data.
    property string activeTab: "library"
    property var browseStates: initialBrowseStates()
    property bool changingTab: false
    property bool onlineMode: false
    property var catalogueMatches: ({query:"",albums:[],artists:[]})
    property string pendingCatalogueQuery: ""
    property var recentContinue: []
    property real homeResumePosition: 0
    property real homeNowMs: Date.now()
    property string homeSelectionDay: ""
    Timer {
        interval:60000;repeat:true;running:root.activeTab==="home" && root.canEnterVinyl
        onTriggered:root.refreshHome()
    }
    readonly property var homeListeningCards: usesRecommendationService
        ? session.plainData(recommendationService.homeListeningCards || []) : []
    readonly property var homeQuickPicks: usesRecommendationService
        ? session.plainData(recommendationService.homeQuickPicks || []) : []
    function refreshHomeSelection() {
        homeNowMs=Date.now();
        if(usesRecommendationService && typeof recommendationService.refreshHomeSelection==="function")
            recommendationService.refreshHomeSelection();
        homeSelectionRefreshed();
    }
    signal homeSelectionRefreshed()
    function refreshHome() {
        homeNowMs=Date.now();
        var today=Qt.formatDateTime(new Date(homeNowMs),"yyyy-MM-dd");
        if(homeSelectionDay && homeSelectionDay!==today && usesRecommendationService
            && typeof recommendationService.refreshHome==="function")recommendationService.refreshHome(false);
        homeSelectionDay=today;
        homeResumePosition=session.position || 0;
        if(!session.profileActive || session.shuttingDown || !session.progressStore
            || typeof session.checkpointSummary!=="function") {recentContinue=[];return;}
        if(typeof session.progressStore.get==="function") {
            var latest=session.plainData(session.progressStore.get("music-session","vinyl"));
            var state=session.checkpointSummary(latest && latest.resume);
            if(state.valid && state.empty){recentContinue=[];return;}
        }
        var values=session.plainData(session.progressStore.recent("music",8)) || [];
        recentContinue=values.map(function(entry){
            var summary=session.checkpointSummary(entry.resume),row=summary.row;
            if(!row || (summary.duration>0 && summary.position>=summary.duration-0.5))return null;
            return {id:"resume:"+row.editionId,kind:"resume",title:row.title,subtitle:row.artist,
                cover:row.cover || "",editionId:row.editionId,trackId:row.trackId,
                lastPlaybackAtMs:Number(summary.lastPlaybackAtMs)||0,
                unavailable:!session.personalRowAvailable(row),resumeEntry:entry};
        }).filter(item=>!!item).slice(0,6);
    }
    function retryHomeRecommendations() {
        if(homePublicDiscovery)return retryDiscovery();
        if(usesRecommendationService && typeof recommendationService.refreshHome === "function")
            return recommendationService.refreshHome(true);
        return false;
    }
    function retryHomeRow(row) {
        if(row && String(row.key || "").indexOf("home-discover-")===0)return retryDiscovery();
        return retryHomeRecommendations();
    }
    function personalizedHomeRow(section, snapshot, generation) {
        if(!section || section.kind!=="recommendations" || !Array.isArray(section.items)) return null;
        var identity=String(section.id || section.key || "").trim();
        if(!identity) return null;
        var items=section.items.map(function(source) {
            if(!source || typeof source!=="object") return null;
            var item=Object.assign({},source),destination=item.destination || {};
            var kind=String(item.kind || (destination.kind==="album" ? "albums"
                : destination.kind==="artist" ? "artists" : destination.kind==="song" ? "songs" : ""));
            if(["albums","artists","songs","saved-song"].indexOf(kind)<0) return null;
            if(kind==="saved-song" && (destination.kind!=="saved-song" || item.unavailable===true || !item.row))return null;
            if(kind==="songs") {
                var tracks=item.record && Array.isArray(item.record.tracks) ? item.record.tracks : [];
                var trackIndex=Number(item.trackIndex);
                if(!Number.isInteger(trackIndex) || trackIndex<0 || trackIndex>=tracks.length) {
                    var trackIds=[String(item.trackId || ""),String(destination.id || ""),String(destination.providerId || "")];
                    trackIndex=tracks.findIndex(function(track){return track && track.id
                        && trackIds.indexOf(String(track.id))>=0;});
                }
                if(destination.kind!=="song" || destination.playable!==true
                    || trackIndex<0 || trackIndex>=tracks.length || tracks[trackIndex].available===false) return null;
                item.trackIndex=trackIndex;
            }
            item.id=String(item.id || destination.id || destination.providerId || "");
            item.title=String(item.title || item.name || "");
            if(!item.id || !item.title) return null;
            item.kind=kind;
            item.destination=destination;
            item.cover=String(item.cover || "");
            if(kind==="artists") {
                item.artistId=String(destination.id || destination.providerId || item.artistId || "");
                item.artist=String(item.artist || item.title);
                item.round=true;
            }
            return item;
        }).filter(function(item) { return !!item; });
        var status=String(section.status || "ready");
        if(!items.length && ["loading","error","stale"].indexOf(status)<0) return null;
        return {key:"personalized:"+identity,sectionId:identity,
            exposureSnapshotId:String(snapshot && snapshot.exposureSnapshotId || ""),
            profileGeneration:generation,title:String(section.title || "Recommendations"),
            kind:"recommendations",recommendation:true,items:items,status:status,loading:section.loading===true,
            error:String(section.error || ""),canRetry:section.canRetry===true,
            fetchedAt:String(section.fetchedAt || ""),reason:String(section.reason || "")};
    }
    function recommendationProfileGeneration() {
        var generation = Number(session.profileGeneration);
        return Number.isInteger(generation) && generation >= 0 ? generation : 0;
    }
    function recommendationArtistRef(artist) {
        if(!artist || typeof artist !== "object") return null;
        var provider=String(artist.provider || "").trim();
        var providerId=String(artist.providerId || artist.localId || "").trim();
        var artistId=String(artist.artistId || "").trim();
        var id=String(artist.id || "").trim();
        if(providerId.indexOf("ytm:artist:")===0 || artistId.indexOf("ytm:artist:")===0
            || id.indexOf("ytm:artist:")===0) {
            provider="youtube-music";
            var ytmId=providerId.indexOf("ytm:artist:")===0 ? providerId
                : artistId.indexOf("ytm:artist:")===0 ? artistId : id;
            providerId=ytmId.slice("ytm:artist:".length);
        }
        if(!providerId) providerId=artistId || id;
        if(!provider) provider="local";
        if(!providerId) return null;
        return {identity:"provider",kind:"artist",providerId:provider,itemId:providerId};
    }
    function recommendationRefsEqual(left, right) {
        return !!left && !!right
            && String(left.identity || "")==="provider" && String(right.identity || "")==="provider"
            && String(left.kind || "")===String(right.kind || "")
            && String(left.providerId || "")===String(right.providerId || "")
            && String(left.itemId || "")===String(right.itemId || "");
    }
    function suppressedRecommendationsForArtist(artistRef) {
        if(!artistRef || artistRef.identity!=="provider" || artistRef.kind!=="artist"
            || !artistRef.providerId || !artistRef.itemId) return [];
        var snapshot=recommendationHomeSnapshot;
        var suppressed=snapshot && Array.isArray(snapshot.suppressedItems) ? snapshot.suppressedItems : [];
        return suppressed.filter(function(entry) {
            if(!entry || typeof entry!=="object"
                || (entry.hidden!==true && entry.snoozed!==true && entry.excluded!==true)) return false;
            if(recommendationRefsEqual(entry.ref,artistRef)) return true;
            if(recommendationRefsEqual(recommendationArtistRef(entry.seedArtistRef),artistRef)) return true;
            var credits=Array.isArray(entry.artistRefs) ? entry.artistRefs : [];
            return credits.some(function(artist) {
                return recommendationRefsEqual(recommendationArtistRef(artist),artistRef);
            });
        }).slice(0,300);
    }
    function setRecommendationFeedback(ref, action, expectedGeneration) {
        var generation=Number(expectedGeneration);
        if(!usesRecommendationService || typeof recommendationService.setFeedback!=="function"
            || !ref || typeof ref!=="object" || !String(action || "")
            || !session.profileActive || session.shuttingDown
            || !Number.isInteger(generation) || generation!==recommendationProfileGeneration()) return false;
        return recommendationService.setFeedback(ref,String(action),generation)===true;
    }
    function recordVisibleRecommendation(row, item, expectedGeneration, actuallyVisible) {
        var generation=Number(expectedGeneration);
        if(!actuallyVisible || !row || row.recommendation!==true || !item || !item.ref
            || !usesRecommendationService || typeof recommendationService.recordVisibleItems!=="function"
            || activeTab!=="home" || tastePickerOpen || !session.profileActive || session.shuttingDown
            || !row.sectionId || !row.exposureSnapshotId
            || !Number.isInteger(generation) || generation!==recommendationProfileGeneration()) return false;
        return recommendationService.recordVisibleItems(String(row.sectionId),String(row.exposureSnapshotId),
            [item.ref],generation)===true;
    }
    readonly property bool catalogueReachable: true
    readonly property bool cataloguePagingSupported: !!session.source && session.source.catalogueSearchVersion===1
    readonly property var searchPage: cataloguePagingSupported && session.catalogueQuery===query.trim() ? session.cataloguePages[lens.kind] || {} : ({})
    readonly property bool searchHasMore: Object.keys(searchPage.nextCursor || {}).length>0
    readonly property bool searchLoading: cataloguePagingSupported && session.catalogueQuery===query.trim() && !!session.catalogueRequests[lens.kind]
    readonly property string searchError: searchPage.error || ""
    function loadMoreSearch() {if(cataloguePagingSupported)session.requestCataloguePage(lens.kind,searchHasMore);}
    readonly property bool discoveryLanding: false
    readonly property bool discoveryLoading: session.discoveryLoading===true
    readonly property var discoverySections: session.discoverySections || ({})
    readonly property var discoveryOrder: []
    readonly property string discoveryError: discoveryOrder.map(key=>discoverySections[key] || {}).map(row=>row.error || "").filter((value,index,all)=>value && all.indexOf(value)===index).join(" ")
    readonly property bool discoveryCached: discoveryOrder.some(key=>(discoverySections[key] || {}).cached===true)
    readonly property string discoveryFetchedAt: discoveryOrder.map(key=>(discoverySections[key] || {}).fetchedAt || "").filter(value=>!!value).sort()[0] || ""
    function ensureDiscovery() {if((discoveryLanding || (activeTab==="home" && homePublicDiscovery)) && typeof session.requestDiscovery==="function")session.requestDiscovery(false);}
    function retryDiscovery() {if(typeof session.requestDiscovery==="function")session.requestDiscovery(true);}
    onDiscoveryLandingChanged: ensureDiscovery()
    onHomePublicDiscoveryChanged: if(homePublicDiscovery)Qt.callLater(root.ensureDiscovery)
    readonly property var publicHomeRows: !homePublicDiscovery ? [] : discoveryOrder.map(function(key) {
        var row=discoverySections[key] || {},items=row.items || [],loading=!!(session.discoveryRequests || {})[key];
        var titles={"new-releases":"New releases",trending:"Trending",artists:"Popular artists",charts:"Charts"};
        if(!items.length && !loading && !row.error)return null;
        return Object.assign({},row,{key:"home-discover-"+key,title:"Discover · "+String(row.title || titles[key]),
            items:items,kind:row.kind || (key==="artists" ? "artists" : "albums"),personal:false,recommendation:false,
            status:loading ? "loading" : row.error ? items.length ? "stale" : "error" : "ready",
            loading:loading,canRetry:!loading,error:String(row.error || "")});
    }).filter(row=>!!row)
    readonly property var libraryAlbums: session.library.map(function(entry) {
        var a = entry.payload && entry.payload.album ? entry.payload.album : entry;
        return Object.assign({}, a, {artists:a.artists || [], tracks:(a.tracks || []).map(function(t) {
            return Object.assign({},t,{artists:t.artists || a.artists || []});
        })});
    })
    readonly property var localAlbums: libraryAlbums.filter(function(a){return a.origin!=="youtube-music";})
    readonly property var visibleLibraryAlbums: libraryAlbums.filter(function(a){
        var entry=session.library.find(function(e){return e.id===a.id || (e.payload && e.payload.album && e.payload.album.id===a.id);});
        return a.origin!=="youtube-music" || !entry || !entry.payload || entry.payload.saved!==false;
    })
    readonly property var discoverSourceAlbums: browseStates.discover.onlineMode
        ? (browseStates.discover.query.trim() && catalogueMatches.query===browseStates.discover.query.trim() ? catalogueMatches.albums : []) : localAlbums
    readonly property var catalogue: ({albums:activeTab==="discover" ? discoverSourceAlbums : libraryAlbums,people:{},profiles:{},homeRegion:[]})
    property var lens: ({kind:"albums",genre:"",decade:"",country:"",language:""})
    property string query: ""
    property string sortOrder: "title"
    function emptyLens() {return {kind:"albums",genre:"",decade:"",country:"",language:""};}
    function initialBrowseStates() {
        return {home:{query:"",lens:emptyLens(),sortOrder:"title",onlineMode:false},
            discover:{query:"",lens:emptyLens(),sortOrder:"title",onlineMode:true},
            library:{query:"",lens:emptyLens(),sortOrder:"title",onlineMode:false}};
    }
    function saveBrowseState() {
        if(changingTab) return;
        var next=Object.assign({},browseStates);
        next[activeTab]={query:query,lens:Object.assign({},lens),sortOrder:sortOrder,onlineMode:activeTab==="discover" && onlineMode};
        browseStates=next;
    }
    function selectTab(tab) {
        if(["home","discover","library"].indexOf(tab)<0) return;
        if(tab==="home" && !usesRecommendationService && !fallbackPickerAnswered)
            fallbackTastePickerOpen=true;
        if(tab==="home" && usesRecommendationService && typeof recommendationService.beginHomeVisit==="function")
            recommendationService.beginHomeVisit();
        if(tab===activeTab) return;
        saveBrowseState();
        if(session.browsing && ["search","catalogueSearch"].indexOf(session.browseOperation)>=0) session.cancelBrowse();
        changingTab=true;activeTab=tab;
        var state=browseStates[tab];query=state.query;lens=Object.assign({},state.lens);sortOrder=state.sortOrder;onlineMode=state.onlineMode;
        changingTab=false;refreshSearch();if(tab==="home")refreshHome();ensureDiscovery();
    }
    function setQuery(value) {query=String(value || "");}
    function setOnlineMode(value) {
        if(activeTab!=="discover") return;
        if(onlineMode===!!value) return;
        session.cancelBrowse();onlineMode=!!value;refreshSearch();
    }
    function captureBrowseState() {
        saveBrowseState();
        return JSON.parse(JSON.stringify({activeTab:activeTab,states:browseStates,matches:catalogueMatches,catalogue:cataloguePagingSupported ? session.captureCatalogue() : null}));
    }
    function restoreBrowseState(snapshot) {
        if(!snapshot || !snapshot.states) return;
        session.cancelBrowse();changingTab=true;
        // Back restores the originating destination, without erasing work done
        // in another tab during a search detour from Album/Artist/Song.
        var next=Object.assign({},browseStates);
        next[snapshot.activeTab]=JSON.parse(JSON.stringify(snapshot.states[snapshot.activeTab]));
        browseStates=next;activeTab=snapshot.activeTab;
        if(activeTab==="discover") {
            catalogueMatches=snapshot.matches || {query:"",albums:[],artists:[]};
            if(cataloguePagingSupported)session.restoreCatalogue(snapshot.catalogue);
        }
        var state=browseStates[activeTab];query=state.query;lens=Object.assign({},state.lens);sortOrder=state.sortOrder;onlineMode=state.onlineMode;
        changingTab=false;refreshSearch();
    }
    function resetBrowseState() {
        changingTab=true;browseStates=initialBrowseStates();activeTab="library";
        query="";lens=emptyLens();sortOrder="title";onlineMode=false;
        pendingCatalogueQuery="";catalogueMatches={query:"",albums:[],artists:[]};changingTab=false;
    }
    function submitSearch() {
        rememberSearch();
        if(!onlineMode) {refreshSearch();return;}
        if(!query.trim()) {pendingCatalogueQuery="";session.cancelBrowse();return;}
        session.searchCatalogue(query.trim(),lens.kind,true);pendingCatalogueQuery=query.trim();
    }
    function acceptCatalogueMatches() {
        if(cataloguePagingSupported) {
            if(session.catalogueQuery===browseStates.discover.query.trim())catalogueMatches={query:session.catalogueQuery,albums:session.onlineAlbums.slice(),artists:session.onlineArtists.slice()};
            return;
        }
        if(!pendingCatalogueQuery || session.browseOperation!=="catalogueSearch") return;
        catalogueMatches={query:pendingCatalogueQuery,albums:session.onlineAlbums.slice(),artists:session.onlineArtists.slice()};
    }
    function refreshSearch() {
        if(changingTab || activeTab==="home" || onlineMode) return;
        if(!session.pagedSearchSupported) return;
        if(query.trim()) session.search(query);
        else if(session.browseOperation==="search") session.cancelBrowse();
    }
    onQueryChanged: {
        saveBrowseState();refreshSearch();
        if(cataloguePagingSupported && activeTab==="discover" && onlineMode && query.trim()!==session.catalogueQuery)session.cancelBrowse();
        if(activeTab==="discover" && onlineMode && !query.trim()) {
            pendingCatalogueQuery="";
            if(session.browseOperation==="catalogueSearch")session.cancelBrowse();
            ensureDiscovery();
        }
    }
    onLensChanged: {saveBrowseState();if(!changingTab && cataloguePagingSupported && activeTab==="discover" && onlineMode && query.trim())session.searchCatalogue(query.trim(),lens.kind,false);}
    onSortOrderChanged: saveBrowseState()
    onOnlineModeChanged: saveBrowseState()
    readonly property string tasteNotice: "Taste choices are saved to this profile."
    readonly property var artistResults: usesRecommendationService ? recommendationService.artistResults || ({}) : ({})
    readonly property var artistResultItems: {
        return artistResults.items || [];
    }
    readonly property bool artistSearchLoading: usesRecommendationService && artistResults.status === "loading"
    readonly property bool artistSearchHasMore: Object.keys((artistResults.nextCursor || {})).length > 0
    readonly property string artistSearchError: usesRecommendationService
        ? (artistResults.error || recommendationService.error || "") : ""
    readonly property string lensTitle: [lens.language,lens.decade,lens.genre,lens.country].filter(function(v){return !!v}).join(" · ") || (onlineMode ? "Catalogue matches" : "Local music")
    function filtered(records,state,localSearch) {return records.filter(function(a) {
        if(!Lists.matches(a,state.lens)) return false;
        // Keep the existing local view while a replacement page chain loads; never
        // expose a partial page as a complete result or alter lens/sort semantics.
        if(!localSearch) return true;
        var complete=session.pagedSearchSupported && session.searchComplete && session.searchQuery===state.query.trim();
        var included=!complete || session.results.some(function(record){return record.id===a.id;});
        return included && (!state.query.trim() || (a.title+" "+a.artists.join(" ")+" "+(a.tracks || []).map(function(t){return t.title}).join(" ")).toLowerCase().indexOf(state.query.trim().toLowerCase())>=0);
    }).sort(function(a,b) {
        return state.sortOrder === "year" ? (Number(b.year)||0)-(Number(a.year)||0) || String(a.title).localeCompare(String(b.title)) : String(a.title).localeCompare(String(b.title));
    });}
    readonly property var filteredLibraryAlbums: filtered(visibleLibraryAlbums,browseStates.library,true)
    readonly property var discoverAlbums: {
        var state=browseStates.discover;
        return filtered(discoverSourceAlbums,state,!state.onlineMode);
    }
    readonly property var albums: activeTab==="discover" ? discoverAlbums : activeTab==="library" ? filteredLibraryAlbums : libraryAlbums
    readonly property var shelves: {
        var state=browseStates.discover;
        if(state.onlineMode && state.query.trim() && cataloguePagingSupported) {
            var kind=state.lens.kind,page=session.catalogueQuery===state.query.trim() ? session.cataloguePages[kind] || {} : {};
            return [{key:"search-"+kind,title:kind.charAt(0).toUpperCase()+kind.slice(1),kind:kind==="playlists"?"catalogue-playlists":kind,
                items:page.items || [],hasMore:Object.keys(page.nextCursor || {}).length>0,loading:!!session.catalogueRequests[kind],error:page.error || ""}];
        }
        if(state.onlineMode && !state.query.trim())return [];
        return [{key:"local-"+state.lens.kind,title:(state.onlineMode ? "Catalogue " : "Local ")+state.lens.kind,icon:"threads",kind:state.lens.kind,items:project(discoverAlbums,state.lens.kind,state.onlineMode)}];
    }
    readonly property var homeRows: {
        if(session.profileActive===false || session.shuttingDown || !canEnterVinyl)return [];
        var nativeSections=recommendationHomeSections;
        var snapshot=recommendationHomeSnapshot;
        var generation=recommendationProfileGeneration();
        var result=[],historyRows=[];
        if(homeQuickPicks.length)result.push({key:"quick-picks",title:"Quick picks",kind:"songs",items:homeQuickPicks});
        if(homeListeningCards.length)result.push({key:"mixes-radio",title:"Mixes & radio",kind:"mixes",items:homeListeningCards});
        var continuing=[],active=session.activeQueueRow;
        if(active && session.sourceState!=="ended" && (!(session.duration>0) || session.position<session.duration-0.5))continuing.push({id:"resume:"+active.queueId,kind:"resume",resumeCurrent:true,
            editionId:active.editionId,title:active.title,subtitle:active.artist,cover:active.cover || "",
            unavailable:typeof session.personalRowAvailable==="function" && !session.personalRowAvailable(active)});
        continuing=continuing.concat(recentContinue.filter(item=>!active || item.editionId!==active.editionId));
        if(continuing.length)result.push({key:"continue-listening",title:"Continue listening",kind:"resume",items:continuing});
        nativeSections.forEach(function(section) {
            var row=personalizedHomeRow(section,snapshot,generation);
            if(!row)return;
            if(section.id==="rediscover-library" || section.id==="less-played")historyRows.push(row);
            else result.push(row);
        });
        var tasteArtists = usesRecommendationService ? recommendationService.selectedArtists || [] : [];
        var tasteItems = tasteArtists.map(function(artist) {
            var provider = String(artist.provider || "");
            var providerId = String(artist.providerId || "");
            var destination = provider === "youtube-music"
                ? (providerId.indexOf("ytm:artist:") === 0 ? providerId : "ytm:artist:" + providerId)
                : provider === "local" ? String(artist.localId || providerId)
                    : String(artist.destination || "");
            var name = String(artist.name || artist.title || "");
            return {id:destination,artistId:destination,kind:"artists",artist:name,title:name,
                cover:String(artist.image || artist.cover || ""),round:true,subtitle:"",
                tasteKey:String(artist.key || ""),ref:recommendationArtistRef(artist)};
        }).filter(function(item) { return !!item.artistId && !!item.title; });
        if(tasteItems.length)result.push({key:"taste-artists",title:"Your artists",kind:"artists",
            profileGeneration:generation,items:tasteItems});
        historyRows.sort((a,b)=>(a.sectionId==="rediscover-library"?0:1)-(b.sectionId==="rediscover-library"?0:1));
        return result.concat(historyRows);
    }
    readonly property var coldShelves: []
    readonly property var shelfKeys: shelves.map(function(r){return r.key})
    readonly property var carouselSlides: {
        if(!canEnterVinyl || tastePickerOpen)return [];
        var result=[];
        var now=Math.max(homeNowMs,Date.now());
        var last=Number(session.lastPlaybackAtMs)||0;
        var unfinished=session.playingIndex>=0 && session.sourceState!=="ended"
            && (!(session.duration>0) || session.position<session.duration-0.5);
        if(unfinished && last>0 && now>=last && now-last<=72*3600000)
            result.push({key:"resume",id:"resume:"+session.playingEditionId,title:session.playingTrack.title,
                blurb:session.playingEdition.artist,art:session.playingEdition.cover || "",artKind:"square",
                cta:session.paused ? "Continue listening" : "Now playing",kicker:"Continue listening"});
        else {
            var recent=recentContinue.find(item=>item.lastPlaybackAtMs>0 && now>=item.lastPlaybackAtMs
                && now-item.lastPlaybackAtMs<=72*3600000 && !item.unavailable);
            if(recent)result.push({key:recent.id,id:recent.id,title:recent.title,blurb:recent.subtitle,
                art:recent.cover,artKind:"square",cta:"Continue listening",kicker:"Continue listening",item:recent});
        }
        var cards=usesRecommendationService ? session.plainData(recommendationService.homeCarouselItems || []) : [];
        cards.forEach(function(item){
            if(result.length>=5)return;
            var kind=String(item.spotlightKind || "");
            result.push({id:String(item.key || item.id),key:String(item.key || item.id),title:item.title,
                blurb:item.subtitle || "",art:item.cover || "",artKind:"square",cta:"Open",
                kicker:kind==="new-from-your-artists" ? "New from your artists" : kind==="related-discoveries" ? "An artist you might like" : "Picked for you",item:item});
        });
        return result.slice(0,5);
    }
    signal itemRequested(var item)
    signal listRequested(string key, bool home)
    signal resumeRequested()
    signal homeDestinationRequested(string tab,string section,string id)
    signal artistSearchFocusRequested()
    function openHomeDestination(tab,section,id) {homeDestinationRequested(tab,section || "albums",id || "");}
    function artwork(url) { return url || ""; }
    function findAlbum(id) {
        var records=libraryAlbums.concat(catalogueMatches.albums);
        if(cataloguePagingSupported)Object.keys(session.cataloguePages).forEach(kind=>{records=records.concat(session.cataloguePages[kind].records || []);});
        discoveryOrder.forEach(key=>{records=records.concat((discoverySections[key] || {}).records || []);});
        return records.find(function(a){return a.id===id;});
    }
    function project(records,kind,remoteArtists) {
        if(remoteArtists && kind==="artists") return (catalogueMatches.query===browseStates.discover.query.trim() ? catalogueMatches.artists : []).map(function(a){return {id:a.id,artistId:a.id,artist:a.name,title:a.name,cover:a.cover || "",kind:"artists",origin:a.origin};});
        if(kind==="artists") {
            var seen={},result=[];
            records.forEach(function(a){a.artists.forEach(function(name,index){
                var id=(a.artistIds || [])[index] || name;
                if(seen[id])return;seen[id]=true;
                result.push({id:id,artistId:(a.artistIds || [])[index] || "",albumId:a.id,kind:"artists",title:name,artist:name,subtitle:"",cover:"",aspect:1,round:true,reason:""});
            });});
            return result;
        }
        // Reuse approved projection semantics, then remove fixture-only display assumptions.
        return Lists.project(catalogue,records,kind).map(function(item) {
            var album=records.find(function(a){return a.id===item.albumId});
            item.subtitle=kind==="albums" ? [item.artist,album.year || ""].filter(function(v){return !!v}).join(" · ") : item.subtitle;
            if(kind==="songs") {var t=album.tracks[item.trackIndex];item.id=album.id+"|"+((album.editions || [])[0] || {}).id+"|"+(t.disc || 1)+"|"+(t.number || item.trackIndex+1)+"|"+item.trackIndex;item.trackId=t.id;}
            return item;
        });
    }
    function row(key,home) {
        if(home) {
            if(homeListSnapshot && homeListSnapshot.key===key) return homeListSnapshot;
            return homeRows.find(function(r){return r.key===key}) || {key:key,title:"Collection",items:[]};
        }
        if(key==="library") return {key:key,title:"Your library",kind:"albums",items:project(filteredLibraryAlbums,"albums")};
        if(key.indexOf("local-")===0) {var kind=key.slice(6),online=browseStates.discover.onlineMode;return {key:key,title:(online ? "Catalogue " : "Local ")+kind,kind:kind,items:project(discoverAlbums,kind,online)};}
        return shelves.find(function(r){return r.key===key}) || {key:key,title:"Collection",items:[]};
    }
    function listItems(key) {return row(key,false).items;}
    function filterOptions(key) {
        var values=[];
        catalogue.albums.forEach(function(a){var value=key==="decade" ? (Number(a.year)>0 ? Math.floor(a.year/10)*10+"s" : "") : a[key];if(value && values.indexOf(value)<0) values.push(value);});
        values.sort();
        return [{value:"",label:"All",enabled:true}].concat(values.map(function(v){var next=Object.assign({},lens);next[key]=v;return {value:v,label:v,enabled:catalogue.albums.some(function(a){return Lists.matches(a,next)})};}));
    }
    function setLens(key,value) {
        if(["genre","decade","country","language"].indexOf(key)<0) return false;
        if(!filterOptions(key).some(function(o){return o.value===value && o.enabled})) return false;
        var next=Object.assign({},lens);next[key]=value;lens=next;return true;
    }
    function setKind(kind) {if(["albums","artists","songs"].indexOf(kind)<0 && !(kind==="playlists" && onlineMode))return false;lens=Object.assign({},lens,{kind:kind});return true;}
    function prepareArtistSearch() {
        if(usesRecommendationService) {
            artistSearchText=String((recommendationService.artistResults || {}).query || "");
        } else fallbackPickerQuery="";
    }
    function setArtistSearchQuery(value) {
        artistSearchText=String(value || "");
        if(usesRecommendationService) {
            recommendationService.searchArtists(artistSearchText.trim(), {});
        } else fallbackPickerQuery=artistSearchText;
    }
    function submitArtistSearch() {
        if(usesRecommendationService) return recommendationService.searchArtists(artistSearchText.trim(), {});
        return true;
    }
    function retryArtistSearch() {
        if(usesRecommendationService) return recommendationService.searchArtists(artistSearchText.trim(), {});
        return false;
    }
    function loadMoreArtistSearch() {
        if(usesRecommendationService && artistSearchHasMore)
            return recommendationService.searchArtists(artistSearchText.trim(), artistResults.nextCursor);
        return false;
    }
    function setPickerQuery(value) {setArtistSearchQuery(value);}
    function togglePickerArtist(artist) {
        if(usesRecommendationService) {
            var ref = artist && artist.artistRef ? artist.artistRef : artist;
            if(ref && typeof ref === "object") recommendationService.toggleDraftArtist(ref);
            return;
        }
        var id = artist && typeof artist === "object" ? artist.id : artist;
        var next=fallbackPickerDraft.slice(),at=next.indexOf(id);
        if(at<0)next.push(id);else next.splice(at,1);
        fallbackPickerDraft=next;
    }
    function artistOptions(value) {
        var rows;
        if(usesRecommendationService) rows=artistResultItems;
        else rows=project(libraryAlbums,"artists").filter(function(a){return a.title.toLowerCase().indexOf(String(value||"").toLowerCase())>=0});
        return rows.map(function(artist) {
            var item=Object.assign({},artist);
            item.id=String(artist.key || artist.id || artist.providerId || "");
            item.key=String(artist.key || item.id);
            item.title=String(artist.name || artist.title || "");
            item.cover=String(artist.image || artist.cover || "");
            item.artistRef=artist;
            return item;
        }).filter(function(item){return !!item.id && !!item.title;});
    }
    function answerPicker(values) {
        if(pickerDraft.length<minimumArtistCount) {
            personalError="Choose at least "+minimumArtistCount+" artists.";return false;
        }
        if(usesRecommendationService) {
            var committed=recommendationService.commitTaste(false);
            if(!committed) personalError=recommendationService.error || "Taste choices could not be saved.";
            else personalError="";
            return committed;
        }
        var valid=artistOptions("").map(function(a){return a.id});
        values=values || fallbackPickerDraft;
        var selected=values.filter(function(v,i){return valid.indexOf(v)>=0 && values.indexOf(v)===i});
        if(selected.length<minimumArtistCount) {
            personalError="Choose at least "+minimumArtistCount+" artists.";return false;
        }
        if(!preferencesStore || !preferencesStore.setVinylTaste(true,selected)) {personalError="Taste choices could not be saved.";return false;}
        fallbackPicks=selected;fallbackPickerAnswered=true;fallbackEditing=false;fallbackPickerDraft=selected.slice();fallbackTastePickerOpen=false; return true;
    }
    function skipPicker() {
        personalError="Choose at least "+minimumArtistCount+" artists.";return false;
    }
    function beginTasteEdit() {
        if(usesRecommendationService) {
            artistSearchText="";
            return recommendationService.beginTasteEdit();
        }
        fallbackPickerDraft=fallbackPicks.slice();fallbackPickerQuery="";
        fallbackEditing=true;fallbackTastePickerOpen=true;return true;
    }
    function cancelTasteEdit() {
        if(usesRecommendationService) return recommendationService.cancelTasteEdit();
        fallbackPickerDraft=fallbackPicks.slice();fallbackPickerQuery="";fallbackEditing=false;fallbackTastePickerOpen=false;return true;
    }
    function resetPicker() {
        return beginTasteEdit();
    }
    function activate(item) {itemRequested(item);}
    function openList(key,home) {
        var sections={"saved-songs":"songs","saved-albums":"albums","saved-artists":"artists","playlists":"playlists"};
        homeListSnapshot=null;
        if(home && key==="taste-artists") {
            if(activeTab!=="home" || !pickerAnswered || tastePickerOpen
                || typeof beginTasteEdit!=="function") return false;
            beginTasteEdit();
            artistSearchFocusRequested();
            return true;
        }
        if(home) {
            var selected=homeRows.find(function(r){return r.key===key;});
            if(selected)
                homeListSnapshot=JSON.parse(JSON.stringify(selected));
        }
        if(home && personalLibrary && sections[key]){openHomeDestination("library",sections[key]);return;}
        listRequested(key,home);
    }
    function resume() {resumeRequested();}
    Connections {
        target:root.session
        ignoreUnknownSignals:true
        function onLibraryChanged(){root.refreshSearch();}
        function onProfileGenerationChanged(){root.recentContinue=[];root.homeSelectionDay="";root.homeListSnapshot=null;root.loadTaste();root.resetBrowseState();}
        function onPlayingTrackChanged(){if(root.activeTab==="home")root.refreshHome();}
        function onPausedChanged(){if(root.activeTab==="home")root.refreshHome();}
        function onQueueChanged(){if(root.activeTab==="home")Qt.callLater(root.refreshHome);}
        function onOnlineAlbumsChanged(){root.acceptCatalogueMatches();}
        function onOnlineArtistsChanged(){root.acceptCatalogueMatches();}
        function onCataloguePagesChanged(){root.acceptCatalogueMatches();}
        function onBrowseOperationChanged(){if(root.session.browseOperation!=="catalogueSearch")root.pendingCatalogueQuery="";}
    }
}
