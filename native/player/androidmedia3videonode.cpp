#include "androidmedia3videonode.h"

#ifdef Q_OS_ANDROID

#include <QDebug>
#include <QHash>
#include <QJniEnvironment>
#include <QMatrix4x4>
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLContext>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <utility>

namespace {
struct FrameRegistryEntry
{
    quint64 serial = 0;
    std::shared_ptr<std::atomic<quint64>> pendingFrameGeneration;
    AndroidMedia3VideoNode::FrameScheduleCallback scheduleFrame;
};

QMutex g_registryMutex;
QHash<jlong, std::shared_ptr<FrameRegistryEntry>> g_frameRegistry;
std::atomic<quint64> g_nextRegistrySerial{1};
std::atomic<quint64> g_nextSurfaceGeneration{1};

quint64 nextNonZero(std::atomic<quint64> &counter)
{
    quint64 value = counter.fetch_add(1, std::memory_order_relaxed);
    while (value == 0)
        value = counter.fetch_add(1, std::memory_order_relaxed);
    return value;
}

QPointF mapUv(const std::array<float, 16> &matrix, float u, float v)
{
    // Arc 46 runtime proof: SurfaceTexture and Qt Quick disagree about the incoming
    // V origin, so flip V before applying Android's SurfaceTexture transform matrix.
    const float surfaceV = 1.0f - v;
    return {matrix[0] * u + matrix[4] * surfaceV + matrix[12],
            matrix[1] * u + matrix[5] * surfaceV + matrix[13]};
}

GLuint compileShader(GLenum type, const char *source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE)
        return shader;

    std::array<GLchar, 2048> log{};
    GLsizei length = 0;
    glGetShaderInfoLog(shader, GLsizei(log.size()), &length, log.data());
    qWarning() << "Media3 OES shader compile failed:" << log.data();
    glDeleteShader(shader);
    return 0;
}

void scheduleFrameFor(jlong nativeHandle, quint64 surfaceGeneration)
{
    std::shared_ptr<FrameRegistryEntry> entry;
    {
        QMutexLocker locker(&g_registryMutex);
        entry = g_frameRegistry.value(nativeHandle);
    }
    if (!entry || !entry->pendingFrameGeneration)
        return;
    entry->pendingFrameGeneration->store(surfaceGeneration, std::memory_order_release);
    if (entry->scheduleFrame)
        entry->scheduleFrame();
}
} // namespace

AndroidMedia3VideoNode::AndroidMedia3VideoNode(
        jlong nativeHandle,
        FrameScheduleCallback scheduleFrame,
        SurfaceReadyCallback surfaceReady,
        ClearSurfaceBlockingCallback clearSurfaceBlocking)
    : m_nativeHandle(nativeHandle),
      m_registrySerial(nextNonZero(g_nextRegistrySerial)),
      m_scheduleFrame(std::move(scheduleFrame)),
      m_surfaceReady(std::move(surfaceReady)),
      m_clearSurfaceBlocking(std::move(clearSurfaceBlocking)),
      m_pendingFrameGeneration(std::make_shared<std::atomic<quint64>>(0))
{
    if (m_nativeHandle == 0)
        return;

    auto entry = std::make_shared<FrameRegistryEntry>();
    entry->serial = m_registrySerial;
    entry->pendingFrameGeneration = m_pendingFrameGeneration;
    entry->scheduleFrame = m_scheduleFrame;
    QMutexLocker locker(&g_registryMutex);
    g_frameRegistry.insert(m_nativeHandle, std::move(entry));
}

AndroidMedia3VideoNode::~AndroidMedia3VideoNode()
{
    unregisterFrameCallback();
    if (QOpenGLContext::currentContext())
        releaseResources();
}

QSGRenderNode::RenderingFlags AndroidMedia3VideoNode::flags() const
{
    return BoundedRectRendering | DepthAwareRendering;
}

QRectF AndroidMedia3VideoNode::rect() const
{
    return m_targetRect;
}

void AndroidMedia3VideoNode::setTargetRect(const QRectF &rect)
{
    m_targetRect = rect;
}

void AndroidMedia3VideoNode::setVideoSize(const QSize &videoSize)
{
    m_videoSize = videoSize;
}

quint64 AndroidMedia3VideoNode::surfaceGeneration() const
{
    return m_surfaceGeneration;
}

