#include "Rendering/NavigatorGeometry.h"
#include <algorithm>
#include <cmath>

QRectF NavigatorGeometry::imageRect() const
{
    if (!(documentSize.width() > 0 && documentSize.height() > 0 && box.width() > 0 && box.height() > 0))
        return QRectF();
    const double scale = std::min(box.width() / documentSize.width(), box.height() / documentSize.height());
    const QSizeF size = documentSize * scale;
    return QRectF(QPointF((box.width() - size.width()) / 2, (box.height() - size.height()) / 2), size);
}

double NavigatorGeometry::scale() const
{
    return documentSize.width() > 0 ? imageRect().width() / documentSize.width() : 0;
}

QRectF NavigatorGeometry::thumbnailRect(QRectF documentRect) const
{
    const QRectF image = imageRect();
    const double scale = this->scale();
    return QRectF(image.left() + documentRect.left() * scale, image.top() + documentRect.top() * scale, documentRect.width() * scale,
                  documentRect.height() * scale);
}

QPointF NavigatorGeometry::documentPoint(QPointF point) const
{
    const double scale = this->scale();
    if (!(scale > 0))
        return QPointF();
    const QRectF image = imageRect();
    return QPointF(std::clamp((point.x() - image.left()) / scale, 0.0, documentSize.width()),
                   std::clamp((point.y() - image.top()) / scale, 0.0, documentSize.height()));
}

QRectF CanvasViewport::visibleDocumentRect(QSizeF documentSize) const
{
    // The viewport never flips: Swift's min and abs are dead.
    return QRectF(documentPoint(QPointF(0, 0), documentSize), documentPoint(QPointF(viewSize.width(), viewSize.height()), documentSize));
}

// Through translate, so the view stops following Fit.
void CanvasViewport::centerView(QPointF point, QSizeF documentSize)
{
    const double scale = pointsPerPixel();
    const QSizeF target(documentSize.width() * scale / 2 - point.x() * scale, documentSize.height() * scale / 2 - point.y() * scale);
    translate(QSizeF(target.width() - pan.width(), target.height() - pan.height()));
}
