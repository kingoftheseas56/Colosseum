.pragma library

function options(worldData, item, recommendationRow) {
    var options=[];
    var artistFeedbackRow=recommendationRow.key==="taste-artists"
        || recommendationRow.key==="saved-artists";
    if(artistFeedbackRow) {
        var artistName=String(item.artist || item.title || "this artist");
        options.push({key:"exclude-current-artist",action:"exclude-artist",
            label:"Show less from "+artistName,ref:item.ref,section:"artists",subject:artistName});
        options.push({key:"allow-current-artist",action:"allow-artist",
            label:"Allow "+artistName+" again",ref:item.ref,section:"artists",subject:artistName});
        var suppressed=typeof worldData.suppressedRecommendationsForArtist==="function"
            ? worldData.suppressedRecommendationsForArtist(item.ref) : [];
        var suppressedCount=suppressed && Number.isInteger(Number(suppressed.length))
            ? Math.min(300,Number(suppressed.length)) : 0;
        for(var suppressedIndex=0;suppressedIndex<suppressedCount && options.length<300;suppressedIndex++) {
            var entry=suppressed[suppressedIndex];
            if(!entry || !entry.ref) continue;
            var suppressedTitle=String(entry.title || "this recommendation").trim();
            if(entry.hidden===true && options.length<300)
                options.push({key:"unhide-suppressed-"+suppressedIndex,action:"unhide",
                    label:"Show "+suppressedTitle+" again",ref:entry.ref,section:"hidden",subject:suppressedTitle});
            if(entry.snoozed===true && options.length<300)
                options.push({key:"unsnooze-suppressed-"+suppressedIndex,action:"unsnooze",
                    label:"Stop hiding "+suppressedTitle+" for 7 days",ref:entry.ref,section:"hidden",subject:suppressedTitle});
            if(entry.excluded!==true || options.length>=300) continue;
            var excludedArtists=Array.isArray(entry.excludedArtistRefs) ? entry.excludedArtistRefs : [];
            var validExcludedCount=0;
            for(var excludedIndex=0;excludedIndex<excludedArtists.length && options.length<300;excludedIndex++) {
                var excluded=excludedArtists[excludedIndex];
                if(!excluded || !excluded.ref || excluded.ref.identity!=="provider"
                    || excluded.ref.kind!=="artist" || !excluded.ref.providerId || !excluded.ref.itemId) continue;
                var excludedName=String(excluded.name || "").trim();
                if(!excludedName && typeof worldData.recommendationRefsEqual==="function"
                    && worldData.recommendationRefsEqual(excluded.ref,item.ref))
                    excludedName=artistName;
                if(!excludedName) continue;
                validExcludedCount++;
                options.push({key:"allow-excluded-artist-"+suppressedIndex+"-"+excludedIndex,
                    action:"allow-artist",label:"Allow "+excludedName+" again (from "+suppressedTitle+")",
                    ref:excluded.ref,section:"hidden",subject:excludedName+" \u00b7 "+suppressedTitle});
            }
            if(validExcludedCount===0 && entry.ref.kind==="artist" && options.length<300) {
                var excludedRefName=String(entry.title || artistName).trim();
                options.push({key:"allow-excluded-artist-"+suppressedIndex+"-0",
                    action:"allow-artist",label:"Allow "+excludedRefName+" again (from "+suppressedTitle+")",
                    ref:entry.ref,section:"hidden",subject:excludedRefName+" \u00b7 "+suppressedTitle});
            }
        }
    } else {
        options=[
            {key:"like",action:"like",label:"Like this recommendation",ref:item.ref},
            {key:"dislike",action:"dislike",label:"Dislike this recommendation",ref:item.ref},
            {key:"neutral",action:"neutral",label:"Clear feedback",ref:item.ref},
            {key:"hide",action:"hide",label:"Hide this recommendation",ref:item.ref},
            {key:"unhide",action:"unhide",label:"Show this recommendation again",ref:item.ref},
            {key:"snooze-7d",action:"snooze-7d",label:"Hide for 7 days",ref:item.ref},
            {key:"unsnooze",action:"unsnooze",label:"Stop hiding for 7 days",ref:item.ref}
        ];
    }
    var artists=item ? item.artistRefs : null;
    var artistCount=!artistFeedbackRow && artists && Number.isInteger(Number(artists.length))
        ? Number(artists.length) : 0;
    for(var index=0;index<artistCount;index++) {
        var artist=artists[index];
        var ref=worldData.recommendationArtistRef(artist);
        if(!ref) continue;
        var name=String(artist.name || artist.title || "").trim();
        if(!name) continue;
        options.push({key:"exclude-artist-"+index,action:"exclude-artist",
            label:"Show less from "+name,ref:ref,section:"artists",subject:name});
        options.push({key:"allow-artist-"+index,action:"allow-artist",
            label:"Allow "+name+" again",ref:ref,section:"artists",subject:name});
    }
    return options;
}

function groups(options) {
    var result=[];
    options.forEach(function(option) {
        var section=option.section || "item";
        var key=section+":"+String(option.subject || "")+":"+JSON.stringify(option.ref);
        var group=result.find(function(candidate) { return candidate.key===key; });
        if(!group) {
            group={key:key,section:section,title:option.subject || "",actions:[]};
            result.push(group);
        }
        group.actions.push(option);
    });
    return result;
}
function recoveryOptions(entry) {
    var result=[];
    if(entry.hidden) result.push({key:"unhide",action:"unhide",label:"Show this recommendation again",ref:entry.ref});
    if(entry.snoozed) result.push({key:"unsnooze",action:"unsnooze",label:"Stop hiding for 7 days",ref:entry.ref});
    (entry.excludedArtistRefs || []).forEach(function(artist,index) {
        if(artist && artist.ref && artist.ref.identity==="provider" && artist.ref.kind==="artist"
            && artist.ref.providerId && artist.ref.itemId)
            result.push({key:"allow-artist-"+index,action:"allow-artist",ref:artist.ref,
                label:"Allow "+String(artist.name || entry.title || "this artist")+" again"});
    });
    if(entry.excluded && entry.ref.kind==="artist" && !result.some(function(option){return option.action==="allow-artist";}))
        result.push({key:"allow-artist",action:"allow-artist",ref:entry.ref,
            label:"Allow "+String(entry.title || "this artist")+" again"});
    return result;
}
function symbol(action) {
    return ({like:"check",dislike:"cancel",neutral:"reset",hide:"hide",unhide:"show",
        "snooze-7d":"snooze",unsnooze:"restore","exclude-artist":"artistLess","allow-artist":"artistAllow"})[action] || "reset";
}
