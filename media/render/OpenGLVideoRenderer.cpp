#include "OpenGLVideoRenderer.h"

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QPainter>
#include <QSizePolicy>
#include <QSurfaceFormat>

/**
 * 自包含 GL 控件：帧数据保存在控件内，绘制全部在 paintGL 内完成。
 * 不回调渲染器，避免控件生命周期与渲染器错位导致访问已释放对象。
 */
class OpenGLVideoRenderer::GlWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
public:
    explicit GlWidget(QWidget *parent = nullptr)
        : QOpenGLWidget(parent)
    {
        QSurfaceFormat fmt = format();
        fmt.setDepthBufferSize(0);
        fmt.setStencilBufferSize(0);
        setFormat(fmt);
        setMinimumSize(640, 360);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    }

    ~GlWidget() override
    {
        makeCurrent();
        delete m_texture;
        m_texture = nullptr;
        delete m_program;
        m_program = nullptr;
        doneCurrent();
    }

    void setFrame(const QImage &frame)
    {
        if (frame.isNull()) {
            return;
        }
        m_frame = frame.convertToFormat(QImage::Format_RGBA8888);
        m_placeholder.clear();
        update();
    }

    void setPlaceholder(const QString &text)
    {
        m_frame = QImage();
        m_placeholder = text;
        update();
    }

protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        // [OpenGL] glClearColor：后续 glClear 用的 RGBA。形参：red/green/blue/alpha，范围 0~1。
        glClearColor(0.07f, 0.07f, 0.07f, 1.0f);
        // [OpenGL] glDisable：关掉深度测试（2D 贴图不需要）。形参：cap=GL_DEPTH_TEST。
        glDisable(GL_DEPTH_TEST);

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

        m_program = new QOpenGLShaderProgram;
        // [OpenGL] 对应 glCreateShader + glShaderSource + glCompileShader（顶点/片元）再 glLinkProgram。
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVert);
        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFrag);
        m_program->bindAttributeLocation("aPos", 0);
        m_program->bindAttributeLocation("aTex", 1);
        m_program->link();
    }

    void paintGL() override
    {
        // [OpenGL] glClear：用 glClearColor 清颜色缓冲。形参：mask=GL_COLOR_BUFFER_BIT。
        glClear(GL_COLOR_BUFFER_BIT);

        if (m_frame.isNull() || !m_program || !m_program->isLinked()) {
            return;
        }

        if (!m_texture || m_texSize != m_frame.size()) {
            delete m_texture;
            // [OpenGL] 经 QOpenGLTexture 创建 GL_TEXTURE_2D，格式 RGBA8，线性过滤，边缘 clamp。
            m_texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
            m_texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
            m_texture->setSize(m_frame.width(), m_frame.height());
            m_texture->setMinificationFilter(QOpenGLTexture::Linear);
            m_texture->setMagnificationFilter(QOpenGLTexture::Linear);
            m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);
            m_texture->allocateStorage();
            m_texSize = m_frame.size();
        }
        // [OpenGL] 对应 glTexSubImage2D：把 CPU 的 RGBA 像素上传到当前 2D 纹理。
        // 形参：pixelFormat=RGBA；pixelType=UInt8；data=帧字节。
        m_texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, m_frame.constBits());

        // KeepAspectRatio：把图像按比例居中映射到 NDC
        const float vw = float(qMax(1, width()));
        const float vh = float(qMax(1, height()));
        const float iw = float(m_frame.width());
        const float ih = float(m_frame.height());
        const float scale = qMin(vw / iw, vh / ih);
        const float dw = iw * scale;
        const float dh = ih * scale;
        const float x = (vw - dw) * 0.5f;
        const float y = (vh - dh) * 0.5f;

        auto ndcX = [&](float px) { return (px / vw) * 2.0f - 1.0f; };
        auto ndcY = [&](float py) { return 1.0f - (py / vh) * 2.0f; };

        const GLfloat verts[] = {
            ndcX(x),      ndcY(y),
            ndcX(x + dw), ndcY(y),
            ndcX(x),      ndcY(y + dh),
            ndcX(x + dw), ndcY(y + dh),
        };
        const GLfloat texCoords[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.0f, 1.0f,
            1.0f, 1.0f,
        };

        m_program->bind();
        m_texture->bind(0);
        m_program->setUniformValue("uTex", 0);
        m_program->enableAttributeArray(0);
        m_program->enableAttributeArray(1);
        m_program->setAttributeArray(0, verts, 2);
        m_program->setAttributeArray(1, texCoords, 2);
        // [OpenGL] glDrawArrays：画三角形带。形参：mode=GL_TRIANGLE_STRIP；first=0；count=4 顶点。
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_program->disableAttributeArray(0);
        m_program->disableAttributeArray(1);
        m_texture->release();
        m_program->release();
    }

    void paintEvent(QPaintEvent *event) override
    {
        QOpenGLWidget::paintEvent(event);
        if (m_frame.isNull() && !m_placeholder.isEmpty()) {
            QPainter p(this);
            p.setPen(QColor(0xcc, 0xcc, 0xcc));
            p.drawText(rect(), Qt::AlignCenter, m_placeholder);
        }
    }

private:
    QImage m_frame;
    QString m_placeholder = QStringLiteral("打开文件后点击播放");
    QOpenGLShaderProgram *m_program = nullptr;
    QOpenGLTexture *m_texture = nullptr;
    QSize m_texSize;
};

OpenGLVideoRenderer::OpenGLVideoRenderer(QWidget *parent)
    : m_widget(new GlWidget(parent))
{
}

OpenGLVideoRenderer::~OpenGLVideoRenderer()
{
    delete m_widget;
}

QWidget *OpenGLVideoRenderer::widget()
{
    return m_widget;
}

void OpenGLVideoRenderer::present(const QImage &frame)
{
    if (m_widget) {
        m_widget->setFrame(frame);
    }
}

void OpenGLVideoRenderer::clear(const QString &placeholder)
{
    if (m_widget) {
        m_widget->setPlaceholder(placeholder.isEmpty() ? QStringLiteral("打开文件后点击播放")
                                                       : placeholder);
    }
}
