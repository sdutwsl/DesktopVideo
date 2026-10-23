#pragma once

#include "core/FrameQueue.h"

#include <GLES2/gl2.h>

#include <string>

namespace vw {

// Draws I420 frames with an OpenGL ES 2 shader, scaled "cover" style so the
// picture fills the surface and excess is cropped equally on both sides.
// One instance serves every surface of a shared EGL context.
class GlRenderer {
public:
    struct Textures {
        GLuint id[3] = {0, 0, 0};
        unsigned pitch = 0, lines = 0;   // allocated size of the luma plane
    };

    bool init(std::string& error);
    void destroy();

    void allocate(Textures& t, const VideoFormat& format);
    void upload(Textures& t, const VideoFormat& format, Frame& frame);
    void release(Textures& t);

    // tex == nullptr paints black (nothing decoded yet).
    void draw(const Textures* tex, const VideoFormat& format, int viewportW, int viewportH);

private:
    GLuint compile(GLenum type, const char* source, std::string& error);

    GLuint program_ = 0;
    GLint aPos_ = -1, aTex_ = -1;
    GLint uPlane_[3] = {-1, -1, -1};
    GLint uCoeff_ = -1;
};

} // namespace vw
