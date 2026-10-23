#include "GlRenderer.h"

namespace vw {

static const char* VERTEX_SRC = R"(
attribute vec2 aPos;
attribute vec2 aTex;
varying vec2 vTex;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTex = aTex;
}
)";

// Limited-range YUV -> RGB; uCoeff carries the BT.601 or BT.709 constants
// (rv, gu, gv, bu). libvlc's vmem output does not tell us the colour space, so
// the caller picks by resolution like players do: HD is 709, SD is 601.
static const char* FRAGMENT_SRC = R"(
precision mediump float;
varying vec2 vTex;
uniform sampler2D uY;
uniform sampler2D uU;
uniform sampler2D uV;
uniform vec4 uCoeff;
void main() {
    float y = 1.1643 * (texture2D(uY, vTex).r - 0.0625);
    float u = texture2D(uU, vTex).r - 0.5;
    float v = texture2D(uV, vTex).r - 0.5;
    gl_FragColor = vec4(y + uCoeff.x * v,
                        y - uCoeff.y * u - uCoeff.z * v,
                        y + uCoeff.w * u,
                        1.0);
}
)";

GLuint GlRenderer::compile(GLenum type, const char* source, std::string& error)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(shader, sizeof log, nullptr, log);
        error = log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool GlRenderer::init(std::string& error)
{
    GLuint vs = compile(GL_VERTEX_SHADER, VERTEX_SRC, error);
    if (!vs)
        return false;
    GLuint fs = compile(GL_FRAGMENT_SHADER, FRAGMENT_SRC, error);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetProgramInfoLog(program_, sizeof log, nullptr, log);
        error = log;
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    aPos_ = glGetAttribLocation(program_, "aPos");
    aTex_ = glGetAttribLocation(program_, "aTex");
    uPlane_[0] = glGetUniformLocation(program_, "uY");
    uPlane_[1] = glGetUniformLocation(program_, "uU");
    uPlane_[2] = glGetUniformLocation(program_, "uV");
    uCoeff_ = glGetUniformLocation(program_, "uCoeff");
    return true;
}

void GlRenderer::destroy()
{
    if (program_)
        glDeleteProgram(program_);
    program_ = 0;
}

void GlRenderer::allocate(Textures& t, const VideoFormat& format)
{
    release(t);
    glGenTextures(3, t.id);
    for (int p = 0; p < 3; ++p) {
        glBindTexture(GL_TEXTURE_2D, t.id[p]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, format.pitch[p], format.lines[p], 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);
    }
    t.pitch = format.pitch[0];
    t.lines = format.lines[0];
}

void GlRenderer::upload(Textures& t, const VideoFormat& format, Frame& frame)
{
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int p = 0; p < 3; ++p) {
        glBindTexture(GL_TEXTURE_2D, t.id[p]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, format.pitch[p], format.lines[p],
                        GL_LUMINANCE, GL_UNSIGNED_BYTE, frame.plane(format, p));
    }
}

void GlRenderer::release(Textures& t)
{
    if (t.id[0])
        glDeleteTextures(3, t.id);
    t.id[0] = t.id[1] = t.id[2] = 0;
    t.pitch = t.lines = 0;
}

void GlRenderer::draw(const Textures* tex, const VideoFormat& format, int viewportW, int viewportH)
{
    glViewport(0, 0, viewportW, viewportH);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!tex || !tex->id[0] || format.width == 0 || format.height == 0 || viewportW <= 0 || viewportH <= 0)
        return;

    // Fraction of the picture that survives the cover crop.
    const float surfaceAspect = float(viewportW) / float(viewportH);
    const float videoAspect = float(format.width) / float(format.height);
    float fx = 1.f, fy = 1.f;
    if (videoAspect > surfaceAspect)
        fx = surfaceAspect / videoAspect;   // picture wider than the screen: trim the sides
    else
        fy = videoAspect / surfaceAspect;   // picture taller: trim top and bottom

    // Visible picture sits in [0, width/pitch] x [0, height/lines] of the padded texture.
    const float sx = float(format.width) / float(format.pitch[0]);
    const float sy = float(format.height) / float(format.lines[0]);
    const float u0 = (1.f - fx) / 2.f * sx, u1 = (1.f + fx) / 2.f * sx;
    const float v0 = (1.f - fy) / 2.f * sy, v1 = (1.f + fy) / 2.f * sy;

    // Triangle strip; texture rows are top-down so v is flipped relative to y.
    const GLfloat verts[] = {
        -1.f, -1.f, u0, v1,
         1.f, -1.f, u1, v1,
        -1.f,  1.f, u0, v0,
         1.f,  1.f, u1, v0,
    };

    glUseProgram(program_);
    if (format.height >= 720)
        glUniform4f(uCoeff_, 1.7927f, 0.2132f, 0.5329f, 2.1124f);   // BT.709
    else
        glUniform4f(uCoeff_, 1.5958f, 0.3917f, 0.8129f, 2.0172f);   // BT.601
    for (int p = 0; p < 3; ++p) {
        glActiveTexture(GL_TEXTURE0 + p);
        glBindTexture(GL_TEXTURE_2D, tex->id[p]);
        glUniform1i(uPlane_[p], p);
    }
    glVertexAttribPointer(aPos_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), verts);
    glVertexAttribPointer(aTex_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), verts + 2);
    glEnableVertexAttribArray(aPos_);
    glEnableVertexAttribArray(aTex_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(aPos_);
    glDisableVertexAttribArray(aTex_);
}

} // namespace vw
