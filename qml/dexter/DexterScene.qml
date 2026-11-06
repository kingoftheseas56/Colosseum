pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D
import "SceneMath.js" as SceneMath
import "RackLayout.js" as Rack

View3D {
    id: view
    property var catalog: []
    property var progressValues: []
    property var hoverLifts: []
    property var dragOffsets: []
    property int selectedIndex: -1
    property bool enableShadows: true
    property bool enableNormalMaps: true
    property real fit: Math.max(1,1.38/(width/Math.max(1,height)))
    property alias cameraObject: sceneCamera
    readonly property quaternion inspectionRotation: SceneMath.multiply(sceneCamera.rotation,Qt.quaternion(Math.SQRT1_2,0,0,Math.SQRT1_2))
    readonly property real boxLength: Rack.boxLength
    readonly property real boxWidth: Rack.boxWidth
    readonly property real slideRatio: 3
    camera: sceneCamera
    function projectedPoint(x,y,z) { return mapFrom3DScene(Qt.vector3d(x,y,z)); }
    function materialFor(name) {
        switch(name) {
        case "wood": return wood;
        case "endgrain": return endgrain;
        case "darkWood": return darkWood;
        case "rimWood": return rimWood;
        case "cream": return cream;
        case "darkCream": return darkCream;
        case "brass": return brass;
        case "darkBrass": return darkBrass;
        case "lidLining": return lidLining;
        }
        return wood;
    }
    function block(w,h,d,m,x,y,z,rotation,cylinder) {
        return {size:[w,h,d],material:m,at:[x||0,y||0,z||0],rotation:rotation||[0,0,0],cylinder:!!cylinder,beveled:!cylinder&&["wood","endgrain","darkWood","rimWood","brass","cream","lidLining"].indexOf(m)>=0};
    }
    function boxParts(count) {
        var a=[], L=Rack.boxLength, W=Rack.boxWidth, wall=Rack.wallThickness, firstX=Rack.firstSlotX(count);
        a.push(block(L,.25,W,"wood",0,.125,0),block(L,.88,wall,"wood",0,.64,-W/2+wall/2),block(L,.88,wall,"wood",0,.64,W/2-wall/2));
        a.push(block(wall,.88,W,"endgrain",-L/2+wall/2,.64,0),block(wall,.88,W,"endgrain",L/2-wall/2,.64,0));
        a.push(block(L+.03,.055,W+.03,"rimWood",0,.315,0),block(L-.56,.11,W-.56,"darkWood",0,.43,0),block(L-.58,.06,W-.6,"cream",0,.51,0));
        for(var s=-1;s<=1;s+=2) {
            a.push(block(L,Rack.rimThickness,wall+.035,"rimWood",0,Rack.rimCenterY,s*(W/2-wall/2)));
            a.push(block(wall+.025,Rack.rimThickness,W,"rimWood",s*(L/2-wall/2),Rack.rimCenterY,0));
            a.push(block(L-.6,.28,.16,"cream",0,.65,s*1.89));
            a.push(block(L-.6,.05,.32,"darkCream",0,.565,s*1.85));
        }
        for(var i=0;i<=count;i++)for(var sign=-1;sign<=1;sign+=2)a.push(block(.052,.38,.18,"cream",firstX+(i-.5)*Rack.slotPitch,.71,sign*1.88));
        for(var j=0;j<4;j++)for(var side=-1;side<=1;side+=2) {
            a.push(block(.018,.11,.3,j%2?"wood":"darkWood",-L/2-.006,.42+j*.21,side*(W/2-.17)));
            a.push(block(.28,.11,.018,j%2?"darkWood":"wood",-L/2+.14,.42+j*.21,side*(W/2+.005)));
        }
        for(var z=-1.25;z<=1.25;z+=2.5) {
            a.push(block(.13,.6,.13,"brass",L/2-.11,1.12,z,[90,0,0],true));
            a.push(block(.26,.055,.44,"brass",L/2-.2,1.08,z));
        }
        return a;
    }
    function latchParts() {
        var a=[block(.58,.61,.045,"brass",0,0,0),block(.20,.44,.065,"darkBrass",0,-.01,.055),block(.065,.54,.045,"brass",-.18,-.26,.10),block(.065,.54,.045,"brass",.18,-.26,.10),block(.41,.08,.05,"brass",0,-.51,.10),block(.34,.31,.04,"darkBrass",0,-.68,.19,[-.48*180/Math.PI,0,0])];
        for(var x=-.23;x<=.23;x+=.46) {
            a.push(block(.11,.026,.11,"darkBrass",x,.17,.06,[90,0,0],true));
            a.push(block(.058,.008,.009,"cream",x,.17,.078,[0,0,.4*180/Math.PI]));
        }
        return a;
    }
    function lidParts() {
        return [block(6.6,.18,4.4,"darkWood",3.3,0,0),block(6.05,.06,3.85,"lidLining",3.3,.12,0),block(6.6,.24,.24,"wood",3.3,.15,-2.08),block(6.6,.24,.24,"wood",3.3,.15,2.08),block(.24,.24,4.4,"wood",6.48,.15,0)];
    }

    environment: SceneEnvironment {
        backgroundMode: SceneEnvironment.Color
        clearColor: "#080204"
        antialiasingMode: SceneEnvironment.MSAA
        antialiasingQuality: SceneEnvironment.High
        tonemapMode: SceneEnvironment.TonemapModeAces
        lightProbe: Texture { source: "materials/studio.png" }
        probeExposure: .86
        specularAAEnabled: true
        aoEnabled: true
        aoStrength: 32
        aoDistance: 25
        aoSoftness: 45
        aoSampleRate: 2
        aoDither: false
    }
    PerspectiveCamera {
        id: sceneCamera
        objectName: "sceneCamera"
        position: Qt.vector3d(-1200*view.fit,80+770*view.fit,600*view.fit)
        rotation: SceneMath.lookAt(position,Qt.vector3d(80,80,0))
        fieldOfView: 34
        fieldOfViewOrientation: PerspectiveCamera.Vertical
        clipNear: 10; clipFar: 12000
    }
    DirectionalLight {
        position: Qt.vector3d(-700,1100,600)
        rotation: SceneMath.lookAt(position,Qt.vector3d(0,0,0))
        color: "#fff0dc"; brightness: 2.35
        ambientColor: "#171318"
        castsShadow: view.enableShadows
        shadowMapQuality: Light.ShadowMapQualityVeryHigh
        shadowMapFar: 3500
        shadowBias: 10
        shadowFactor: 75
        softShadowQuality: Light.PCF16
        pcfFactor: 4
    }
    DirectionalLight {
        position: Qt.vector3d(700,900,-1100)
        rotation: SceneMath.lookAt(position,Qt.vector3d(0,0,0))
        color: "#a9bec9"; brightness: .58
    }
    DirectionalLight {
        position: Qt.vector3d(1500,400,300)
        rotation: SceneMath.lookAt(position,Qt.vector3d(0,0,0))
        color: "#ffd9ae"; brightness: .82
    }

    Texture { id: woodDiffuse; source: "materials/wood-mahogany.jpg"; tilingModeHorizontal: Texture.Repeat; tilingModeVertical: Texture.Repeat; generateMipmaps: true; mipFilter: Texture.Linear }
    Texture { id: woodRoughness; source: "materials/wood-roughness.jpg"; tilingModeHorizontal: Texture.Repeat; tilingModeVertical: Texture.Repeat; generateMipmaps: true; mipFilter: Texture.Linear }
    Texture { id: woodNormal; source: "materials/wood-normal.png"; tilingModeHorizontal: Texture.Repeat; tilingModeVertical: Texture.Repeat; generateMipmaps: true; mipFilter: Texture.Linear }
    Texture { id: grainNormal; source: "materials/grain-normal.png"; generateMipmaps: true; mipFilter: Texture.Linear }
    Texture { id: lining; source: "materials/lining-ivory.jpg"; generateMipmaps: true; mipFilter: Texture.Linear }
    PrincipledMaterial { id: wood; baseColor: "#9b8580"; baseColorMap: woodDiffuse; normalMap: view.enableNormalMaps ? woodNormal : null; normalStrength: .42; roughnessMap: woodRoughness; roughnessChannel: Material.R; roughness: .84; metalness: 0; clearcoatAmount: .42; clearcoatRoughnessAmount: .25 }
    PrincipledMaterial { id: endgrain; baseColor: "#a28b82"; baseColorMap: woodDiffuse; normalMap: view.enableNormalMaps ? woodNormal : null; normalStrength: .38; roughnessMap: woodRoughness; roughnessChannel: Material.R; roughness: .88; metalness: 0; clearcoatAmount: .34; clearcoatRoughnessAmount: .30 }
    PrincipledMaterial { id: darkWood; baseColor: "#6b504b"; baseColorMap: woodDiffuse; normalMap: view.enableNormalMaps ? woodNormal : null; normalStrength: .38; roughnessMap: woodRoughness; roughnessChannel: Material.R; roughness: .88; metalness: 0; clearcoatAmount: .32; clearcoatRoughnessAmount: .28 }
    PrincipledMaterial { id: rimWood; baseColor: "#bd9e92"; baseColorMap: woodDiffuse; normalMap: view.enableNormalMaps ? woodNormal : null; normalStrength: .32; roughnessMap: woodRoughness; roughnessChannel: Material.R; roughness: .78; metalness: 0; clearcoatAmount: .48; clearcoatRoughnessAmount: .24 }
    PrincipledMaterial { id: cream; baseColor: "#e4d9c7"; baseColorMap: lining; normalMap: view.enableNormalMaps ? grainNormal : null; normalStrength: .20; roughness: .86; metalness: 0 }
    PrincipledMaterial { id: darkCream; baseColor: "#9e8e77"; roughness: .94 }
    PrincipledMaterial { id: brass; baseColor: "#e0bb70"; metalness: .68; roughness: .23; clearcoatAmount: .12; clearcoatRoughnessAmount: .28 }
    PrincipledMaterial { id: darkBrass; baseColor: "#b5995e"; metalness: .65; roughness: .34 }
    PrincipledMaterial { id: lidLining; baseColor: "#cfc5b4"; baseColorMap: lining; normalMap: view.enableNormalMaps ? grainNormal : null; normalStrength: .18; roughness: .96 }

    Model {
        source: "#Rectangle"; scale: Qt.vector3d(160,160,1); x: 250; y: -2; eulerRotation.x: -90
        castsShadows: false
        materials: PrincipledMaterial {
            baseColor: "#ffffff"
            baseColorMap: Texture { source: "materials/oxblood-floor.png"; generateMipmaps: true; mipFilter: Texture.Linear }
            roughness: .97
            specularAmount: 0
        }
    }
    Model {
        source: "#Rectangle"; scale: Qt.vector3d(9,5,1); y: .5; eulerRotation.x: -90
        castsShadows: false; receivesShadows: false; depthBias: 100000000
        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: Texture { source: "materials/shadow.png" } opacity: .82; alphaMode: PrincipledMaterial.Blend; depthDrawMode: Material.NeverDepthDraw }
    }
    Model {
        source: "#Rectangle"; scale: Qt.vector3d(13,6,1); x: 820; y: .4; eulerRotation.x: -90
        castsShadows: false; receivesShadows: false; depthBias: 100000000
        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: Texture { source: "materials/shadow.png" } opacity: .07; alphaMode: PrincipledMaterial.Blend; depthDrawMode: Material.NeverDepthDraw }
    }
    Node {
        objectName: "specimenBox"
        Repeater3D {
            model: view.boxParts(view.catalog.length)
            delegate: SceneBlock { required property var modelData; spec: modelData; surfaceMaterial: view.materialFor(modelData.material) }
        }
        Model {
            source: "#Rectangle"; scale: Qt.vector3d(Rack.slotPitch*view.catalog.length,.19,1)
            y: 96; z: 201.5; eulerRotation.x: -90
            materials: PrincipledMaterial { baseColor: "#bcb18e"; roughness: .86 }
        }
        Node {
            objectName: "brassLatch"
            x: -333.4; y: 69; eulerRotation.y: -90
            Repeater3D { model: view.latchParts(); delegate: SceneBlock { required property var modelData; spec: modelData; surfaceMaterial: view.materialFor(modelData.material) } }
        }
        Node {
            objectName: "openLid"
            x: 317; y: 110; eulerRotation.z: .04*180/Math.PI
            Repeater3D { model: view.lidParts(); delegate: SceneBlock { required property var modelData; spec: modelData; surfaceMaterial: view.materialFor(modelData.material) } }
        }
        Model {
            source: "#Rectangle"; scale: Qt.vector3d(.028,3.78,1)
            x: Rack.slotX(view.selectedIndex,view.catalog.length)*100; y: 80.5; eulerRotation.x: -90 // highlight stays on the rack, not suspended at slide-top height
            visible: view.selectedIndex>=0
            castsShadows: false; receivesShadows: false; depthBias: 225000
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e0c997"; opacity: .45; alphaMode: PrincipledMaterial.Blend; depthDrawMode: Material.NeverDepthDraw }
        }
        Repeater3D {
            id: specimens
            model: view.catalog.length
            delegate: SpecimenSlide {
                required property int index
                objectName: "specimen-"+index
                slideIndex: index; slideCount: view.catalog.length
                item: view.catalog[index]
                progress: view.progressValues[index] || 0
                hoverLift: view.hoverLifts[index] || 0
                dragOffset: view.dragOffsets[index] || null
                cameraPosition: sceneCamera.position
                inspectionRotation: view.inspectionRotation
            }
        }
    }
}