QRectF AndroidMedia3VideoNode::aspectFitRect() const
{
    if (m_targetRect.isEmpty() || m_videoSize.width() <= 0 || m_videoSize.height() <= 0)
        return m_targetRect;

    const qreal scale = std::min(m_targetRect.width() / qreal(m_videoSize.width()),
                                 m_targetRect.height() / qreal(m_videoSize.height()));
    const QSizeF fitted(qreal(m_videoSize.width()) * scale,
                        qreal(m_videoSize.height()) * scale);
    return QRectF(QPointF(m_targetRect.center().x() - fitted.width() / 2.0,
                          m_targetRect.center().y() - fitted.height() / 2.0),
                  fitted);
}

void AndroidMedia3VideoNode::render(const RenderState *state)
{
    if (!QOpenGLContext::currentContext()) {
        qWarning() << "Media3 OES render called without a current OpenGL context";
        return;
    }
    if (m_targetRect.isEmpty() || !ensureResources())
        return;

    const quint64 pendingGeneration =
            m_pendingFrameGeneration->exchange(0, std::memory_order_acq_rel);
    if (pendingGeneration != 0 && pendingGeneration != m_surfaceGeneration) {
        // A callback from a released SurfaceTexture must never update a replacement texture.
        qDebug() << "Ignoring stale Media3 frame generation" << pendingGeneration
                 << "current" << m_surfaceGeneration;
    } else if (pendingGeneration == m_surfaceGeneration && m_bridge.isValid()) {
        m_bridge.callMethod<void>("updateTexImage", "(J)V", jlong(m_surfaceGeneration));
        QJniEnvironment env;
        jfloatArray matrix = env->NewFloatArray(16);
        if (matrix) {
            m_bridge.callMethod<void>("getTransformMatrix", "(J[F)V",
                                      jlong(m_surfaceGeneration), matrix);
            env->GetFloatArrayRegion(matrix, 0, 16, m_transform.data());
            env->DeleteLocalRef(matrix);
        }
    }

    const QPointF tl = mapUv(m_transform, 0.0f, 0.0f);
    const QPointF bl = mapUv(m_transform, 0.0f, 1.0f);
    const QPointF tr = mapUv(m_transform, 1.0f, 0.0f);
    const QPointF br = mapUv(m_transform, 1.0f, 1.0f);
    const QRectF fitted = aspectFitRect();
    const float left = float(fitted.left());
    const float top = float(fitted.top());
    const float right = float(fitted.right());
    const float bottom = float(fitted.bottom());
    const std::array<float, 16> vertices{
        left,  top,    float(tl.x()), float(tl.y()),
        left,  bottom, float(bl.x()), float(bl.y()),
        right, top,    float(tr.x()), float(tr.y()),
        right, bottom, float(br.x()), float(br.y())
    };

    glUseProgram(m_program);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(sizeof(vertices)), vertices.data(), GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void *>(2 * sizeof(float)));

    QMatrix4x4 mvp = *state->projectionMatrix();
    if (matrix())
        mvp *= *matrix();
    glUniformMatrix4fv(m_mvpLocation, 1, GL_FALSE, mvp.constData());
    glUniform1f(m_opacityLocation, float(inheritedOpacity()));
    glUniform1i(m_textureLocation, 0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_textureId);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

