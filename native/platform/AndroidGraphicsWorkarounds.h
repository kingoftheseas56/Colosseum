#pragma once

#include <QtCore/qglobal.h>

#ifdef Q_OS_ANDROID
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPointer>
#include <QQuickWindow>

namespace Colosseum::Platform {

inline void installAndroidGraphicsWorkarounds(QQuickWindow* window)
{
    QObject::connect(window, &QQuickWindow::beforeRenderPassRecording, window,
        [window, classifiedContext = QPointer<QOpenGLContext>(), emulatorContext = false]() mutable {
            auto* context = QOpenGLContext::currentContext();
            if (!context)
                return;
            auto* gl = context->functions();
            if (classifiedContext != context) {
                classifiedContext = context;
                const auto* renderer = gl->glGetString(GL_RENDERER);
                emulatorContext = context->isOpenGLES() && context->format().majorVersion() >= 3
                    && renderer && QByteArray(reinterpret_cast<const char*>(renderer))
                                       .contains("Android Emulator OpenGL ES Translator");
                if (emulatorContext)
                    qInfo("[graphics] emulator indexed-strip primitive-restart workaround enabled");
            }
            // Qt's GLES RHI enables fixed-index primitive restart at context creation.
            // The emulator translator drops triangles in indexed strips with that state
            // enabled (even a plain QML Rectangle). Qt Quick's 2D batches join strips
            // with degenerate indices; they do not require restart markers. Media3's
            // non-indexed OES quad is unaffected. Keep physical-device GL state intact.
            if (emulatorContext) {
                // Flush pending RHI commands first, so a later context/state setup
                // cannot undo this before the Qt Quick indexed draws execute.
                window->beginExternalCommands();
                if (auto* current = QOpenGLContext::currentContext())
                    current->functions()->glDisable(0x8D69); // GL_PRIMITIVE_RESTART_FIXED_INDEX
                window->endExternalCommands();
            }
        }, Qt::DirectConnection);
}

} // namespace Colosseum::Platform
#endif
