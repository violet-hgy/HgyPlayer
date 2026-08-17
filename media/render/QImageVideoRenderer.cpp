#include "QImageVideoRenderer.h"

#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>

/**
 * 自包含显示控件：帧数据与缩放都在控件内部完成。
 * 不回调渲染器，避免控件在渲染器析构后仍访问已释放对象。
 */
class QImageVideoRenderer::ImageWidget : public QLabel
{
public:
    explicit ImageWidget(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(640, 360);
        // 忽略 pixmap 的 sizeHint，否则 setPixmap 会触发布局 Resize 造成重入
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        setStyleSheet(QStringLiteral("QLabel { background: #111111; color: #cccccc; }"));
        setText(QStringLiteral("打开文件后点击播放"));
    }

    void setFrame(const QImage &frame)
    {
        if (frame.isNull()) {
            return;
        }
        m_latest = frame;
        rescale();
    }

    void setPlaceholder(const QString &text)
    {
        m_latest = QImage();
        QLabel::clear();
        setText(text);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        rescale();
    }

private:
    void rescale()
    {
        if (m_updating || m_latest.isNull()) {
            return;
        }
        const QSize target = size();
        if (target.width() < 2 || target.height() < 2) {
            return;
        }

        m_updating = true;
        // SmoothTransformation 在全屏大分辨率下极重，会堵死 UI/音频泵
        const QImage scaled = m_latest.scaled(target, Qt::KeepAspectRatio, Qt::FastTransformation);
        setPixmap(QPixmap::fromImage(scaled, Qt::NoFormatConversion));
        m_updating = false;
    }

    QImage m_latest;
    bool m_updating = false;
};

QImageVideoRenderer::QImageVideoRenderer(QWidget *parent)
    : m_widget(new ImageWidget(parent))
{
}

QImageVideoRenderer::~QImageVideoRenderer()
{
    delete m_widget;
}

QWidget *QImageVideoRenderer::widget()
{
    return m_widget;
}

void QImageVideoRenderer::present(const QImage &frame)
{
    if (m_widget) {
        m_widget->setFrame(frame);
    }
}

void QImageVideoRenderer::clear(const QString &placeholder)
{
    if (m_widget) {
        m_widget->setPlaceholder(placeholder.isEmpty() ? QStringLiteral("打开文件后点击播放")
                                                       : placeholder);
    }
}
