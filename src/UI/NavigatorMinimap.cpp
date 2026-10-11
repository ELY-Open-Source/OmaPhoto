#include "UI/NavigatorMinimap.h"
#include "Rendering/NavigatorGeometry.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <cmath>

bool NavigatorMinimap::visibleSetting()
{
    return QSettings().value(QLatin1String(visibleKey), false).toBool();
}

void NavigatorMinimap::setVisibleSetting(bool visible)
{
    QSettings().setValue(QLatin1String(visibleKey), visible);
}

QSizeF NavigatorMinimap::sizeFor(QSizeF document)
{
    if (!(document.width() > 0 && document.height() > 0))
        return QSizeF(0, 0);
    const double scale = std::min(largest.width() / document.width(), largest.height() / document.height());
    return QSizeF(std::round(document.width() * scale), std::round(document.height() * scale));
}

NavigatorMinimap::RenderKey::RenderKey(const std::optional<CanvasDocument> &document, int revision)
    : documentID(document ? document->id : QUuid()), size(document ? document->size() : QSizeF(0, 0)),
      layers(document ? document->layers : std::vector<ImageLayer>()), revision(revision)
{
}

NavigatorMinimap::NavigatorMinimap(EditorSession &session, QWidget *parent) : QWidget(parent), m_session(session), m_wait(new QTimer(this))
{
    setObjectName(QStringLiteral("navigatorMinimap"));
    setAccessibleName(QStringLiteral("Navigator"));
    setAccessibleDescription(QStringLiteral("Click or drag to move the view"));
    setCursor(Qt::ArrowCursor);
    m_wait->setObjectName(QStringLiteral("navigatorWait"));
    m_wait->setSingleShot(true);
    m_wait->setInterval(250);
    m_wait->setTimerType(Qt::PreciseTimer);
    connect(m_wait, &QTimer::timeout, this, &NavigatorMinimap::render);
    connect(&session, &EditorSession::changed, this, &NavigatorMinimap::synchronize);
    synchronize();
}

// Swift's ContentView condition, then its task(id:).
void NavigatorMinimap::synchronize()
{
    const std::optional<CanvasDocument> &document = m_session.document();
    const bool shown = visibleSetting() && !m_session.canvasOnly() && document && m_session.viewport.zoom() >= zoomShown;
    setVisible(shown);
    if (!shown) {
        // SwiftUI drops the view and its picture with it.
        m_picture = QImage();
        m_key.reset();
        m_wait->stop();
        return;
    }
    setFixedSize(sizeFor(document->size()).toSize());
    RenderKey key(document, m_session.brushRevision());
    if (key != m_key) {
        m_key = std::move(key);
        if (m_picture.isNull())
            render();
        else
            m_wait->start();
    }
    update();
}

void NavigatorMinimap::render()
{
    m_picture = m_session.navigatorImage(picturePixels).value_or(QImage());
    update();
}

void NavigatorMinimap::centre(QPointF point)
{
    const QSizeF document = m_session.document().value().size();
    const NavigatorGeometry geometry{document, sizeFor(document)};
    m_session.viewport.centerView(geometry.documentPoint(point), document);
    m_session.notify();
}

void NavigatorMinimap::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        centre(event->position());
}

void NavigatorMinimap::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
        centre(event->position());
}

void NavigatorMinimap::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds(rect());
    QPainterPath shape;
    shape.addRoundedRect(bounds, 4, 4);
    painter.save();
    painter.setClipPath(shape);
    painter.fillRect(bounds, QColor::fromRgbF(0.11f, 0.11f, 0.11f));
    if (!m_picture.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(bounds, m_picture);
    }
    // The view's box in the accent, inside its rect.
    const QSizeF document = m_session.document().value().size();
    const NavigatorGeometry geometry{document, bounds.size()};
    const QRectF shown = geometry.thumbnailRect(m_session.viewport.visibleDocumentRect(document)).intersected(geometry.imageRect());
    if (!shown.isEmpty()) {
        const QRectF box(shown.topLeft(), QSizeF(std::max(3.0, shown.width()), std::max(3.0, shown.height())));
        painter.setPen(QPen(palette().color(QPalette::Active, QPalette::Highlight), 2.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(box.adjusted(1.25, 1.25, -1.25, -1.25));
    }
    painter.restore();
    // A white rim at 25%, inside the rounded edge.
    painter.setPen(QPen(QColor::fromRgbF(1, 1, 1, 0.25f), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), 3.5, 3.5);
}
