pragma ComponentBehavior: Bound
import QtQuick
import ".."

Item {
    id: root
    objectName: "vinylCatalogueBrowsePage"
    required property var browseData
    required property var worldData
    required property Item backdrop
    required property Flickable pageFlick
    property string kind: "songs"
    property int currentIndex: browseState.currentIndex
    readonly property var browseState: browseData.page(kind)
    readonly property bool wide: width>=900
    readonly property real contentLeft: wide ? 260 : 0
    readonly property int columns: Math.max(1,Math.floor((width-contentLeft+24)/220))
    readonly property real cardWidth: Math.max(120,(width-contentLeft-(columns-1)*24)/columns)
    implicitHeight: Math.max(controls.y+controls.implicitHeight,results.y+results.implicitHeight,200)
    onBrowseStateChanged: currentIndex=browseState.currentIndex
    function defaultTitle() {return kind==="songs" ? qsTr("Trending songs") : kind==="albums" ? qsTr("New releases") : qsTr("Artists");}
    function selectionTitle() {
        if(!browseState.category)return defaultTitle();
        var selected=browseData.categories.find(function(c){return c.params===root.browseState.category;});
        return selected ? selected.title : defaultTitle();
    }
    function marketTitle(market) {
        var names={ZZ:qsTr("Global"),IN:qsTr("India"),US:qsTr("United States"),GB:qsTr("United Kingdom"),JP:qsTr("Japan"),KR:qsTr("South Korea"),
            AE:qsTr("United Arab Emirates"),AR:qsTr("Argentina"),AT:qsTr("Austria"),AU:qsTr("Australia"),BE:qsTr("Belgium"),BG:qsTr("Bulgaria"),BO:qsTr("Bolivia"),BR:qsTr("Brazil"),
            CA:qsTr("Canada"),CH:qsTr("Switzerland"),CL:qsTr("Chile"),CO:qsTr("Colombia"),CR:qsTr("Costa Rica"),CZ:qsTr("Czechia"),DE:qsTr("Germany"),DK:qsTr("Denmark"),
            DO:qsTr("Dominican Republic"),DZ:qsTr("Algeria"),EC:qsTr("Ecuador"),EE:qsTr("Estonia"),EG:qsTr("Egypt"),ES:qsTr("Spain"),FI:qsTr("Finland"),FR:qsTr("France"),
            GH:qsTr("Ghana"),GR:qsTr("Greece"),GT:qsTr("Guatemala"),HK:qsTr("Hong Kong"),HN:qsTr("Honduras"),HR:qsTr("Croatia"),HU:qsTr("Hungary"),ID:qsTr("Indonesia"),
            IE:qsTr("Ireland"),IL:qsTr("Israel"),IS:qsTr("Iceland"),IT:qsTr("Italy"),JO:qsTr("Jordan"),KE:qsTr("Kenya"),KW:qsTr("Kuwait"),LB:qsTr("Lebanon"),
            LK:qsTr("Sri Lanka"),LT:qsTr("Lithuania"),LU:qsTr("Luxembourg"),LV:qsTr("Latvia"),LY:qsTr("Libya"),MA:qsTr("Morocco"),MT:qsTr("Malta"),MX:qsTr("Mexico"),MY:qsTr("Malaysia"),
            NG:qsTr("Nigeria"),NI:qsTr("Nicaragua"),NL:qsTr("Netherlands"),NO:qsTr("Norway"),NP:qsTr("Nepal"),NZ:qsTr("New Zealand"),PA:qsTr("Panama"),PE:qsTr("Peru"),
            PH:qsTr("Philippines"),PK:qsTr("Pakistan"),PL:qsTr("Poland"),PR:qsTr("Puerto Rico"),PT:qsTr("Portugal"),PY:qsTr("Paraguay"),QA:qsTr("Qatar"),RO:qsTr("Romania"),RS:qsTr("Serbia"),
            RU:qsTr("Russia"),SA:qsTr("Saudi Arabia"),SE:qsTr("Sweden"),SG:qsTr("Singapore"),SI:qsTr("Slovenia"),SK:qsTr("Slovakia"),SN:qsTr("Senegal"),SV:qsTr("El Salvador"),
            TH:qsTr("Thailand"),TN:qsTr("Tunisia"),TR:qsTr("Turkey"),TW:qsTr("Taiwan"),TZ:qsTr("Tanzania"),UA:qsTr("Ukraine"),UG:qsTr("Uganda"),UY:qsTr("Uruguay"),
            VE:qsTr("Venezuela"),VN:qsTr("Vietnam"),ZA:qsTr("South Africa"),ZW:qsTr("Zimbabwe")};
        return names[market.code] || (market.title!==market.code ? market.title : "");
    }
    function focusCurrent() {
        var card=cards.itemAt(Math.max(0,Math.min(cards.count-1,browseState.currentIndex)));
        if(card)card.forceActiveFocus(Qt.TabFocusReason);
        else {
            var picker=kind==="artists" ? market : categoryPicker;
            var action=picker.nextItemInFocusChain(true);
            if(action && action.visible && action.enabled)action.forceActiveFocus(Qt.TabFocusReason);
        }
    }
    Keys.onPressed:event=>nav.handle(event)
    KeyboardCollectionController {
        id:nav;view:root;count:cards.count;orientation:"grid";columns:root.columns
        positionIndexFn:function(index){
            var card=cards.itemAt(index);if(!card)return;
            root.browseData.update(root.kind,{currentIndex:index,selectedId:String(root.browseState.items[index].id || "")});
            var top=card.mapToItem(root.pageFlick.contentItem,0,0).y;
            root.pageFlick.contentY=Math.max(0,Math.min(Math.max(0,root.pageFlick.contentHeight-root.pageFlick.height),top-80));
            card.forceActiveFocus(Qt.TabFocusReason);
        }
        onActivated:index=>root.worldData.activate(root.browseState.items[index])
    }
    Theme {id:theme}
    Column {
        id:controls;width:root.wide ? 230 : parent.width;spacing:16
        Text {text:qsTr("YouTube Music");color:theme.gold;font.family:theme.ui;font.pixelSize:14}
        Text {width:parent.width;text:root.kind==="artists" ? qsTr("Artist charts") : qsTr("Browse collections");color:theme.ink;font.family:theme.ui;font.pixelSize:20;wrapMode:Text.WordWrap}
        DiscoverPicker {
            id:categoryPicker;objectName:"vinylCatalogueCategory";width:parent.width
            visible:root.kind!=="artists";label:root.defaultTitle();clearable:false;openOnDown:true
            options:[{key:"",text:root.defaultTitle()}].concat(root.browseData.categories.map(function(c){return {key:c.params,text:c.title,sub:c.group || ""};}))
            currentKey:root.browseState.category
            onPicked:key=>root.browseData.select(root.kind,key,"")
        }
        Text {width:parent.width;visible:root.kind!=="artists";text:qsTr("Curated collections from YouTube.");color:theme.inkDim;font.family:theme.ui;font.pixelSize:12;wrapMode:Text.WordWrap}
        DiscoverPicker {
            id:market;objectName:"vinylCatalogueMarket";visible:root.kind==="artists";width:parent.width
            label:qsTr("Market");clearable:false;openOnDown:true
            options:root.browseData.markets.map(function(m){return {key:m.code,text:root.marketTitle(m)};}).filter(function(m){return !!m.text;})
            currentKey:root.browseState.region
            onPicked:key=>root.browseData.select("artists","",key)
        }
        Text {visible:root.kind==="artists";width:parent.width;text:root.browseState.region==="ZZ" ? qsTr("Global selection") : qsTr("Selected market: %1").arg(root.marketTitle({code:root.browseState.region,title:root.browseState.region}));color:theme.inkDim;font.family:theme.ui;font.pixelSize:12;wrapMode:Text.WordWrap}
        Text {width:parent.width;visible:root.kind!=="artists" && !!root.browseData.categoriesError;text:root.browseData.categoriesError;color:theme.inkDim;font.family:theme.ui;font.pixelSize:12;wrapMode:Text.WordWrap}
        VinylWorldButton {visible:root.kind!=="artists" && !!root.browseData.categoriesError;label:qsTr("Retry collections");symbol:"retry";backdrop:root.backdrop;onTriggered:root.browseData.requestCategories(true)}
    }
    Column {
        id:results;x:root.contentLeft;y:root.wide ? 0 : controls.implicitHeight+24;width:parent.width-x;spacing:24
        Text {width:parent.width;text:root.selectionTitle();color:theme.ink;font.family:theme.ui;font.pixelSize:24;wrapMode:Text.WordWrap}
        Text {width:parent.width;visible:root.browseState.loading || !!root.browseState.error || (root.browseState.loaded && !root.browseState.items.length);text:root.browseState.loading ? qsTr("Loading music…") : root.browseState.error || qsTr("This selection has no music available.");color:theme.inkDim;font.family:theme.ui;font.pixelSize:14;wrapMode:Text.WordWrap}
        VinylWorldButton {visible:!!root.browseState.error;enabled:!root.browseState.loading;label:qsTr("Retry");symbol:"retry";backdrop:root.backdrop;onTriggered:root.browseData.retry(root.kind)}
        Grid {
            width:parent.width;columns:root.columns;columnSpacing:24;rowSpacing:30
            Repeater {
                id:cards;model:root.browseState.items
                delegate:VinylItemCard {
                    id:card
                    required property var modelData
                    required property int index
                    objectName:"vinylCatalogueCard_"+index;width:root.cardWidth
                    item:modelData;worldData:root.worldData;backdrop:root.backdrop;pageFlick:root.pageFlick
                    Connections {target:card;function onTriggered(){root.browseData.update(root.kind,{currentIndex:card.index,selectedId:String(card.modelData.id || "")});}}
                    onActiveFocusChanged:if(activeFocus)root.browseData.update(root.kind,{currentIndex:index,selectedId:String(modelData.id || "")})
                }
            }
        }
        VinylWorldButton {objectName:"vinylCatalogueMore";visible:root.browseState.loaded && !root.browseState.complete;enabled:!root.browseState.loading;label:qsTr("Load more");backdrop:root.backdrop;onTriggered:root.browseData.fetch(root.kind,true,false)}
    }
}
