#pragma once
#include "Rendering/CanvasViewport.h"

// The document fitted and centred in the Navigator's box.
struct NavigatorGeometry {
    QSizeF documentSize;
    QSizeF box;
    QRectF imageRect() const;
    QRectF thumbnailRect(QRectF documentRect) const;
    // The document pixel under a box point, held inside.
    QPointF documentPoint(QPointF point) const;
    friend bool operator==(const NavigatorGeometry &, const NavigatorGeometry &) = default;

private:
    // Box points per document pixel.
    double scale() const;
};
