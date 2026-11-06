import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import "SceneMath.js" as SceneMath

Model {
    id: block
    property real w: 1
    property real h: 1
    property real d: 1
    property Material surfaceMaterial
    readonly property var mesh: SceneMath.beveledBox(w,h,d)
    geometry: ProceduralMesh {
        positions: block.mesh.positions
        normals: block.mesh.normals
        uv0s: block.mesh.uvs
        tangents: block.mesh.tangents
        binormals: block.mesh.binormals
        indexes: block.mesh.indexes
    }
    materials: [surfaceMaterial]
}
