#include "NativeOpenGLVideoRenderer.h"

#include <QResizeEvent>
#include <QSizePolicy>
#include <QWindow>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <GL/gl.h>

#include <cstddef>
#include <cstring>

#pragma comment(lib, "opengl32.lib")

namespace {

HWND hwndFromWinId(WId wid)
{
    return reinterpret_cast<HWND>(wid);
}

bool clientSizePx(HWND hwnd, int *w, int *h)
{
    if (!hwnd || !w || !h) {
        return false;
    }
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) {
        return false;
    }
    *w = rc.right - rc.left;
    *h = rc.bottom - rc.top;
    return *w > 0 && *h > 0;
}

// GL 1.1 头文件没有着色器/VBO 常量，从 glext 抄需要的几个。
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

using PFNGLGENBUFFERSPROC = void (APIENTRY *)(GLsizei, GLuint *);
using PFNGLBINDBUFFERPROC = void (APIENTRY *)(GLenum, GLuint);
using PFNGLBUFFERDATAPROC = void (APIENTRY *)(GLenum, ptrdiff_t, const void *, GLenum);
using PFNGLDELETEBUFFERSPROC = void (APIENTRY *)(GLsizei, const GLuint *);
using PFNGLCREATESHADERPROC = GLuint (APIENTRY *)(GLenum);
using PFNGLSHADERSOURCEPROC = void (APIENTRY *)(GLuint, GLsizei, const char *const *, const GLint *);
using PFNGLCOMPILESHADERPROC = void (APIENTRY *)(GLuint);
using PFNGLGETSHADERIVPROC = void (APIENTRY *)(GLuint, GLenum, GLint *);
using PFNGLDELETESHADERPROC = void (APIENTRY *)(GLuint);
using PFNGLCREATEPROGRAMPROC = GLuint (APIENTRY *)();
using PFNGLATTACHSHADERPROC = void (APIENTRY *)(GLuint, GLuint);
using PFNGLLINKPROGRAMPROC = void (APIENTRY *)(GLuint);
using PFNGLGETPROGRAMIVPROC = void (APIENTRY *)(GLuint, GLenum, GLint *);
using PFNGLUSEPROGRAMPROC = void (APIENTRY *)(GLuint);
using PFNGLDELETEPROGRAMPROC = void (APIENTRY *)(GLuint);
using PFNGLGETATTRIBLOCATIONPROC = GLint (APIENTRY *)(GLuint, const char *);
using PFNGLGETUNIFORMLOCATIONPROC = GLint (APIENTRY *)(GLuint, const char *);
using PFNGLENABLEVERTEXATTRIBARRAYPROC = void (APIENTRY *)(GLuint);
using PFNGLDISABLEVERTEXATTRIBARRAYPROC = void (APIENTRY *)(GLuint);
using PFNGLVERTEXATTRIBPOINTERPROC = void (APIENTRY *)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
using PFNGLUNIFORM1IPROC = void (APIENTRY *)(GLint, GLint);
using PFNGLACTIVETEXTUREPROC = void (APIENTRY *)(GLenum);

struct GlApi {
    PFNGLGENBUFFERSPROC GenBuffers = nullptr;
    PFNGLBINDBUFFERPROC BindBuffer = nullptr;
    PFNGLBUFFERDATAPROC BufferData = nullptr;
    PFNGLDELETEBUFFERSPROC DeleteBuffers = nullptr;
    PFNGLCREATESHADERPROC CreateShader = nullptr;
    PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
    PFNGLCOMPILESHADERPROC CompileShader = nullptr;
    PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
    PFNGLDELETESHADERPROC DeleteShader = nullptr;
    PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
    PFNGLATTACHSHADERPROC AttachShader = nullptr;
    PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
    PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
    PFNGLUSEPROGRAMPROC UseProgram = nullptr;
    PFNGLDELETEPROGRAMPROC DeleteProgram = nullptr;
    PFNGLGETATTRIBLOCATIONPROC GetAttribLocation = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray = nullptr;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray = nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer = nullptr;
    PFNGLUNIFORM1IPROC Uniform1i = nullptr;
    PFNGLACTIVETEXTUREPROC ActiveTexture = nullptr;

