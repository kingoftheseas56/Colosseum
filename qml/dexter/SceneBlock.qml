import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import "SceneMath.js" as SceneMath

Model {
    id: block
    property var spec: ({size:[1,1,1],at:[0,0,0],material:"wood",beveled:false})
    property Material surfaceMaterial
    readonly property bool beveled: !!spec.beveled
    readonly property var meshData: beveled ? SceneMath.beveledBox(spec.size[0],spec.size[1],spec.size[2],spec.material === "cream" || spec.material === "lidLining" ? "lining" : "wood") : ({positions:[],normals:[],uvs:[],tangents:[],binormals:[],indexes:[]})
    source: beveled ? "" : (spec.cylinder ? "#Cylinder" : "#Cube")
    geometry: beveled ? nativeMesh : null
    scale: beveled ? Qt.vector3d(1,1,1) : Qt.vector3d(spec.size[0],spec.size[1],spec.size[2])
    position: Qt.vector3d(spec.at[0]*100,spec.at[1]*100,spec.at[2]*100)
    eulerRotation: spec.rotation ? Qt.vector3d(spec.rotation[0],spec.rotation[1],spec.rotation[2]) : Qt.vector3d(0,0,0)
    materials: [surfaceMaterial]
    ProceduralMesh {
        id: nativeMesh
        positions: block.meshData.positions
        normals: block.meshData.normals
        uv0s: block.meshData.uvs
        tangents: block.meshData.tangents
        binormals: block.meshData.binormals
        indexes: block.meshData.indexes
    }
}