bool AndroidMedia3VideoNode::ensureResources()
{
    if (m_program != 0 && m_textureId != 0 && m_vertexBuffer != 0 && m_bridge.isValid())
        return true;
    if (!QOpenGLContext::currentContext())
        return false;
    if (m_nativeHandle == 0 || !m_scheduleFrame || !m_surfaceReady || !m_clearSurfaceBlocking) {
        qWarning() << "Media3 OES node is missing required facade callbacks";
        return false;
    }

    static const char vertexShaderSource[] =
            "attribute highp vec2 aPosition;\n"
            "attribute highp vec2 aTexCoord;\n"
            "uniform highp mat4 uMvp;\n"
            "varying highp vec2 vTexCoord;\n"
            "void main() {\n"
            "    gl_Position = uMvp * vec4(aPosition, 0.0, 1.0);\n"
            "    vTexCoord = aTexCoord;\n"
            "}\n";
    static const char fragmentShaderSource[] =
            "#extension GL_OES_EGL_image_external : require\n"
            "precision mediump float;\n"
            "varying highp vec2 vTexCoord;\n"
            "uniform samplerExternalOES uTexture;\n"
            "uniform lowp float uOpacity;\n"
            "void main() {\n"
            "    gl_FragColor = texture2D(uTexture, vTexCoord) * uOpacity;\n"
            "}\n";

    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    if (!vertexShader)
        return false;
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    if (!fragmentShader) {
        glDeleteShader(vertexShader);
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vertexShader);
    glAttachShader(m_program, fragmentShader);
    glBindAttribLocation(m_program, 0, "aPosition");
    glBindAttribLocation(m_program, 1, "aTexCoord");
    glLinkProgram(m_program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        std::array<GLchar, 2048> log{};
        GLsizei length = 0;
        glGetProgramInfoLog(m_program, GLsizei(log.size()), &length, log.data());
        qWarning() << "Media3 OES program link failed:" << log.data();
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    m_mvpLocation = glGetUniformLocation(m_program, "uMvp");
    m_textureLocation = glGetUniformLocation(m_program, "uTexture");
    m_opacityLocation = glGetUniformLocation(m_program, "uOpacity");

    glGenBuffers(1, &m_vertexBuffer);
    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_textureId);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    const GLenum error = glGetError();
    if (m_textureId == 0 || m_vertexBuffer == 0 || error != GL_NO_ERROR) {
        qWarning() << "Media3 OES resource creation failed texture=" << m_textureId
                   << "buffer=" << m_vertexBuffer << "glError=" << Qt::hex << error;
        destroyGlObjects();
        return false;
    }

    m_surfaceGeneration = nextNonZero(g_nextSurfaceGeneration);
    m_bridge = QJniObject("org/colosseum/player/Media3SurfaceBridge", "(IJJ)V",
                          jint(m_textureId), m_nativeHandle, jlong(m_surfaceGeneration));
    if (!m_bridge.isValid()) {
        qWarning() << "Media3 failed to create SurfaceBridge";
        m_surfaceGeneration = 0;
        destroyGlObjects();
        return false;
    }

    const QJniObject surface = m_bridge.callObjectMethod(
            "surface", "()Landroid/view/Surface;");
    if (!surface.isValid()) {
        qWarning() << "Media3 SurfaceBridge returned an invalid Surface";
        m_bridge.callMethod<void>("release");
        m_bridge = QJniObject();
        m_surfaceGeneration = 0;
        destroyGlObjects();
        return false;
    }

    m_surfaceReady(m_surfaceGeneration, surface);
    m_surfacePublished = true;
    qInfo() << "Media3 OES render node ready textureId=" << m_textureId
            << "surfaceGeneration=" << m_surfaceGeneration;
    return true;
}

void AndroidMedia3VideoNode::releaseResources()
{
    if (!QOpenGLContext::currentContext()) {
        qWarning() << "Media3 OES resources can only be released with the scene-graph GL context current";
        return;
    }

    if (m_bridge.isValid()) {
        const QJniObject surface = m_bridge.callObjectMethod(
                "surface", "()Landroid/view/Surface;");
        if (m_surfacePublished && surface.isValid() && m_clearSurfaceBlocking) {
            // This callback is deliberately blocking: Media3 must stop using this exact Surface
            // before SurfaceTexture or its GL_TEXTURE_EXTERNAL_OES name can be destroyed.
            m_clearSurfaceBlocking(m_surfaceGeneration, surface);
        }
        m_surfacePublished = false;
        m_bridge.callMethod<void>("release");
        m_bridge = QJniObject();
    }

    destroyGlObjects();
    m_surfaceGeneration = 0;
    if (m_pendingFrameGeneration)
        m_pendingFrameGeneration->store(0, std::memory_order_release);
}

void AndroidMedia3VideoNode::destroyGlObjects()
{
    if (!QOpenGLContext::currentContext())
        return;
    if (m_vertexBuffer != 0) {
        glDeleteBuffers(1, &m_vertexBuffer);
        m_vertexBuffer = 0;
    }
    if (m_program != 0) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    if (m_textureId != 0) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    m_mvpLocation = -1;
    m_textureLocation = -1;
    m_opacityLocation = -1;
}

void AndroidMedia3VideoNode::unregisterFrameCallback()
{
    if (m_nativeHandle == 0)
        return;
    QMutexLocker locker(&g_registryMutex);
    const auto current = g_frameRegistry.value(m_nativeHandle);
    if (current && current->serial == m_registrySerial)
        g_frameRegistry.remove(m_nativeHandle);
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3SurfaceBridge_nativeOnFrameAvailable(
        JNIEnv *, jclass, jlong nativeHandle, jlong surfaceGeneration)
{
    if (surfaceGeneration <= 0)
        return;
    scheduleFrameFor(nativeHandle, quint64(surfaceGeneration));
}

#endif // Q_OS_ANDROID
