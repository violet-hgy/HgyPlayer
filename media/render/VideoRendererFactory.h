#ifndef VIDEORENDERERFACTORY_H
#define VIDEORENDERERFACTORY_H

#include "IVideoRenderer.h"

#include <memory>

class QWidget;

class VideoRendererFactory
{
public:
    /**
     * @brief 创建指定后端的渲染器
     * @param parent 作为 widget 的 Qt 父对象（通常是容器 QWidget）
     * @return 失败时返回 nullptr（例如当前平台不支持 D3D11）
     */
    static std::unique_ptr<IVideoRenderer> create(IVideoRenderer::Backend backend,
                                                  QWidget *parent = nullptr);

    static QString backendName(IVideoRenderer::Backend backend);
};

#endif // VIDEORENDERERFACTORY_H
