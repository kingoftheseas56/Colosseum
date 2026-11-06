import QtQuick
import QtQuick3D

// Original poster and pigment materials set toneMapped:false. An unshaded
// native material preserves those image colors instead of applying scene ACES.
CustomMaterial {
    id: material
    property url imageSource
    property Item artworkItem: null
    readonly property bool flipArtworkY: artworkItem !== null
    property real imageOpacity: 1
    property TextureInput imageTexture: TextureInput {
        texture: Texture {
            source: material.imageSource
            sourceItem: material.artworkItem
            generateMipmaps: true
            mipFilter: Texture.Linear
            minFilter: Texture.Linear
            magFilter: Texture.Linear
        }
    }
    shadingMode: CustomMaterial.Unshaded
    sourceBlend: CustomMaterial.SrcAlpha
    destinationBlend: CustomMaterial.OneMinusSrcAlpha
    sourceAlphaBlend: CustomMaterial.One
    destinationAlphaBlend: CustomMaterial.OneMinusSrcAlpha
    cullMode: Material.NoCulling
    depthDrawMode: Material.NeverDepthDraw
    vertexShader: "shaders/artwork.vert"
    fragmentShader: "shaders/artwork.frag"
}