    bool load()
    {
        auto proc = [](const char *name) -> void * {
            // [WGL] wglGetProcAddress：取当前上下文的扩展函数（着色器/VBO 不在 opengl32 导出表里）。
            // 形参：lpszProc=函数名。1.1 函数这里会返回空，需改走 GetProcAddress。
            void *p = reinterpret_cast<void *>(wglGetProcAddress(name));
            if (!p) {
                HMODULE mod = GetModuleHandleW(L"opengl32.dll");
                p = mod ? reinterpret_cast<void *>(GetProcAddress(mod, name)) : nullptr;
            }
            return p;
        };
#define LOAD(member, name)                                                                 \
    do {                                                                                   \
        member = reinterpret_cast<decltype(member)>(proc(name));                           \
        if (!member) {                                                                     \
            return false;                                                                  \
        }                                                                                  \
    } while (0)
        LOAD(GenBuffers, "glGenBuffers");
        LOAD(BindBuffer, "glBindBuffer");
        LOAD(BufferData, "glBufferData");
        LOAD(DeleteBuffers, "glDeleteBuffers");
        LOAD(CreateShader, "glCreateShader");
        LOAD(ShaderSource, "glShaderSource");
        LOAD(CompileShader, "glCompileShader");
        LOAD(GetShaderiv, "glGetShaderiv");
        LOAD(DeleteShader, "glDeleteShader");
        LOAD(CreateProgram, "glCreateProgram");
        LOAD(AttachShader, "glAttachShader");
        LOAD(LinkProgram, "glLinkProgram");
        LOAD(GetProgramiv, "glGetProgramiv");
        LOAD(UseProgram, "glUseProgram");
        LOAD(DeleteProgram, "glDeleteProgram");
        LOAD(GetAttribLocation, "glGetAttribLocation");
        LOAD(GetUniformLocation, "glGetUniformLocation");
        LOAD(EnableVertexAttribArray, "glEnableVertexAttribArray");
        LOAD(DisableVertexAttribArray, "glDisableVertexAttribArray");
        LOAD(VertexAttribPointer, "glVertexAttribPointer");
        LOAD(Uniform1i, "glUniform1i");
        LOAD(ActiveTexture, "glActiveTexture");
#undef LOAD
        return true;
    }
};

static const char *kVert = R"(
attribute vec2 aPos;
attribute vec2 aTex;
varying vec2 vTex;
void main() {
    vTex = aTex;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char *kFrag = R"(
varying vec2 vTex;
uniform sampler2D uTex;
void main() {
    gl_FragColor = texture2D(uTex, vTex);
}
)";

struct Vertex {
    float x, y, u, v;
};

} // namespace

/**
 * 自包含 WGL 窗口：像素格式、上下文、着色器、纹理都在本对象内。
 */
class NativeOpenGLVideoRenderer::RenderWindow : public QWindow
{
public:
    RenderWindow()
    {
        setSurfaceType(QSurface::RasterSurface);
    }

    ~RenderWindow() override
    {
        releaseGl();
    }

    void setFrame(const QImage &frame)
    {
        if (frame.isNull()) {
            return;
        }
        m_frame = frame.convertToFormat(QImage::Format_RGBA8888);
        m_hasFrame = !m_frame.isNull();
        render();
    }

    void setPlaceholder()
    {
        m_frame = QImage();
        m_hasFrame = false;
        render();
    }

    void releaseGl()
    {
        if (m_hrc && m_hdc) {
            // [WGL] wglMakeCurrent：把本窗口的 GL 上下文绑到当前线程。hdc/hglrc 都空则解绑。
            wglMakeCurrent(m_hdc, m_hrc);
            if (m_api.DeleteBuffers && m_vbo) {
                m_api.DeleteBuffers(1, &m_vbo);
                m_vbo = 0;
            }
            if (m_tex) {
                // [OpenGL] glDeleteTextures：释放纹理对象。形参：n=1；textures。
                glDeleteTextures(1, &m_tex);
                m_tex = 0;
            }
            if (m_api.DeleteProgram && m_program) {
                m_api.DeleteProgram(m_program);
                m_program = 0;
            }
            wglMakeCurrent(nullptr, nullptr);
        }
        if (m_hrc) {
            // [WGL] wglDeleteContext：销毁渲染上下文。形参：hglrc。
            wglDeleteContext(m_hrc);
            m_hrc = nullptr;
        }
        if (m_hdc && m_hwnd) {
            ReleaseDC(m_hwnd, m_hdc);
            m_hdc = nullptr;
        }
        m_hwnd = nullptr;
        m_texW = m_texH = 0;
        m_loaded = false;
    }

protected:
    void exposeEvent(QExposeEvent *) override
    {
        if (isExposed()) {
            render();
        }
    }

