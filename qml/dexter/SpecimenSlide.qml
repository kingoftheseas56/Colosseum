import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import "SceneMath.js" as SceneMath
import "RackLayout.js" as Rack

Node {
    id: slide
    property int slideIndex: 0
    property int slideCount: 13
    property var item: ({})
    property real progress: 0
    property real hoverLift: 0
    property var dragOffset: null
    property vector3d cameraPosition: Qt.vector3d(-1200,850,600)
    property quaternion inspectionRotation: Qt.quaternion(1,0,0,0)
    readonly property var pose: SceneMath.pose(slideIndex,slideCount,progress,cameraPosition)
    readonly property real travel: pose.travel
    readonly property real artWidth: 1.12
    readonly property real artHeight: artWidth * 1.5
    readonly property real sortBase: progress>.015 ? -1000000-slideIndex*10000 : 200000+slideIndex*10000
    position: Qt.vector3d(pose.position.x + (dragOffset ? dragOffset[0] * 100 : 0),
                          pose.position.y + (hoverLift + (dragOffset ? dragOffset[1] : 0)) * 100,
                          pose.position.z + (dragOffset ? dragOffset[2] * 100 : 0))
    rotation: SceneMath.slerp(Qt.quaternion(Math.SQRT1_2,0,-Math.SQRT1_2,0),inspectionRotation,pose.turn)

    // The 75 x 25 mm glass remains one rigid 3:1 object throughout inspection.
    Model {
        source: "#Cube"
        scale: Qt.vector3d(Rack.slideWidth, Rack.slideHeight, Rack.slideThickness)
        castsShadows: false; receivesShadows: false
        depthBias: slide.sortBase-3000
        materials: PrincipledMaterial {
            baseColor: "#d5e2df"; metalness: 0; roughness: .095
            opacity: SceneMath.mix(.12,.038,slide.travel)
            opacityMap: Texture { source: "materials/clarity.png" }
            opacityChannel: Material.R
            alphaMode: PrincipledMaterial.Blend
            depthDrawMode: Material.NeverDepthDraw; cullMode: Material.NoCulling
            clearcoatAmount: .65; clearcoatRoughnessAmount: .10
        }
    }
    Model {
        geometry: ProceduralMesh {
            primitiveMode: ProceduralMesh.Lines
            positions: [Qt.vector3d(-187.5,-62.5,-1.35),Qt.vector3d(187.5,-62.5,-1.35),Qt.vector3d(187.5,62.5,-1.35),Qt.vector3d(-187.5,62.5,-1.35),Qt.vector3d(-187.5,-62.5,1.35),Qt.vector3d(187.5,-62.5,1.35),Qt.vector3d(187.5,62.5,1.35),Qt.vector3d(-187.5,62.5,1.35)]
            indexes: [0,1,1,2,2,3,3,0,4,5,5,6,6,7,7,4,0,4,1,5,2,6,3,7]
        }
        castsShadows: false; receivesShadows: false; depthBias: slide.sortBase-4000
        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#bdd7d2"; opacity: SceneMath.mix(.30,.52,slide.travel); alphaMode: PrincipledMaterial.Blend; depthDrawMode: Material.NeverDepthDraw; cullMode: Material.NoCulling }
    }
    Model {
        source: "#Cube"; scale: Qt.vector3d(3.75,.004,.029); y: 62.5
        castsShadows: false; receivesShadows: false; depthBias: slide.sortBase-5000
        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#d4e6de"; opacity: SceneMath.mix(.35,.58,slide.travel); alphaMode: PrincipledMaterial.Blend; depthDrawMode: Material.NeverDepthDraw }
    }
    Model {
        source: "#Rectangle"; scale: Qt.vector3d(.022,1.23,1); x: -123.75; z: 2.1
        castsShadows: false; receivesShadows: false; depthBias: slide.sortBase-8000
        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e7eee5"; opacity: .07*slide.travel; alphaMode: PrincipledMaterial.Blend; depthDrawMode: Material.NeverDepthDraw; cullMode: Material.NoCulling }
    }
    Model {
        source: "#Rectangle"; scale: Qt.vector3d(.59,1.205,1); x: -161.25; z: 2.3
        castsShadows: false; receivesShadows: false; depthBias: slide.sortBase-6000
        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e4e7dc"; opacity: SceneMath.mix(.075,.65,slide.travel); alphaMode: PrincipledMaterial.Blend; depthDrawMode: Material.NeverDepthDraw; cullMode: Material.NoCulling }
    }
    Model {
        source: "#Rectangle"
        scale: Qt.vector3d(.98+(slide.slideIndex%4)*.022,1.18-(slide.slideIndex%3)*.018,1)
        position: Qt.vector3d(106+Math.sin(slide.slideIndex*1.7)*2.2,-1.5+Math.sin(slide.slideIndex*2.1)*.8,1.9)
        eulerRotation.z: Math.sin(slide.slideIndex*2.5)*.025*180/Math.PI
        castsShadows: false; receivesShadows: false; depthBias: slide.sortBase
        materials: ArtworkMaterial {
            imageSource: slide.item.pigment || ""
            imageOpacity: SceneMath.mix(.87,.25,slide.travel)
        }
    }
    Model {
        source: "#Rectangle"; scale: Qt.vector3d(slide.artWidth+.075,slide.artHeight+.075,1)
        x: 30; z: 2.8; eulerRotation.z: -90
        castsShadows: false; receivesShadows: false; depthBias: slide.sortBase-1000
        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#181510"; opacity: .62; alphaMode: PrincipledMaterial.Blend; depthDrawMode: Material.NeverDepthDraw; cullMode: Material.NoCulling }
    }
    Node {
        objectName: "poster-" + slide.slideIndex
        x: 30
        z: 3.1
        eulerRotation.z: -90
        Rectangle {
            width: 240
            height: 360
            x: -width / 2
            y: -height / 2
            scale: slide.artWidth * 100 / width
            color: "#26151a"
            Image {
                id: poster
                objectName: "dexterArtwork-" + slide.slideIndex
                anchors.fill: parent
                source: slide.item.cover || ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                visible: poster.status !== Image.Ready
                text: slide.item.title || ""
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: "#e8d8c5"
                font.pixelSize: 25
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 42
                visible: (slide.item.season || 0) > 0
                color: "#db210f14"
                Text {
                    anchors.centerIn: parent
                    text: qsTr("SEASON %1").arg(slide.item.season || 0)
                    color: "#f2e6d6"
                    font.pixelSize: 22
                }
            }
        }
    }
    Model {
        source: "#Rectangle"; scale: Qt.vector3d(3.705,1.205,1); z: 3.9
        castsShadows: false; receivesShadows: false; depthBias: slide.sortBase-9000
        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e1ebde"; opacity: .065*slide.travel; alphaMode: PrincipledMaterial.Blend; depthDrawMode: Material.NeverDepthDraw; cullMode: Material.NoCulling }
    }
}
