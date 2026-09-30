from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "native" / "player" / "androidmedia3videonode.h"
CPP = ROOT / "native" / "player" / "androidmedia3videonode.cpp"
JAVA = ROOT / "native" / "platform" / "android" / "src" / "org" / "colosseum" / "player" / "Media3SurfaceBridge.java"


def source(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required renderer source is missing: {path.relative_to(ROOT)}")
    return path.read_text(encoding="utf-8")


class AndroidMedia3RendererContract(unittest.TestCase):
    def test_qsg_render_node_owns_external_oes_draw(self):
        header = source(HEADER)
        cpp = source(CPP)
        self.assertIn("public QSGRenderNode", header)
        self.assertIn("GL_TEXTURE_EXTERNAL_OES", cpp)
        self.assertIn("samplerExternalOES", cpp)
        self.assertIn("GL_OES_EGL_image_external", cpp)
        self.assertIn("QOpenGLContext::currentContext()", cpp)

    def test_surface_texture_transform_keeps_proven_v_correction(self):
        cpp = source(CPP)
        self.assertIn("getTransformMatrix", cpp)
        self.assertIn("1.0f - v", cpp)
        self.assertIn("updateTexImage", cpp)
        self.assertLess(cpp.index("updateTexImage"), cpp.index("getTransformMatrix"))

    def test_surface_bridge_wraps_qt_texture_and_only_schedules_frames(self):
        java = source(JAVA)
        self.assertIn("new SurfaceTexture(textureId)", java)
        self.assertIn("new Surface(surfaceTexture)", java)
        self.assertIn("setOnFrameAvailableListener", java)
        self.assertIn("nativeOnFrameAvailable(nativeHandle, surfaceGeneration)", java)
        listener = java[java.index("setOnFrameAvailableListener"):]
        self.assertNotIn("updateTexImage()", listener.split("public void updateTexImage", 1)[0])

    def test_surface_generation_filters_stale_frames(self):
        header = source(HEADER)
        cpp = source(CPP)
        java = source(JAVA)
        self.assertIn("surfaceGeneration", header)
        self.assertIn("pendingFrameGeneration", cpp)
        self.assertIn("pendingGeneration != m_surfaceGeneration", cpp)
        self.assertIn("surfaceGeneration", java)

    def test_surface_release_is_generation_tagged_and_precedes_resource_release(self):
        header = source(HEADER)
        cpp = source(CPP)
        self.assertIn("ClearSurfaceBlockingCallback", header)
        self.assertIn("m_clearSurfaceBlocking", cpp)
        release_start = cpp.index("void AndroidMedia3VideoNode::releaseResources()")
        destroy_start = cpp.index("void AndroidMedia3VideoNode::destroyGlObjects()")
        release = cpp[release_start:destroy_start]
        clear_call = release.index("m_clearSurfaceBlocking(m_surfaceGeneration, surface)")
        bridge_release = release.index('m_bridge.callMethod<void>("release")')
        destroy_call = release.index("destroyGlObjects()")
        self.assertLess(clear_call, bridge_release)
        self.assertLess(bridge_release, destroy_call)
        self.assertIn("glDeleteTextures", cpp[destroy_start:])

    def test_aspect_fit_is_renderer_geometry_only(self):
        header = source(HEADER)
        cpp = source(CPP)
        self.assertIn("setVideoSize", header)
        self.assertIn("aspectFitRect", cpp)
        self.assertIn("std::min", cpp)
        for playback_word in ("play()", "pause()", "seekTo", "loadSource"):
            self.assertNotIn(playback_word, header)

    def test_forbidden_rendering_paths_are_absent(self):
        combined = "\n".join((source(HEADER), source(CPP), source(JAVA)))
        for forbidden in (
            "fromNativeExternalOES", "QSGTextureMaterial", "SurfaceView", "TextureView",
            "PlayerView", "QVideoSink", "QtMultimedia", "Qt Multimedia", "libmpv", "MpvItem",
            "glReadPixels", "QImage", "ImageReader", "ByteBuffer", "Bitmap",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, combined)


if __name__ == "__main__":
    unittest.main()
