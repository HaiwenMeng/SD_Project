#include "ui/ImageView.h"

#include <QFileInfo>
#include <QGraphicsScene>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QScrollBar>
#include <QWheelEvent>

ImageView::ImageView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
    , m_imageItem(new QGraphicsPixmapItem())
    , m_maskItem(new QGraphicsPixmapItem())
    , m_polygonPreviewItem(new QGraphicsPathItem())
    , m_infoLabel(new QLabel(viewport()))
{
    setScene(m_scene);
    m_scene->addItem(m_imageItem);
    m_scene->addItem(m_maskItem);
    m_scene->addItem(m_polygonPreviewItem);
    m_maskItem->setOpacity(0.45);
    QPen previewPen(QColor(0, 180, 255), 2.0);
    previewPen.setCosmetic(true);
    m_polygonPreviewItem->setPen(previewPen);
    m_polygonPreviewItem->setBrush(Qt::NoBrush);
    m_polygonPreviewItem->setZValue(10.0);
    setRenderHint(QPainter::Antialiasing, false);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    m_infoLabel->setAutoFillBackground(false);
    m_infoLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_infoLabel->setStyleSheet(QStringLiteral("QLabel { color: white; background-color: rgba(0, 0, 0, 150); padding: 3px 6px; border-radius: 2px; }"));
    m_infoLabel->setText(QStringLiteral("W:- H:- | x: - y: - | R: - G:- B:-"));
    m_infoLabel->adjustSize();
    updateInfoLabelPosition();
}

bool ImageView::loadImage(const QString &path, QString *errorMessage)
{
    QImage image(path);
    if (image.isNull()) {
        if (errorMessage) {
            *errorMessage = QString(u8"加载图像失败: %1").arg(path);
        }
        return false;
    }
    m_imagePath = path;
    setImage(image);
    clearMask();
    return true;
}

void ImageView::setImage(const QImage &image)
{
    m_image = image.convertToFormat(QImage::Format_RGB888);
    m_imageItem->setPixmap(QPixmap::fromImage(m_image));
    m_mask = QImage(m_image.size(), QImage::Format_Grayscale8);
    m_mask.fill(0);
    refreshMaskOverlay();
    updateSceneRectToImage();
    fitInView(sceneRect(), Qt::KeepAspectRatio);
    m_infoLabel->setText(QStringLiteral("W:%1 H:%2 | x: - y: - | R: - G:- B:-")
                         .arg(m_image.width())
                         .arg(m_image.height()));
    m_infoLabel->adjustSize();
    updateInfoLabelPosition();
}

