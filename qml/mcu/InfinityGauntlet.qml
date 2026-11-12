import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "Catalogue.js" as Catalogue

Item {
    id: root
    objectName: "tribute"
    focus: true
    // Inside Colosseum the app's own back button and chrome replace the tribute's brand bar
    // and its "← The gauntlet" button (they collided with the phase eyebrow).
    property bool embedded: false
    property int activePhase: 0
    property string activeFilter: "all"
    property string query: ""
    property int hoveredPhase: 0
    readonly property color hoverTint: hoveredPhase ? Catalogue.stones[hoveredPhase-1].c : "#b49d58"
    property var selectedWork: ({})
    property var phase: activePhase ? Catalogue.phases[activePhase-1] : Catalogue.phases[0]
    readonly property color gold: "#f0c44a"
    readonly property color ink: "#f7f7f5"
    readonly property color muted: "#9a99a5"
    readonly property real side: width < 700 ? 24 : 64
    readonly property bool narrow: width < 700
    readonly property string bodyFont: switzer.name
    readonly property string titleFont: fraunces.name
    FontLoader { id: switzer; source: "assets/Switzer-400.otf" }
    FontLoader { source: "assets/Switzer-600.otf" }
    FontLoader { id: fraunces; source: "assets/Fraunces-400.ttf" }
    function groups(p) {
        return [{id:"movies",name:"Films",items:p.screen.filter(x=>x.b==="MOVIE")},
                {id:"series",name:"Series",items:p.screen.filter(x=>x.b.indexOf("SERIES")===0)},
                {id:"specials",name:"Specials",items:p.screen.filter(x=>x.b!=="MOVIE"&&x.b.indexOf("SERIES")!==0)},
                {id:"adjacent",name:"Adjacent releases",items:p.adjacent},
                {id:"comics",name:"Comics",items:p.comics}].filter(g=>g.items.length>0)
    }
    function filteredGroups() {
        return groups(phase).filter(g=>activeFilter==="all"||activeFilter===g.id).map(g=>({id:g.id,name:g.name,items:g.items.filter(x=>x.t.toLowerCase().indexOf(query.trim().toLowerCase())!==-1)})).filter(g=>g.items.length>0)
    }
    function years(p) { return p.years.replace(/-/g,"–")+(p.years.endsWith("-")?"onward":"") }
    function go(n) { hoveredPhase=0; activePhase=n; if(n>0) phaseEnter.restart(); activeFilter="all"; query=""; search.text=""; phaseScroll.contentY=0; forceActiveFocus() }
    // Inside Colosseum a card opens the real page: a film or series opens Theatre, and a comic
    // opens its comic page (by its pinned GetComics post, or by title when it has none).
    // The standalone preview keeps its details dialog.
    signal watchRequested(var item)
    signal comicsArchiveRequested(var entry)
    signal comicTitleRequested(var entry)
    function openWork(x) {
        if (embedded) {
            if (x.comic) {
                var year = ((x.url||"").match(/-(\d{4})\/?$/)||[])[1] || ""
                if (x.posts && x.posts.length) comicsArchiveRequested({ title:x.t, year:year, posts:x.posts, cover:"" })
                else comicTitleRequested({ title:x.t })
                return
            }
            if (x.id && /^tt\d+$/.test(x.id)) {
                watchRequested({ id:x.id, title:x.t, type:x.b==="MOVIE"?"movie":"series",
                                 cover:"https://images.metahub.space/poster/medium/"+x.id+"/img" })
                return
            }
        }
        selectedWork=x; work.open()
    }
    Keys.onPressed: event => {
        if(work.opened||about.opened) return
        if(event.key>=Qt.Key_1&&event.key<=Qt.Key_6) { go(event.key-Qt.Key_0); event.accepted=true }
        else if(event.key===Qt.Key_Escape&&activePhase) { go(0); event.accepted=true }
        else if(event.key===Qt.Key_Left&&activePhase>0&&activePhase<6) { go(activePhase+1); event.accepted=true }
        else if(event.key===Qt.Key_Right&&activePhase>1) { go(activePhase-1); event.accepted=true }
    }
    component LabelText: Text { color: root.ink; font.family: root.bodyFont; font.pixelSize: 13; textFormat: Text.PlainText }
    component PlainButton: Button {
        id: b
        hoverEnabled:true
        font.family: root.bodyFont; font.pixelSize: 13
        contentItem: Text { text: b.text; color: b.highlighted ? "#17120a" : root.ink; font: b.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 12; color: b.highlighted ? root.gold : b.hovered ? "#25ffffff" : "transparent"; border.width:b.activeFocus?2:0; border.color:root.gold }
    }
    component Cover: Rectangle {
        id:cover; property var entry: ({})
        color:"#161a21"; clip:true
        LabelText { x:14;y:14;width:parent.width-28;text:cover.entry.comic?"MARVEL / COMICS":"MARVEL / SCREEN";font.pixelSize:10;font.letterSpacing:1;color:root.muted;wrapMode:Text.WordWrap }
        LabelText { x:14;anchors.bottom:parent.bottom;anchors.bottomMargin:14;width:parent.width-28;text:cover.entry.t||"";font.family:root.titleFont;font.pixelSize:16;color:"#c9c8d0";wrapMode:Text.WordWrap }
        Image { anchors.fill:parent;source:cover.entry.poster||"";fillMode:Image.PreserveAspectCrop;visible:status===Image.Ready }
    }
    Rectangle { anchors.fill: parent; color: "#0c0e11" }
    Item {
        anchors.fill: parent; visible: root.activePhase===0
        Rectangle { width: parent.width; height: root.narrow?64:72; color:"#0c0e11"
            Rectangle { anchors.bottom:parent.bottom; width:parent.width; height:1; color:"#10ffffff" }
            Row { x:root.side; anchors.verticalCenter:parent.verticalCenter; spacing:10; visible:!root.embedded
                Image { source:"assets/logo.svg"; width:38; height:38 }
                LabelText { text:"COLOSSEUM"; font.pixelSize:12; font.letterSpacing:2.5; font.weight:Font.DemiBold; anchors.verticalCenter:parent.verticalCenter }
            }
            PlainButton { anchors.right:parent.right; anchors.rightMargin:root.side; anchors.verticalCenter:parent.verticalCenter; width:38; height:38; text:"↗"; onClicked:about.open(); Accessible.name:"About this tribute"
                background: Rectangle { radius:19; color:parent.hovered?"#14ffffff":"transparent"; border.color:"#40ffffff" }
            }
        }
        Item {
            y:root.narrow?64:72; width:parent.width; height:parent.height-y
            Canvas {
                id:aureole; objectName:"hoverTint"
                width:Math.min(parent.width*.85,1050); height:width; anchors.centerIn:parent
                property color tint: root.hoverTint
                Behavior on tint { ColorAnimation { duration:240 } }
                onTintChanged:requestPaint()
                onWidthChanged:requestPaint()
                onPaint: { var c=getContext("2d");c.clearRect(0,0,width,height);var g=c.createRadialGradient(width/2,height/2,0,width/2,height/2,width/2);g.addColorStop(0,Qt.rgba(tint.r,tint.g,tint.b,root.hoveredPhase?.19:.095));g.addColorStop(1,"transparent");c.fillStyle=g;c.fillRect(0,0,width,height) }
            }
            Item {
                id: gauntlet
                anchors.centerIn:parent
                width:Math.min(660,parent.width*.93,Math.max(200,root.height-80)*.535)
                height:width*985/525
                Image { anchors.fill:parent; source:"assets/gauntlet.webp"; fillMode:Image.PreserveAspectFit; smooth:true }
                Repeater {
                    model:Catalogue.stones
                    delegate: Button {
                        id:gem; hoverEnabled:true; required property var modelData
                        objectName:"gem"+modelData.p
                        x:gauntlet.width*(modelData.x-modelData.w/2)/100
                        y:gauntlet.height*(modelData.y-modelData.h/2)/100
                        width:gauntlet.width*modelData.w/100; height:gauntlet.height*modelData.h/100
                        Accessible.name:"Open Phase "+Catalogue.phases[modelData.p-1].roman
                        onClicked:root.go(modelData.p)
                        contentItem:Item {}
                        background:Item {}
                        onHoveredChanged: { if(hovered) root.hoveredPhase=modelData.p; else if(!activeFocus && root.hoveredPhase===modelData.p) root.hoveredPhase=0 }
                        onActiveFocusChanged: { if(activeFocus) root.hoveredPhase=modelData.p; else if(!hovered && root.hoveredPhase===modelData.p) root.hoveredPhase=0 }
                        Rectangle {
                            objectName:"phaseLabel"+gem.modelData.p
                            anchors.horizontalCenter:parent.horizontalCenter;anchors.bottom:parent.top;anchors.bottomMargin:9
                            width:phaseLabel.implicitWidth+24;height:phaseLabel.implicitHeight+14;radius:8;color:"#ee0c0e11";border.color:"#40ffffff"
                            opacity:gem.hovered||gem.activeFocus?1:0
                            Behavior on opacity { NumberAnimation {duration:180} }
                            LabelText { id:phaseLabel;anchors.centerIn:parent;text:"PHASE "+Catalogue.phases[gem.modelData.p-1].roman;font.pixelSize:11;font.letterSpacing:1.1 }
                        }
                    }
                }
            }
        }
    }
    ParallelAnimation {
        id:phaseEnter
        NumberAnimation {target:phasePage;property:"opacity";from:0;to:1;duration:450;easing.type:Easing.OutCubic}
        NumberAnimation {target:phaseShift;property:"y";from:8;to:0;duration:450;easing.type:Easing.OutCubic}
    }
    Item {
        id:phasePage; objectName:"phasePage"
        anchors.fill:parent; visible:root.activePhase>0
        transform:Translate { id:phaseShift }
        // The tribute's own dark base (Hemanth 2026-11-09: no shell wallpaper here), under a
        // wide backdrop from the film that defines each phase (assets/BACKDROPS.txt). The art
        // sits right and fades left and down, so the title and the glass stay legible.
        Item {
            id:wall; anchors.fill:parent
            Rectangle {anchors.fill:parent;color:"#0c0e11"}
            Image {
                objectName:"phaseBackdrop"
                anchors.right:parent.right
                width:Math.max(parent.width*(root.narrow?1:.82),parent.height*0.95*16/9);height:width*9/16
                source:root.activePhase?"assets/backdrop-"+root.activePhase+".jpg":""
                fillMode:Image.PreserveAspectCrop;asynchronous:true;opacity:.72
            }
            Rectangle {anchors.fill:parent;gradient:Gradient {orientation:Gradient.Horizontal;GradientStop{position:0;color:"#f50c0e11"}GradientStop{position:.35;color:"#c80c0e11"}GradientStop{position:.7;color:"#400c0e11"}GradientStop{position:1;color:"#5a0c0e11"}}}
            Rectangle {anchors.fill:parent;gradient:Gradient {GradientStop{position:0;color:"#660c0e11"}GradientStop{position:.22;color:"transparent"}GradientStop{position:.62;color:"#a00c0e11"}GradientStop{position:.95;color:"#0c0e11"}}}
        }
        PlainButton {z:20;x:root.side;y:24;visible:!root.embedded;text:"←  The gauntlet";onClicked:root.go(0)}
        Glass {z:20;anchors.right:parent.right;anchors.rightMargin:root.side;y:22;width:phaseSwitch.width+8;height:46;radius:23;backdrop:wall
            Row {id:phaseSwitch;anchors.centerIn:parent;spacing:2;layoutDirection:Qt.RightToLeft
                // Each numeral wears its own stone's colour (Hemanth 2026-11-09): the active phase
                // is filled with the stone, the others carry it in their text.
                Repeater {model:6;delegate:PlainButton {id:phaseTab;required property int index;width:root.narrow?26:36;height:36;text:Catalogue.phases[index].roman;onClicked:root.go(index+1);Accessible.name:"Phase "+text
                    readonly property color stone:Catalogue.stones[index].c
                    readonly property bool current:root.activePhase===index+1
                    topPadding:0;bottomPadding:0
                    contentItem:LabelText{text:phaseTab.text;color:phaseTab.current?"#090c13":phaseTab.stone;font.weight:Font.DemiBold;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter}
                    background:Rectangle{radius:18;color:phaseTab.current?phaseTab.stone:phaseTab.hovered?Qt.rgba(phaseTab.stone.r,phaseTab.stone.g,phaseTab.stone.b,.18):"transparent"}
                }}
            }
        }
        Flickable {
            id:phaseScroll;objectName:"phaseScroll";anchors.fill:parent;clip:true
            contentWidth:width; contentHeight:pageColumn.height; boundsBehavior:Flickable.StopAtBounds
            ScrollBar.vertical:HouseScrollBar {flick:phaseScroll}
            Column {
                id:pageColumn;width:phaseScroll.width
                Item {
                    width:parent.width;height:root.width<800?420:400
                    Column {x:root.side;y:120;width:parent.width-root.side*2;spacing:18
                        LabelText{text:root.phase.saga;color:"#c9c8d0";font.pixelSize:11;font.letterSpacing:3}
                        LabelText{text:"Phase "+root.phase.roman;font.family:root.titleFont;font.pixelSize:root.width<800?40:72}
                        Flow {width:parent.width;spacing:8
                            Repeater {model:root.groups(root.phase);delegate:Rectangle {required property var modelData;width:factText.implicitWidth+28;height:30;radius:15;color:"#19ffffff";border.color:"#2effffff"
                                LabelText{id:factText;anchors.centerIn:parent;text:modelData.items.length+" "+modelData.name;color:"#c9c8d0";font.pixelSize:12}
                            }}
                            Rectangle {width:yearText.implicitWidth+28;height:30;radius:15;color:"#19ffffff";border.color:"#2effffff"
                                LabelText{id:yearText;anchors.centerIn:parent;text:root.years(root.phase);color:"#c9c8d0";font.pixelSize:12}
                            }
                        }
                    }
                }
                Column {
                    x:root.side;width:parent.width-root.side*2;spacing:44;bottomPadding:96
                    Flow {width:parent.width;spacing:16
                        Glass {width:Math.min(filterRow.implicitWidth+8,parent.width);height:46;radius:23;backdrop:wall;track:phaseScroll.contentY
                            Flickable {anchors.fill:parent;anchors.margins:4;contentWidth:filterRow.width;clip:true;boundsBehavior:Flickable.StopAtBounds
                                Row {id:filterRow;spacing:2
                                    Repeater {model:[{id:"all",name:"Everything",items:[]}].concat(root.groups(root.phase));delegate:PlainButton {
                                        id:filterButton;required property var modelData;objectName:"filter_"+modelData.id;height:36;width:filterText.implicitWidth+36
                                        onClicked:root.activeFilter=modelData.id
                                        // A plain Text, not a Row: a Control stretches its contentItem to
                                        // the pill's height, which left a Row's label stuck at the top.
                                        topPadding:0;bottomPadding:0
                                        contentItem:LabelText{id:filterText;text:filterButton.modelData.name;color:root.activeFilter===filterButton.modelData.id?"#090c13":"#c9c8d0";font.pixelSize:13;font.weight:Font.Medium;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter}
                                        background:Rectangle{radius:18;color:root.activeFilter===filterButton.modelData.id?root.ink:filterButton.hovered?"#19ffffff":"transparent"}
                                    }}
                                }
                            }
                        }
                        Item {width:Math.max(0,parent.width-filterRow.implicitWidth-8-310-32);height:46;visible:!root.narrow&&width>0}
                        TextField {id:search;objectName:"search";width:root.narrow?parent.width:Math.min(310,parent.width);height:46;placeholderText:"Find a title in this phase";leftPadding:40;rightPadding:35;color:root.ink;placeholderTextColor:root.muted;font.family:root.bodyFont;font.pixelSize:13;onTextChanged:root.query=text
                            background:Glass{radius:23;backdrop:wall;track:phaseScroll.contentY}
                            Canvas {x:14;anchors.verticalCenter:parent.verticalCenter;width:17;height:17;onPaint:{var c=getContext("2d");c.clearRect(0,0,17,17);c.strokeStyle="#c9c8d0";c.lineWidth=1.5;c.beginPath();c.arc(7,7,5,0,Math.PI*2);c.moveTo(11,11);c.lineTo(16,16);c.stroke()}}
                            PlainButton{objectName:"clearSearch";anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter;width:32;height:36;text:"×";font.pixelSize:18;Accessible.name:"Clear search";onClicked:{search.text="";search.forceActiveFocus()}}
                        }
                    }
                    Flow {
                        width:parent.width;spacing:24
                        Repeater {
                            model:root.filteredGroups()
                            delegate:Glass {
                                id:panel;required property var modelData
                                width:Math.min(parent.width,Math.max(240,48+modelData.items.length*164+Math.max(0,modelData.items.length-1)*16))
                                height:382;radius:18;backdrop:wall;track:phaseScroll.contentY
                                Row{x:24;y:20;spacing:12
                                    LabelText{text:panel.modelData.name;font.family:root.titleFont;font.pixelSize:22}
                                    LabelText{text:panel.modelData.items.length;color:root.muted;font.pixelSize:12;anchors.baseline:parent.children[0].baseline}
                                }
                                ListView {
                                    id:shelf;objectName:"shelf_"+panel.modelData.id;x:24;y:67;width:parent.width-24;height:293;orientation:ListView.Horizontal;spacing:16;clip:true;model:panel.modelData.items;boundsBehavior:Flickable.StopAtBounds;rightMargin:24
                                    delegate:Item {
                                        id:card;required property var modelData;width:164;height:293
                                        Rectangle {
                                            id:poster;objectName:"poster_"+card.modelData.t;width:164;height:246;radius:3;color:"#221c30";border.width:cardHover.hovered||posterKey.activeFocus?2:1;border.color:cardHover.hovered||posterKey.activeFocus?"#b3f0c44a":"#1ff7f7f5"
                                            Item{anchors.fill:parent;anchors.margins:1;clip:true
                                                LabelText{anchors.centerIn:parent;width:parent.width-24;text:card.modelData.t;font.family:root.titleFont;font.pixelSize:16;wrapMode:Text.WordWrap;horizontalAlignment:Text.AlignHCenter;visible:posterArt.status!==Image.Ready;color:"#c9c8d0"}
                                                Image{id:posterArt;objectName:"posterArt_"+card.modelData.t;anchors.fill:parent;source:card.modelData.poster||"";fillMode:Image.PreserveAspectCrop;sourceSize.width:400;visible:status===Image.Ready}
                                            }
                                            HoverHandler{id:cardHover}
                                            MouseArea{anchors.fill:parent;cursorShape:Qt.PointingHandCursor;onClicked:root.openWork(card.modelData)}
                                            Item{id:posterKey;anchors.fill:parent;activeFocusOnTab:true;Keys.onReturnPressed:root.openWork(card.modelData);Keys.onSpacePressed:root.openWork(card.modelData);Accessible.role:Accessible.Button;Accessible.name:"Details for "+card.modelData.t}
                                        }
                                        LabelText{y:256;width:164;height:34;text:card.modelData.t;wrapMode:Text.WordWrap;maximumLineCount:2;elide:Text.ElideRight;font.pixelSize:12;lineHeight:1.35}
                                    }
                                    // Actual Universe shelves page with chevrons, not a native Windows track.
                                }
                                PlainButton {anchors.left:parent.left;y:70;width:28;height:240;text:"‹";font.pixelSize:26;visible:shelf.contentX>8;onClicked:shelf.contentX=Math.max(0,shelf.contentX-720)}
                                PlainButton {anchors.right:parent.right;y:70;width:28;height:240;text:"›";font.pixelSize:26;visible:shelf.contentX+shelf.width<shelf.contentWidth-8;onClicked:shelf.contentX=Math.min(shelf.contentWidth-shelf.width,shelf.contentX+720)}
                            }
                        }
                    }
                    LabelText{visible:root.filteredGroups().length===0;text:"No titles found";font.family:root.titleFont;font.pixelSize:25}
                    // One way onward: the next phase, or the gauntlet after Phase VI. (A second
                    // "The gauntlet" link duplicated it; the back button already returns there.)
                    PlainButton{text:root.activePhase<6?"←  Phase "+Catalogue.phases[root.activePhase].roman:"Back to the gauntlet";onClicked:root.go(root.activePhase<6?root.activePhase+1:0);font.family:root.titleFont;font.pixelSize:20}
                }
            }
        }
    }
    Popup {
        id:work; objectName:"workDialog"; parent:Overlay.overlay; anchors.centerIn:parent; width:Math.min(root.width-32,720);height:Math.min(root.height-40,440);modal:true;focus:true;padding:32;closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        background:Rectangle {color:"#11141a";radius:18;border.color:"#50ffffff"}
        Overlay.modal:Rectangle {color:"#dd040509"}
        PlainButton {z:2;anchors.right:parent.right;anchors.top:parent.top;width:32;height:32;text:"×";onClicked:work.close();Accessible.name:"Close details"}
        Row {anchors.fill:parent;spacing:28
            Cover {width:root.narrow?100:220;height:width*1.5;entry:root.selectedWork}
            Flickable {width:parent.width-parent.children[0].width-28;height:parent.height;contentHeight:workCopy.height;clip:true
                Column {id:workCopy;width:parent.width;spacing:16
                    LabelText {width:parent.width;wrapMode:Text.WordWrap;text:(root.selectedWork.kind||"")+" / PHASE "+root.phase.roman;color:root.gold;font.pixelSize:12}
                    LabelText {width:parent.width;wrapMode:Text.WordWrap;text:root.selectedWork.t||"";font.family:root.titleFont;font.pixelSize:root.narrow?23:32}
                    LabelText {width:parent.width;wrapMode:Text.WordWrap;text:root.phase.saga+" · "+root.years(root.phase);color:"#c9c8d0"}
                    LabelText {width:parent.width;wrapMode:Text.WordWrap;font.pixelSize:11;color:root.muted;text:root.selectedWork.note||(root.selectedWork.comic?(root.selectedWork.url?"A comic reference retained from the original mock. The source opens in your browser.":"This entry has no verified source link in the original mock. No reader or source link is invented here."):"This is a catalogue preview. Playback belongs in the Colosseum app; the link opens this title’s IMDb page.")}
                    PlainButton {objectName:"referenceLink";visible:!!root.selectedWork.url;text:root.selectedWork.comic?"Original source ↗":"View on IMDb ↗";onClicked:Qt.openUrlExternally(root.selectedWork.url)}
                }
            }
        }
    }
    Popup {
        id:about;objectName:"aboutDialog";parent:Overlay.overlay;anchors.centerIn:parent;width:Math.min(root.width-32,600);height:Math.min(root.height-40,460);modal:true;focus:true;padding:32;closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        background:Rectangle{color:"#11141a";radius:18;border.color:"#50ffffff"}
        Overlay.modal:Rectangle{color:"#dd040509"}
        PlainButton {z:2;anchors.right:parent.right;anchors.top:parent.top;text:"×";width:32;height:32;onClicked:about.close()}
        Flickable {anchors.fill:parent;contentHeight:aboutCopy.height;clip:true
            Column {id:aboutCopy;width:parent.width;spacing:20
                LabelText {text:"COLOSSEUM / TRIBUTES";color:root.gold;font.letterSpacing:3}
                LabelText {text:"A universe in your hand.";width:parent.width;wrapMode:Text.WordWrap;font.family:root.titleFont;font.pixelSize:32}
                LabelText {width:parent.width;wrapMode:Text.WordWrap;text:"Each stone on the gauntlet opens one phase of the Marvel Cinematic Universe, from Phase I to Phase VI. Open a phase to see its films and shows, and the comics behind them."}
                LabelText {width:parent.width;wrapMode:Text.WordWrap;text:"Keyboard: 1–6 open a phase, and Esc returns to the gauntlet."}
                LabelText {width:parent.width;wrapMode:Text.WordWrap;text:"Marvel characters and artwork belong to their respective owners. Colosseum is not affiliated with Marvel.";color:root.muted;font.pixelSize:11}
            }
        }
    }
}
