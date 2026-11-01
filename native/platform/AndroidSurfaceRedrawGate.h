#pragma once

#include <cstdint>
#include <cstdlib>

namespace Colosseum::Platform {

// Android must not reveal a resized surface until Qt presents its new contents.
// Qt 6.11 may reconnect EGL in swapBuffers(), so the first swap after a resize
// can belong to the newly created, empty buffer. Request one more actual frame.
class AndroidSurfaceRedrawGate {
public:
    enum class Action { None, RequestFrame, Complete };
    struct Result { Action action = Action::None; std::int64_t token = 0; };

    void begin(std::int64_t token, int width, int height) {
        m_token = token;
        m_width = width;
        m_height = height;
        m_presented = 0;
        m_pending = token > 0 && width > 0 && height > 0;
    }
    bool isCurrent(std::int64_t token) const { return token > 0 && token == m_token; }
    Result presented(int width, int height) {
        // Logical Qt dimensions multiplied by fractional DPR can round a native
        // surface dimension by a pixel. A frame in the previous orientation fails.
        if (!m_pending || std::abs(width - m_width) > 2 || std::abs(height - m_height) > 2)
            return {};
        if (++m_presented == 1) return {Action::RequestFrame, m_token};
        m_pending = false;
        return {Action::Complete, m_token};
    }

private:
    std::int64_t m_token = 0;
    int m_width = 0;
    int m_height = 0;
    int m_presented = 0;
    bool m_pending = false;
};

} // namespace Colosseum::Platform
