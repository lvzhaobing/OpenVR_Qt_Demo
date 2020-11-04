#ifndef IMAGEVIEW_H
#define IMAGEVIEW_H

#include <QImage>
#include <QPainter>
#include <QQuickPaintedItem>

class ImageView : public QQuickPaintedItem
{
    Q_OBJECT
    Q_DISABLE_COPY(ImageView)
    Q_PROPERTY(QImage image WRITE updateImage READ image)
    Q_PROPERTY(int aspectRatioMode READ aspectRatioMode WRITE setAspectRatioMode NOTIFY aspectRatioModeChanged)
    Q_PROPERTY(QRect contentRect READ contentRect WRITE setContentRect NOTIFY contentRectChanged)

public:
    ImageView(QQuickItem* parent = nullptr);
    void paint(QPainter *painter) override;

    QImage image() const;
    int aspectRatioMode() const;

    QRect contentRect() const;

public slots:
    void updateImage(const QImage& image);

    void setAspectRatioMode(int aspectRatioMode);

    void setContentRect(QRect contentRect);

    bool contentRectContains(int x,int y);

signals:
    void aspectRatioModeChanged(int aspectRatioMode);
    void contentRectChanged(QRect contentRect);
    void requestRender();

protected:
    QImage m_image;
    int m_aspectRatioMode = Qt::IgnoreAspectRatio;
    QRect m_contentRect;
};

#endif // IMAGEVIEW_H
