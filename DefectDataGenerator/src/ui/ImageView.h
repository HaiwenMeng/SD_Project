#pragma once

#include <QGraphicsPixmapItem>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QImage>
#include <QLabel>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QVector>

class QGraphicsScene;

class ImageView : public QGraphicsView
{
    Q_OBJECT

public:
    enum ToolMode
    {
        ViewMode,
        RectMode,
        PolygonMode,
        BrushMode,
        EraserMode
    };

    explicit ImageView(QWidget *parent = nullptr);

    bool loadImage(const QString &path, QString *errorMessage);
    void setImage(const QImage &image);
    void setMask(const QImage &mask);
    bool saveMask(const QString &path, QString *errorMessage) const;
    QImage maskImage() const;
    void clearMask();
    void setToolMode(ToolMode mode);
    void setBrushSize(int size);
    QString imagePath() const;

signals:
    void statusTextChanged(const QString &text);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void refreshMaskOverlay();
    QPoint imagePointFromView(const QPoint &viewPoint) const;
    void drawBrushAt(const QPoint &imagePoint, bool erase);
    void updatePixelInfoAt(const QPoint &viewPoint);
    void updateInfoLabelPosition();
    void finishRect(const QPoint &start, const QPoint &end);
    void finishPolygon();
    void updateRectPreview(const QPoint &start, const QPoint &end);
    void updatePolygonPreview(const QPoint &currentImagePoint = QPoint(-1, -1));
    void clearPolygonPreview();
    void updateSceneRectToImage();

    QGraphicsScene *m_scene = nullptr;
    QGraphicsPixmapItem *m_imageItem = nullptr;
    QGraphicsPixmapItem *m_maskItem = nullptr;
    QGraphicsPathItem *m_polygonPreviewItem = nullptr;
    QLabel *m_infoLabel = nullptr;
    QImage m_image;
    QImage m_mask;
    QString m_imagePath;
    ToolMode m_toolMode = ViewMode;
    int m_brushSize = 16;
    bool m_drawing = false;
    bool m_panning = false;
    QPoint m_lastMousePos;
    QPoint m_rectStart;
    QVector<QPoint> m_polygonPoints;
    QPoint m_polygonMousePoint;
    bool m_hasPolygonMousePoint = false;
};