void ImageView::setMask(const QImage &mask)
{
    if (m_image.isNull()) {
        return;
    }
    m_mask = mask.convertToFormat(QImage::Format_Grayscale8);
    if (m_mask.size() != m_image.size()) {
        m_mask = m_mask.scaled(m_image.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    refreshMaskOverlay();
}

bool ImageView::saveMask(const QString &path, QString *errorMessage) const
{
    if (m_mask.isNull()) {
        if (errorMessage) {
            *errorMessage = QString(u8"mask 为空");
        }
        return false;
    }
    if (!m_mask.save(path, "PNG")) {
        if (errorMessage) {
            *errorMessage = QString(u8"保存 mask 失败: %1").arg(path);
        }
        return false;
    }
    return true;
}

QImage ImageView::maskImage() const
{
    return m_mask;
}

void ImageView::clearMask()
{
    if (m_image.isNull()) {
        return;
    }
    m_mask = QImage(m_image.size(), QImage::Format_Grayscale8);
    m_mask.fill(0);
    m_polygonPoints.clear();
    refreshMaskOverlay();
}

void ImageView::setToolMode(ImageView::ToolMode mode)
{
    m_toolMode = mode;
    if (mode != PolygonMode) {
        m_polygonPoints.clear();
        clearPolygonPreview();
    }
}

void ImageView::setBrushSize(int size)
{
    m_brushSize = qMax(1, size);
}

QString ImageView::imagePath() const
{
    return m_imagePath;
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    scale(factor, factor);
    event->accept();
}

void ImageView::mousePressEvent(QMouseEvent *event)
{
    if (m_image.isNull()) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    updatePixelInfoAt(event->pos());
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_toolMode == ViewMode)) {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    const QPoint imagePoint = imagePointFromView(event->pos());
    if (!QRect(QPoint(0, 0), m_image.size()).contains(imagePoint)) {
        return;
    }

    if (m_toolMode == BrushMode || m_toolMode == EraserMode) {
        m_drawing = true;
        drawBrushAt(imagePoint, m_toolMode == EraserMode);
    } else if (m_toolMode == RectMode) {
        m_drawing = true;
        m_rectStart = imagePoint;
    } else if (m_toolMode == PolygonMode) {
        m_polygonPoints.append(imagePoint);
        updatePolygonPreview(imagePoint);
        emit statusTextChanged(QString(u8"多边形点数: %1").arg(m_polygonPoints.size()));
    }
}

void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    updatePixelInfoAt(event->pos());
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        return;
    }

    if (m_drawing && (m_toolMode == BrushMode || m_toolMode == EraserMode)) {
        const QPoint imagePoint = imagePointFromView(event->pos());
        if (QRect(QPoint(0, 0), m_image.size()).contains(imagePoint)) {
            drawBrushAt(imagePoint, m_toolMode == EraserMode);
        }
        return;
    }
    if (m_drawing && m_toolMode == RectMode) {
        const QPoint imagePoint = imagePointFromView(event->pos());
        if (QRect(QPoint(0, 0), m_image.size()).contains(imagePoint)) {
            updateRectPreview(m_rectStart, imagePoint);
        }
        return;
    }
    if (m_toolMode == PolygonMode && !m_polygonPoints.isEmpty()) {
        const QPoint imagePoint = imagePointFromView(event->pos());
        if (QRect(QPoint(0, 0), m_image.size()).contains(imagePoint)) {
            updatePolygonPreview(imagePoint);
        }
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning) {
        m_panning = false;
        unsetCursor();
        return;
    }
    if (m_drawing && m_toolMode == RectMode) {
        finishRect(m_rectStart, imagePointFromView(event->pos()));
    }
    m_drawing = false;
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_toolMode == PolygonMode && event->button() == Qt::LeftButton) {
        finishPolygon();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    updateInfoLabelPosition();
}

void ImageView::refreshMaskOverlay()
{
    if (m_mask.isNull()) {
        m_maskItem->setPixmap(QPixmap());
        return;
    }
    QImage overlay(m_mask.size(), QImage::Format_ARGB32);
    overlay.fill(Qt::transparent);
    for (int y = 0; y < m_mask.height(); ++y) {
        const uchar *maskLine = m_mask.constScanLine(y);
        QRgb *outLine = reinterpret_cast<QRgb *>(overlay.scanLine(y));
        for (int x = 0; x < m_mask.width(); ++x) {
            if (maskLine[x] > 0) {
                outLine[x] = qRgba(255, 0, 0, 180);
            }
        }
    }
    m_maskItem->setPixmap(QPixmap::fromImage(overlay));
}

QPoint ImageView::imagePointFromView(const QPoint &viewPoint) const
{
    const QPointF scenePoint = mapToScene(viewPoint);
    return QPoint(qRound(scenePoint.x()), qRound(scenePoint.y()));
}

void ImageView::drawBrushAt(const QPoint &imagePoint, bool erase)
{
    QPainter painter(&m_mask);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(erase ? QColor(0, 0, 0) : QColor(255, 255, 255));
    painter.drawEllipse(imagePoint, m_brushSize / 2, m_brushSize / 2);
    painter.end();
    refreshMaskOverlay();
}

