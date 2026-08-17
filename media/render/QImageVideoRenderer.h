#ifndef QIMAGEVIDEORENDERER_H
#define QIMAGEVIDEORENDERER_H

#include "IVideoRenderer.h"

#include <QPointer>

/**
 * @brief 软件渲染：QLabel + QPixmap 缩放显示
 *
 * 实现简单、兼容性最好；全屏大分辨率时 CPU 缩放成本较高。
 * 显示控件由本类持有并在析构时销毁，保证控件不会比渲染器活得更久。
 */
class QImageVideoRenderer : public IVideoRenderer
{
public:
    explicit QImageVideoRenderer(QWidget *parent = nullptr);
    ~QImageVideoRenderer() override;

    QWidget *widget() override;
    Backend backend() const override { return Backend::QImage; }
    QString name() const override { return QStringLiteral("QImage"); }

    void present(const QImage &frame) override;
    void clear(const QString &placeholder = QString()) override;

private:
    class ImageWidget;
    QPointer<ImageWidget> m_widget;
};

#endif // QIMAGEVIDEORENDERER_H
