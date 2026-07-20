// Captured Motion - three still, QML-drawn folded-sheet compositions.
// Pure Qt Quick Shapes: no bitmap, Canvas, shader, or per-frame work.
import QtQuick
import QtQuick.Shapes
import QtQuick.Effects

Item {
    id: root
    anchors.fill: parent

    property bool running: true
    property int variant: 0

    readonly property int v: Math.max(0, Math.min(2, variant))
    readonly property var compositionSets: [aurelia, prismFold, emberGlass]
    readonly property var composition: compositionSets[v]
    readonly property var faceLayers: composition.faces
    readonly property var shadowLayers: composition.shadows
    readonly property var glassEdges: composition.edges

    // Broad looped sheets: orange, crimson and glass crossing at different depths.
    readonly property var aurelia: ({
        field: "#080810", bloom: "#35101f", bloomX: 735, bloomY: 430,
        shadows: [
            { path:"M 80 565 C 80 210 245 60 420 112 C 570 156 566 360 746 350 C 930 340 1002 168 1200 218 L 1208 300 C 1008 260 957 464 744 466 C 518 468 470 260 335 230 C 210 202 180 410 230 560 Z", op:.62 },
            { path:"M 405 678 C 535 530 604 310 808 312 C 1044 314 1086 570 1260 516 L 1260 650 C 1068 724 982 456 804 474 C 664 488 590 686 492 754 Z", op:.72 }
        ],
        faces: [
            { path:"M 72 500 C 90 212 236 68 398 104 C 552 138 570 338 742 333 C 920 328 1008 157 1216 215 L 1198 302 C 1000 260 946 450 746 455 C 520 460 475 254 338 224 C 218 198 170 396 214 536 Z", g:[80,120,510,560], c:["#441206","#c83b0b","#ff8a28","#69200f","#170913"], op:.94 },
            { path:"M 205 565 C 205 410 278 324 392 342 C 540 365 574 554 728 520 C 854 492 862 305 1018 284 C 1136 268 1196 352 1232 440 L 1172 500 C 1128 394 1072 370 1012 394 C 914 434 920 604 754 636 C 544 678 499 450 370 452 C 302 452 284 522 292 594 Z", g:[230,350,1080,550], c:["#290b3d","#8d126f","#ed2f78","#ff6c50","#4b1022"], op:.92 },
            { path:"M 360 645 C 478 580 520 362 700 326 C 900 286 982 438 1138 374 C 1194 350 1232 314 1276 282 L 1248 390 C 1178 444 1104 474 1018 456 C 890 430 820 406 728 476 C 616 560 606 724 466 756 Z", g:[380,650,1120,360], c:["#27052d","#72115f","#df2f76","#ff7d43","#55120f"], op:.94 },
            { path:"M 526 608 C 600 520 640 376 774 364 C 900 352 960 450 1052 430 C 1130 412 1178 340 1250 320 L 1234 390 C 1164 438 1118 506 1020 508 C 918 510 874 428 800 454 C 710 486 690 636 594 694 Z", g:[540,620,1160,360], c:["#40101c","#a92919","#ff7b22","#ffd071","#8d2914"], op:.94 },
            { path:"M 640 282 C 710 220 816 210 880 278 C 952 354 902 450 944 502 C 984 552 1056 524 1094 460 C 1126 406 1114 348 1092 308 C 1186 372 1202 490 1136 578 C 1050 692 906 650 850 566 C 798 488 850 400 820 356 C 788 310 728 326 680 372 Z", g:[670,250,1060,590], c:["#260826","#7e135a","#ed395e","#ff8a37","#37101d"], op:.91 },
            { path:"M 650 438 C 740 348 822 336 920 370 C 1018 404 1080 338 1168 250 L 1224 286 C 1156 414 1058 502 934 466 C 836 438 790 452 706 520 Z", g:[660,500,1190,265], c:["#153143","#387d86","#f3d486","#ff8f55","#55213d"], op:.56 },
            { path:"M 872 614 C 934 530 1000 512 1074 552 C 1146 590 1190 570 1272 496 L 1278 580 C 1198 666 1110 700 1026 638 C 970 596 930 610 894 674 Z", g:[880,660,1240,520], c:["#18091f","#60144e","#c33384","#ff81c4","#4b1734"], op:.84 }
        ],
        edges: [
            { path:"M 72 500 C 90 212 236 68 398 104 C 552 138 570 338 742 333 C 920 328 1008 157 1216 215 L 1208 228 C 1008 177 925 347 744 350 C 565 354 548 158 396 122 C 244 86 112 232 94 504 Z", g:[80,120,1210,230], c:["#00ffffff","#ffb26f","#fff0ca","#ff7b8d","#00ffffff"], op:.72 },
            { path:"M 360 645 C 478 580 520 362 700 326 C 900 286 982 438 1138 374 C 1194 350 1232 314 1276 282 L 1268 300 C 1214 344 1180 374 1136 394 C 976 462 902 312 704 346 C 534 376 496 594 370 664 Z", g:[370,650,1268,290], c:["#00ffffff","#ff77cf","#fff4bd","#82eff2","#00ffffff"], op:.7 },
            { path:"M 650 438 C 740 348 822 336 920 370 C 1018 404 1080 338 1168 250 L 1178 258 C 1092 358 1022 426 918 390 C 824 356 752 370 664 454 Z", g:[660,450,1178,258], c:["#00ffffff","#64e7ea","#fff59e","#ff76c4","#00ffffff"], op:.82 }
        ]
    })

    // A pleated crest: many leaves turn through one deep valley, like img26.
    readonly property var prismFold: ({
        field: "#090812", bloom: "#26113c", bloomX: 780, bloomY: 430,
        shadows: [
            { path:"M 84 628 C 250 530 390 570 490 456 C 586 348 560 170 734 166 C 950 160 1078 356 1256 630 L 1256 790 L 608 790 C 450 690 314 724 84 704 Z", op:.72 }
        ],
        faces: [
            { path:"M 80 600 C 254 500 382 548 474 438 C 574 320 548 140 730 142 C 958 144 1080 364 1266 650 L 1220 724 C 1050 450 936 246 748 232 C 622 222 632 382 532 500 C 414 638 270 572 110 680 Z", g:[90,600,760,170], c:["#21142d","#5c355f","#af553e","#ff8a43","#3a1730"], op:.98 },
            { path:"M 126 606 C 272 514 390 570 500 444 C 596 334 572 166 742 164 C 938 162 1060 358 1234 642 L 1194 688 C 1024 430 922 254 760 246 C 646 240 648 392 548 510 C 426 654 286 596 154 672 Z", g:[130,600,780,190], c:["#321122","#7d293d","#d64b31","#ff9d5a","#522447"], op:.97 },
            { path:"M 174 614 C 308 530 414 584 520 462 C 616 352 600 188 754 186 C 924 184 1038 360 1204 624 L 1164 672 C 1004 434 908 284 774 276 C 670 270 674 406 580 520 C 456 668 326 618 204 680 Z", g:[180,610,800,210], c:["#230d39","#662180","#b73a7f","#ff6576","#4a1e38"], op:.97 },
            { path:"M 226 622 C 346 548 436 600 542 480 C 636 372 628 210 770 208 C 914 206 1018 364 1174 612 L 1134 658 C 984 442 896 312 790 306 C 696 300 706 424 614 536 C 486 686 370 642 254 690 Z", g:[230,620,820,230], c:["#29104d","#7446a0","#c46ac4","#f0c4e9","#4b244f"], op:.98 },
            { path:"M 282 634 C 388 570 462 616 566 500 C 658 394 658 234 786 232 C 910 230 998 374 1144 604 L 1106 648 C 968 450 888 338 808 332 C 724 326 738 442 648 550 C 516 704 410 662 310 700 Z", g:[290,630,840,250], c:["#1b163f","#454b98","#8d83d2","#e9e5ff","#532768"], op:.98 },
            { path:"M 340 644 C 432 592 494 634 592 518 C 680 414 688 258 804 256 C 908 254 982 384 1116 596 L 1078 636 C 950 460 884 360 828 356 C 752 350 770 458 680 564 C 548 716 454 682 368 712 Z", g:[350,640,860,275], c:["#291027","#7f254a","#d45161","#ffb48b","#56203d"], op:.98 },
            { path:"M 402 654 C 480 612 526 650 618 536 C 704 432 716 280 822 280 C 908 280 968 398 1088 586 L 1050 626 C 940 470 884 384 848 382 C 780 378 802 476 712 578 C 580 730 496 702 430 724 Z", g:[410,650,880,300], c:["#221039","#682273","#c14c86","#ff867b","#5b2434"], op:.98 },
            { path:"M 468 664 C 530 634 560 666 646 552 C 728 446 748 304 840 304 C 908 304 956 410 1062 578 L 1024 614 C 928 482 888 408 868 408 C 810 406 832 492 744 592 C 612 742 546 720 496 736 Z", g:[475,660,900,325], c:["#241029","#802344","#e45038","#ff9e4d","#502436"], op:.98 },
            { path:"M 92 478 C 192 334 344 280 474 350 C 562 398 586 482 540 548 C 500 606 418 608 354 572 C 278 530 220 556 148 632 C 166 546 142 504 92 478 Z", g:[110,500,490,350], c:["#150d29","#39204d","#755188","#ae87b2","#35172d"], op:.96 },
            { path:"M 394 720 C 484 646 584 622 680 666 C 760 702 838 698 916 638 C 884 742 794 798 686 782 C 574 764 520 734 426 784 Z", g:[410,740,900,660], c:["#16060d","#4b1019","#8f251d","#d8542d","#2b0b22"], op:.98 }
        ],
        edges: [
            { path:"M 80 600 C 254 500 382 548 474 438 C 574 320 548 140 730 142 C 958 144 1080 364 1266 650 L 1256 664 C 1070 380 950 160 732 158 C 566 156 590 330 488 450 C 390 566 256 520 88 616 Z", g:[80,600,1260,200], c:["#00ffffff","#ff8c5c","#ffd4b8","#9d8dff","#00ffffff"], op:.72 },
            { path:"M 282 634 C 388 570 462 616 566 500 C 658 394 658 234 786 232 C 910 230 998 374 1144 604 L 1136 616 C 994 390 908 246 788 248 C 674 250 676 402 580 512 C 472 632 390 588 290 648 Z", g:[290,630,1138,260], c:["#00ffffff","#ffdcbe","#ffffff","#d896ff","#00ffffff"], op:.76 },
            { path:"M 468 664 C 530 634 560 666 646 552 C 728 446 748 304 840 304 C 908 304 956 410 1062 578 L 1054 590 C 952 428 908 320 842 320 C 764 320 748 456 658 562 C 570 678 534 648 476 678 Z", g:[470,665,1055,330], c:["#00ffffff","#ff7f98","#fff3cc","#a97cff","#00ffffff"], op:.84 }
        ]
    })

    // Monumental isolated folds with cyan/gold refraction and quiet dark space.
    readonly property var emberGlass: ({
        field: "#07070d", bloom: "#32120d", bloomX: 760, bloomY: 410,
        shadows: [
            { path:"M 40 610 C 180 430 280 360 420 400 C 570 444 588 630 716 620 C 868 608 884 348 1070 286 C 1172 252 1232 308 1278 360 L 1278 712 C 1150 620 1070 570 964 640 C 824 734 688 774 510 700 C 336 628 198 666 40 740 Z", op:.68 }
        ],
        faces: [
            { path:"M 32 592 C 154 418 270 332 408 382 C 558 436 550 620 696 602 C 848 584 864 328 1058 266 C 1172 230 1244 304 1290 374 L 1244 480 C 1194 390 1132 350 1072 378 C 934 442 938 668 742 720 C 560 768 488 562 370 524 C 264 490 174 570 96 688 Z", g:[40,600,1110,290], c:["#27080c","#801811","#e2461c","#ff9d3e","#4c1421"], op:.96 },
            { path:"M 248 628 C 322 520 356 332 506 270 C 642 214 758 286 762 406 C 766 522 650 566 662 652 C 674 730 762 754 850 706 C 746 820 584 800 536 700 C 494 612 590 520 574 432 C 562 366 494 368 444 426 C 382 500 370 622 306 700 Z", g:[280,650,690,280], c:["#230631","#6b145e","#d42d78","#ff738d","#4e1129"], op:.94 },
            { path:"M 476 586 C 516 468 578 366 686 346 C 806 324 870 402 848 498 C 826 594 740 618 714 674 C 692 724 734 754 794 740 C 702 818 584 772 588 686 C 592 606 690 558 700 486 C 708 432 660 416 616 456 C 572 496 554 560 534 622 Z", g:[500,600,790,360], c:["#35110b","#a43213","#ff7a20","#ffd06a","#7c2544"], op:.98 },
            { path:"M 760 616 C 836 516 868 340 1010 306 C 1146 274 1210 382 1194 492 C 1182 584 1112 636 1118 698 C 1122 742 1162 768 1218 752 C 1138 820 1028 784 1026 702 C 1024 622 1106 558 1094 476 C 1084 408 1022 400 978 454 C 920 522 910 630 840 706 Z", g:[780,650,1110,330], c:["#160b30","#4d237a","#a23bad","#ef6fbe","#3d1539"], op:.92 },
            { path:"M 848 420 C 930 332 1054 292 1168 332 C 1240 358 1272 414 1296 468 L 1288 584 C 1230 500 1182 454 1124 466 C 1040 484 1014 586 932 614 C 866 636 812 594 800 532 C 790 480 814 448 848 420 Z", g:[840,410,1270,520], c:["#102a38","#2c7585","#f5d782","#ff8650","#501738"], op:.58 }
        ],
        edges: [
            { path:"M 32 592 C 154 418 270 332 408 382 C 558 436 550 620 696 602 C 848 584 864 328 1058 266 C 1172 230 1244 304 1290 374 L 1282 392 C 1234 322 1170 250 1062 284 C 882 342 864 600 700 620 C 544 638 544 452 402 400 C 276 354 168 438 48 606 Z", g:[40,600,1280,300], c:["#00ffffff","#ff8a5a","#fff1a4","#62e9e9","#00ffffff"], op:.78 },
            { path:"M 248 628 C 322 520 356 332 506 270 C 642 214 758 286 762 406 L 748 410 C 744 300 640 244 516 292 C 378 348 340 530 264 642 Z", g:[250,630,750,280], c:["#00ffffff","#ff6db6","#fff3c0","#62eaf0","#00ffffff"], op:.84 },
            { path:"M 760 616 C 836 516 868 340 1010 306 C 1146 274 1210 382 1194 492 L 1180 490 C 1192 394 1138 302 1018 324 C 888 350 852 530 778 630 Z", g:[770,620,1190,320], c:["#00ffffff","#6ce4f0","#fff0a6","#ff72cf","#00ffffff"], op:.82 }
        ]
    })

    Rectangle {
        anchors.fill: parent
        color: root.composition.field
        gradient: Gradient {
            GradientStop { position: 0; color: root.v === 1 ? "#100d1a" : "#0c0a12" }
            GradientStop { position: .55; color: root.composition.field }
            GradientStop { position: 1; color: "#030306" }
        }
    }

    Item {
        id: art
        width: 1280
        height: 800
        anchors.centerIn: parent
        transformOrigin: Item.Center
        scale: Math.max(root.width / width, root.height / height)

        Shape {
            anchors.fill: parent
            ShapePath {
                strokeWidth: 0
                fillGradient: RadialGradient {
                    centerX: root.composition.bloomX
                    centerY: root.composition.bloomY
                    centerRadius: 620
                    focalX: root.composition.bloomX
                    focalY: root.composition.bloomY
                    GradientStop { position: 0; color: root.composition.bloom }
                    GradientStop { position: .55; color: "#140912" }
                    GradientStop { position: 1; color: "#00000000" }
                }
                PathMove { x: 0; y: 0 }
                PathLine { x: 1280; y: 0 }
                PathLine { x: 1280; y: 800 }
                PathLine { x: 0; y: 800 }
                PathLine { x: 0; y: 0 }
            }
        }

        Repeater {
            model: root.shadowLayers
            delegate: Shape {
                id: shadowFace
                required property var modelData
                anchors.fill: parent
                opacity: modelData.op
                ShapePath {
                    strokeWidth: 0
                    fillColor: "#020205"
                    PathSvg { path: shadowFace.modelData.path }
                }
            }
        }

        Repeater {
            model: root.faceLayers
            delegate: Item {
                id: materialFace
                required property var modelData
                anchors.fill: parent
                Shape {
                    id: faceSource
                    anchors.fill: parent
                    visible: false
                    layer.enabled: true
                    opacity: materialFace.modelData.op
                    ShapePath {
                        strokeWidth: 0
                        fillGradient: LinearGradient {
                            x1: materialFace.modelData.g[0]; y1: materialFace.modelData.g[1]
                            x2: materialFace.modelData.g[2]; y2: materialFace.modelData.g[3]
                            GradientStop { position: 0; color: materialFace.modelData.c[0] }
                            GradientStop { position: .2; color: materialFace.modelData.c[1] }
                            GradientStop { position: .46; color: materialFace.modelData.c[2] }
                            GradientStop { position: .68; color: materialFace.modelData.c[3] }
                            GradientStop { position: 1; color: materialFace.modelData.c[4] }
                        }
                        PathSvg { path: materialFace.modelData.path }
                    }
                }
                MultiEffect {
                    anchors.fill: faceSource
                    source: faceSource
                    shadowEnabled: true
                    shadowColor: "#d0000000"
                    shadowBlur: .72
                    shadowOpacity: .82
                    shadowVerticalOffset: 14
                    shadowHorizontalOffset: 5
                    blurMax: 32
                }
            }
        }

        // A clipped, asymmetric reflection pass makes each face turn through light
        // instead of reading as one flat vector fill.
        Repeater {
            model: root.faceLayers
            delegate: Shape {
                id: sheenFace
                required property var modelData
                anchors.fill: parent
                opacity: .72 * sheenFace.modelData.op
                ShapePath {
                    strokeWidth: 0
                    fillGradient: LinearGradient {
                        x1: sheenFace.modelData.g[2]; y1: sheenFace.modelData.g[1]
                        x2: sheenFace.modelData.g[0]; y2: sheenFace.modelData.g[3]
                        GradientStop { position: 0; color: "#00000000" }
                        GradientStop { position: .18; color: "#18ffffff" }
                        GradientStop { position: .3; color: "#02000000" }
                        GradientStop { position: .54; color: "#30fff2d8" }
                        GradientStop { position: .64; color: "#08000000" }
                        GradientStop { position: .82; color: "#24e6faff" }
                        GradientStop { position: 1; color: "#00000000" }
                    }
                    PathSvg { path: sheenFace.modelData.path }
                }
            }
        }

        Repeater {
            model: root.glassEdges
            delegate: Shape {
                id: glassFace
                required property var modelData
                anchors.fill: parent
                opacity: modelData.op
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: "#60ffe0b0"
                    shadowBlur: .38
                    shadowOpacity: .65
                    blurMax: 18
                }
                ShapePath {
                    strokeWidth: 0
                    fillGradient: LinearGradient {
                        x1: glassFace.modelData.g[0]; y1: glassFace.modelData.g[1]
                        x2: glassFace.modelData.g[2]; y2: glassFace.modelData.g[3]
                        GradientStop { position: 0; color: glassFace.modelData.c[0] }
                        GradientStop { position: .24; color: glassFace.modelData.c[1] }
                        GradientStop { position: .5; color: glassFace.modelData.c[2] }
                        GradientStop { position: .74; color: glassFace.modelData.c[3] }
                        GradientStop { position: 1; color: glassFace.modelData.c[4] }
                    }
                    PathSvg { path: glassFace.modelData.path }
                }
            }
        }
    }
}
