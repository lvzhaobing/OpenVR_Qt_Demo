#include "image_view.h"

ImageView::ImageView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setRenderTarget(RenderTarget::FramebufferObject);
    m_image = QImage();
    m_contentRect = QRect();
    m_aspectRatioMode = Qt::IgnoreAspectRatio;
}

void ImageView::updateImage(const QImage &image)
{
    m_image = image;
    update();             // triggers actual update
}

void ImageView::paint(QPainter *painter)
{
    if(m_image.isNull()){
        requestRender();
        update();
        return;
    }
    if(m_aspectRatioMode != Qt::IgnoreAspectRatio)
    {
        m_image = m_image.scaled(this->boundingRect().width(),this->boundingRect().height(),
                                 (Qt::AspectRatioMode)m_aspectRatioMode,Qt::FastTransformation);
        int x = (this->boundingRect().width() - m_image.width()) / 2;
        int y = (this->boundingRect().height() - m_image.height()) / 2;
        setContentRect(QRect(x,y,m_image.width(),m_image.height()));
        painter->drawImage(QRect(x ,y,m_image.width(),m_image.height()), m_image);
    }
    else
    {
        setContentRect(this->boundingRect().toRect());
        painter->drawImage(this->boundingRect(), m_image);
    }
    requestRender();
    update();
}

QImage ImageView::image() const
{
    return m_image;
}

int ImageView::aspectRatioMode() const
{
    return m_aspectRatioMode;
}

QRect ImageView::contentRect() const
{
    return m_contentRect;
}

void ImageView::setAspectRatioMode(int aspectRatioMode)
{
    if (m_aspectRatioMode == aspectRatioMode)
        return;

    m_aspectRatioMode = aspectRatioMode;
    emit aspectRatioModeChanged(m_aspectRatioMode);
}

void ImageView::setContentRect(QRect contentRect)
{
    if (m_contentRect == contentRect)
        return;

    m_contentRect = contentRect;
    emit contentRectChanged(m_contentRect);
}

/**
 * 内容区域是否包含输入的点位
 **/
bool ImageView::contentRectContains(int x, int y)
{
    return m_contentRect.contains(x,y);
}