void ImageView::updatePixelInfoAt(const QPoint &viewPoint)
{
    if (m_image.isNull()) {
        return;
    }

    const QPoint imagePoint = imagePointFromView(viewPoint);
    if (!QRect(QPoint(0, 0), m_image.size()).contains(imagePoint)) {
        m_infoLabel->setText(QStringLiteral("W:%1 H:%2 | x: - y: - | R: - G:- B:-")
                             .arg(m_image.width())
                             .arg(m_image.height()));
        m_infoLabel->adjustSize();
        updateInfoLabelPosition();
        return;
    }

    const QColor color = QColor::fromRgb(m_image.pixel(imagePoint));
    m_infoLabel->setText(QStringLiteral("W:%1 H:%2 | x: %3 y: %4 | R: %5 G:%6 B:%7")
                         .arg(m_image.width())
                         .arg(m_image.height())
                         .arg(imagePoint.x())
                         .arg(imagePoint.y())
                         .arg(color.red())
                         .arg(color.green())
                         .arg(color.blue()));
    m_infoLabel->adjustSize();
    updateInfoLabelPosition();
}

void ImageView::updateInfoLabelPosition()
{
    if (!m_infoLabel) {
        return;
    }
    const int margin = 6;
    const int x = margin;
    const int y = viewport()->height() - m_infoLabel->height() - margin;
    m_infoLabel->move(x, qMax(margin, y));
    m_infoLabel->raise();
}

void ImageView::finishRect(const QPoint &start, const QPoint &end)
{
    QRect rect(start, end);
    rect = rect.normalized().intersected(QRect(QPoint(0, 0), m_image.size()));
    if (rect.isEmpty()) {
        return;
    }
    QPainter painter(&m_mask);
    painter.fillRect(rect, Qt::white);
    painter.end();
    clearPolygonPreview();
    refreshMaskOverlay();
}

void ImageView::updateRectPreview(const QPoint &start, const QPoint &end)
{
    QRect rect(start, end);
    rect = rect.normalized().intersected(QRect(QPoint(0, 0), m_image.size()));
    QPainterPath path;
    if (!rect.isEmpty()) {
        path.addRect(rect);
    }
    m_polygonPreviewItem->setPath(path);
}

void ImageView::finishPolygon()
{
    if (m_polygonPoints.size() < 3) {
        emit statusTextChanged(QString(u8"多边形至少需要 3 个点"));
        return;
    }
    QPolygon polygon(m_polygonPoints);
    QPainter painter(&m_mask);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPolygon(polygon);
    painter.end();
    m_polygonPoints.clear();
    clearPolygonPreview();
    refreshMaskOverlay();
}

void ImageView::updatePolygonPreview(const QPoint &currentImagePoint)
{
    m_hasPolygonMousePoint = currentImagePoint.x() >= 0 && currentImagePoint.y() >= 0;
    if (m_hasPolygonMousePoint) {
        m_polygonMousePoint = currentImagePoint;
    }

    QPainterPath path;
    if (!m_polygonPoints.isEmpty()) {
        path.moveTo(m_polygonPoints.first());
        for (int i = 1; i < m_polygonPoints.size(); ++i) {
            path.lineTo(m_polygonPoints.at(i));
        }
        if (m_hasPolygonMousePoint) {
            path.lineTo(m_polygonMousePoint);
        }
        for (const QPoint &point : m_polygonPoints) {
            path.addEllipse(QPointF(point), 3.0, 3.0);
        }
    }
    m_polygonPreviewItem->setPath(path);
}

void ImageView::clearPolygonPreview()
{
    m_polygonPreviewItem->setPath(QPainterPath());
    m_hasPolygonMousePoint = false;
}

void ImageView::updateSceneRectToImage()
{
    m_imageItem->setPos(0, 0);
    m_maskItem->setPos(0, 0);
    m_polygonPreviewItem->setPos(0, 0);
    m_scene->setSceneRect(QRectF(QPointF(0, 0), QSizeF(m_image.size())));
}
