#ifndef NATIVEOPENGLVIDEORENDERER_H
#define NATIVEOPENGLVIDEORENDERER_H

#include "IVideoRenderer.h"

#include <QPointer>
#include <QWidget>

/**
 * @brief 用系统 OpenGL 库渲染（Windows：WGL + opengl32）
 *
 * 不使用 QOpenGLWidget / QOpenGLShaderProgram / QOpenGLTexture。
 * 自己选像素格式、建上下文、编译着色器、glTexImage2D 上传、SwapBuffers。
 */
class NativeOpenGLVideoRenderer : public IVideoRenderer
{
public:
    explicit NativeOpenGLVideoRenderer(QWidget *parent = nullptr);
    ~NativeOpenGLVideoRenderer() override;

    QWidget *widget() override;
    Backend backend() const override { return Backend::OpenGLNative; }
    QString name() const override { return QStringLiteral("OpenGL"); }

    void present(const QImage &frame) override;
    void clear(const QString &placeholder = QString()) override;

private:
    class RenderWindow;

    RenderWindow *m_window = nullptr;
    QPointer<QWidget> m_container;
};

#endif // NATIVEOPENGLVIDEORENDERER_H
