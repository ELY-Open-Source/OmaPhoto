#include "Document/EditorSession.h"
#include <cmath>

LayerTransform LayerTransform::quarterTurned(bool clockwise, QSizeF canvas) const
{
    LayerTransform result = *this;
    // The middle moves where the turn takes it.
    const QPointF middle = clockwise ? QPointF(canvas.height() - center().y(), center().x()) : QPointF(center().y(), canvas.width() - center().x());
    result.origin = QPointF(middle.x() - size.width() / 2, middle.y() - size.height() / 2);
    result.rotation = std::fmod(rotation + (clockwise ? 90 : -90), 360.0);
    return result;
}

CanvasGuide CanvasGuide::quarterTurned(bool clockwise, QSizeF canvas) const
{
    CanvasGuide guide = *this;
    if (axis == Axis::vertical) {
        guide.axis = Axis::horizontal;
        if (!clockwise)
            guide.position = canvas.width() - position;
    } else {
        guide.axis = Axis::vertical;
        if (clockwise)
            guide.position = canvas.height() - position;
    }
    return guide;
}

void EditorSession::rotateCanvas(bool clockwise)
{
    commitTransform();
    cancelCrop();
    if (!canEditLayers())
        return;
    const QSizeF canvas = m_document->size();
    beginEdit(clockwise ? QStringLiteral("Rotate Canvas 90° Clockwise") : QStringLiteral("Rotate Canvas 90° Counterclockwise"));
    for (ImageLayer &layer : m_document->layers) {
        layer.transform = layer.transform.quarterTurned(clockwise, canvas);
        if (layer.mask && layer.mask->placement)
            layer.mask->placement = layer.mask->placement->quarterTurned(clockwise, canvas);
    }
    std::swap(m_document->width, m_document->height);
    for (CanvasGuide &guide : m_document->guides)
        guide = guide.quarterTurned(clockwise, canvas);
    if (m_document->selection) {
        const QTransform turn = clockwise ? QTransform(0, 1, -1, 0, canvas.height(), 0) : QTransform(0, -1, 1, 0, 0, canvas.width());
        m_document->selection->path = turn.map(m_document->selection->path);
    }
    viewport.fit(m_document->size());
    endEdit();
}
