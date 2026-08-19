#ifndef OPENGLVIDEORENDERER_H
#define OPENGLVIDEORENDERER_H

#include "IVideoRenderer.h"

#include <QPointer>

/**
 * @brief OpenGL 渲染：上传 RGBA 纹理，GPU 按比例绘制
 *
 * 显示控件由本类持有并在析构时销毁。
 * 与 NativeOpenGLVideoRenderer（WGL/opengl32）对照学习。
 */
class OpenGLVideoRenderer : public IVideoRenderer
{
public:
    explicit OpenGLVideoRenderer(QWidget *parent = nullptr);
    ~OpenGLVideoRenderer() override;

    QWidget *widget() override;
    Backend backend() const override { return Backend::OpenGL; }
    QString name() const override { return QStringLiteral("OpenGL (Qt)"); }

    void present(const QImage &frame) override;
    void clear(const QString &placeholder = QString()) override;

private:
    class GlWidget;
    QPointer<GlWidget> m_widget;
};

#endif // OPENGLVIDEORENDERER_H