    void resizeEvent(QResizeEvent *) override
    {
        if (isExposed()) {
            render();
        }
    }

private:
    bool ensureContext()
    {
        if (!handle()) {
            create();
        }
        const HWND hwnd = hwndFromWinId(winId());
        if (!hwnd) {
            return false;
        }

        if (m_hwnd != hwnd) {
            releaseGl();
            m_hwnd = hwnd;
        }

        int cw = 0;
        int ch = 0;
        if (!clientSizePx(hwnd, &cw, &ch)) {
            return false;
        }
        m_viewW = cw;
        m_viewH = ch;

        if (!m_hdc) {
            m_hdc = GetDC(hwnd);
            if (!m_hdc) {
                return false;
            }
            PIXELFORMATDESCRIPTOR pfd{};
            pfd.nSize = sizeof(pfd);
            pfd.nVersion = 1;
            pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            pfd.iPixelType = PFD_TYPE_RGBA;
            pfd.cColorBits = 32;
            pfd.iLayerType = PFD_MAIN_PLANE;
            // [WGL] ChoosePixelFormat：选匹配的像素格式下标。形参：hdc；ppfd。
            const int fmt = ChoosePixelFormat(m_hdc, &pfd);
            if (fmt == 0) {
                return false;
            }
            // [WGL] SetPixelFormat：把像素格式绑到 HWND（每个窗口只能设一次）。形参：hdc；format；ppfd。
            if (!SetPixelFormat(m_hdc, fmt, &pfd)) {
                return false;
            }
            // [WGL] wglCreateContext：创建兼容 OpenGL 上下文。形参：hdc。
            m_hrc = wglCreateContext(m_hdc);
            if (!m_hrc) {
                return false;
            }
        }

        if (!wglMakeCurrent(m_hdc, m_hrc)) {
            return false;
        }

        if (!m_loaded) {
            if (!m_api.load()) {
                return false;
            }
            if (!createProgram()) {
                return false;
            }
            // [OpenGL] glGenBuffers：分配一个 VBO。形参：n=1；buffers 输出 id。
            m_api.GenBuffers(1, &m_vbo);
            m_loaded = true;
        }
        return m_program != 0;
    }

    GLuint compileShader(GLenum type, const char *src)
    {
        // [OpenGL] glCreateShader：建着色器对象。形参：shaderType=GL_VERTEX_SHADER / GL_FRAGMENT_SHADER。
        const GLuint sh = m_api.CreateShader(type);
        const char *p = src;
        // [OpenGL] glShaderSource：上传源码。形参：shader；count=1；string；length=nullptr 表示以 \0 结尾。
        m_api.ShaderSource(sh, 1, &p, nullptr);
        // [OpenGL] glCompileShader：编译。形参：shader。
        m_api.CompileShader(sh);
        GLint ok = 0;
        m_api.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            m_api.DeleteShader(sh);
            return 0;
        }
        return sh;
    }

    bool createProgram()
    {
        const GLuint vs = compileShader(GL_VERTEX_SHADER, kVert);
        const GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFrag);
        if (!vs || !fs) {
            if (vs) {
                m_api.DeleteShader(vs);
            }
            if (fs) {
                m_api.DeleteShader(fs);
            }
            return false;
        }
        // [OpenGL] glCreateProgram + glAttachShader + glLinkProgram：链成可运行程序。
        m_program = m_api.CreateProgram();
        m_api.AttachShader(m_program, vs);
        m_api.AttachShader(m_program, fs);
        m_api.LinkProgram(m_program);
        m_api.DeleteShader(vs);
        m_api.DeleteShader(fs);
        GLint ok = 0;
        m_api.GetProgramiv(m_program, GL_LINK_STATUS, &ok);
        if (!ok) {
            m_api.DeleteProgram(m_program);
            m_program = 0;
            return false;
        }
        m_locPos = m_api.GetAttribLocation(m_program, "aPos");
        m_locTex = m_api.GetAttribLocation(m_program, "aTex");
        m_locSamp = m_api.GetUniformLocation(m_program, "uTex");
        return true;
    }

    bool uploadTexture()
    {
        const int fw = m_frame.width();
        const int fh = m_frame.height();
        if (fw <= 0 || fh <= 0) {
            return false;
        }

        if (!m_tex) {
            // [OpenGL] glGenTextures：分配纹理 id。形参：n=1；textures。
            glGenTextures(1, &m_tex);
        }
        // [OpenGL] glBindTexture：后续纹理调用作用在这张 2D 纹理上。形参：target=GL_TEXTURE_2D；texture。
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        const int stride = m_frame.bytesPerLine();
        // [OpenGL] glPixelStorei(GL_UNPACK_ROW_LENGTH)：源行有 padding 时按像素宽告诉驱动。
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (stride != fw * 4) {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
        } else {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }

        if (m_texW != fw || m_texH != fh) {
            // [OpenGL] glTexImage2D：分配并上传整张纹理。形参：target；level=0；internalformat=RGBA；
            //           width/height；border=0；format=RGBA；type=UNSIGNED_BYTE；pixels。
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         m_frame.constBits());
            m_texW = fw;
            m_texH = fh;
        } else {
            // [OpenGL] glTexSubImage2D：尺寸不变时只更新像素。形参：xoffset/yoffset=0；其余同上。
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE,
                            m_frame.constBits());
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        return true;
    }

    void render()
    {
        if (!isExposed()) {
            return;
        }
        if (!ensureContext()) {
            return;
        }

        // [OpenGL] glViewport：把 NDC 映射到窗口客户区像素。形参：x/y=0；width/height。
        glViewport(0, 0, m_viewW, m_viewH);
        glClearColor(0.07f, 0.07f, 0.07f, 1.0f);
        glDisable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT);

        if (m_hasFrame && uploadTexture()) {
            const float vw = float(qMax(1, m_viewW));
            const float vh = float(qMax(1, m_viewH));
            const float iw = float(m_texW);
            const float ih = float(m_texH);
            const float scale = qMin(vw / iw, vh / ih);
            const float dw = iw * scale;
            const float dh = ih * scale;
            const float x = (vw - dw) * 0.5f;
            const float y = (vh - dh) * 0.5f;
            auto ndcX = [&](float px) { return (px / vw) * 2.0f - 1.0f; };
            auto ndcY = [&](float py) { return 1.0f - (py / vh) * 2.0f; };

            const Vertex quad[] = {
                {ndcX(x), ndcY(y), 0.0f, 0.0f},
                {ndcX(x + dw), ndcY(y), 1.0f, 0.0f},
                {ndcX(x), ndcY(y + dh), 0.0f, 1.0f},
                {ndcX(x + dw), ndcY(y + dh), 1.0f, 1.0f},
            };

            m_api.UseProgram(m_program);
            m_api.ActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_tex);
            if (m_locSamp >= 0) {
                m_api.Uniform1i(m_locSamp, 0);
            }

            // [OpenGL] glBindBuffer + glBufferData：把四顶点上传到 ARRAY_BUFFER。
            // 形参：target=ARRAY_BUFFER；size；data；usage=DYNAMIC_DRAW（每帧改 NDC）。
            m_api.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
            m_api.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
            if (m_locPos >= 0) {
                m_api.EnableVertexAttribArray(GLuint(m_locPos));
                // [OpenGL] glVertexAttribPointer：aPos = 每顶点前两个 float。
                // 形参：index；size=2；type=FLOAT；normalized=FALSE；stride；pointer 偏移。
                m_api.VertexAttribPointer(GLuint(m_locPos), 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                          reinterpret_cast<void *>(offsetof(Vertex, x)));
            }
            if (m_locTex >= 0) {
                m_api.EnableVertexAttribArray(GLuint(m_locTex));
                m_api.VertexAttribPointer(GLuint(m_locTex), 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                          reinterpret_cast<void *>(offsetof(Vertex, u)));
            }
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            if (m_locPos >= 0) {
                m_api.DisableVertexAttribArray(GLuint(m_locPos));
            }
            if (m_locTex >= 0) {
                m_api.DisableVertexAttribArray(GLuint(m_locTex));
            }
            m_api.BindBuffer(GL_ARRAY_BUFFER, 0);
            m_api.UseProgram(0);
        }

        // [WGL] SwapBuffers：把后缓冲送到窗口（双缓冲）。形参：hdc。
        SwapBuffers(m_hdc);
    }

    QImage m_frame;
    bool m_hasFrame = false;
    GlApi m_api;
    HWND m_hwnd = nullptr;
    HDC m_hdc = nullptr;
    HGLRC m_hrc = nullptr;
    GLuint m_program = 0;
    GLuint m_vbo = 0;
    GLuint m_tex = 0;
    GLint m_locPos = -1;
    GLint m_locTex = -1;
    GLint m_locSamp = -1;
    int m_texW = 0;
    int m_texH = 0;
    int m_viewW = 0;
    int m_viewH = 0;
    bool m_loaded = false;
};

NativeOpenGLVideoRenderer::NativeOpenGLVideoRenderer(QWidget *parent)
{
    m_window = new RenderWindow;
    m_container = QWidget::createWindowContainer(m_window, parent);
    m_container->setMinimumSize(640, 360);
    m_container->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_container->setAttribute(Qt::WA_NativeWindow);
    m_container->setAttribute(Qt::WA_NoSystemBackground);
}

NativeOpenGLVideoRenderer::~NativeOpenGLVideoRenderer()
{
    if (m_window) {
        m_window->releaseGl();
    }
    if (m_container) {
        m_container->hide();
        m_container->setParent(nullptr);
        delete m_container;
    }
    m_window = nullptr;
}

QWidget *NativeOpenGLVideoRenderer::widget()
{
    return m_container;
}

void NativeOpenGLVideoRenderer::present(const QImage &frame)
{
    if (m_window) {
        m_window->setFrame(frame);
    }
}

void NativeOpenGLVideoRenderer::clear(const QString &)
{
    if (m_window) {
        m_window->setPlaceholder();
    }
}
