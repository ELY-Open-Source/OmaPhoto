#include "UI/BlendModePicker.h"
#include <QAbstractItemView>
#include <QEvent>

BlendModePicker::BlendModePicker(EditorSession &session, QWidget *parent) : QComboBox(parent), m_session(session)
{
    setAccessibleName(QStringLiteral("Blend mode"));
    setObjectName(QStringLiteral("blendMode"));
    // Grouped as Photoshop groups them, a line between.
    for (const LayerBlendMode mode : allLayerBlendModes) {
        if (count() && blendGroup(mode) != blendGroup(*layerBlendMode(itemText(count() - 1))))
            insertSeparator(count());
        addItem(rawValue(mode));
    }
    connect(this, &QComboBox::highlighted, this, &BlendModePicker::highlight);
    connect(this, &QComboBox::activated, this, &BlendModePicker::choose);
    connect(&m_session, &EditorSession::changed, this, &BlendModePicker::synchronize);
    // Qt 6.10 closes on Escape without calling hidePopup.
    view()->window()->installEventFilter(this);
    synchronize();
}

void BlendModePicker::showPopup()
{
    m_tracking = true;
    m_layerID = m_session.activeLayerID();
    m_highlighted = std::nullopt;
    QComboBox::showPopup();
}

bool BlendModePicker::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != view()->window() || event->type() != QEvent::Hide)
        return QComboBox::eventFilter(watched, event);
    m_tracking = false;
    // A click closes the menu, then chooses: clear after.
    QMetaObject::invokeMethod(this, [this] {
        m_session.previewBlendMode(std::nullopt, std::nullopt);
        m_layerID = std::nullopt;
        m_highlighted = std::nullopt;
    }, Qt::QueuedConnection);
    return false;
}

void BlendModePicker::synchronize()
{
    setEnabled(m_session.canEditAppearance());
    if (m_tracking)
        return;
    const std::optional<ImageLayer> active = m_session.activeLayer();
    setCurrentText(rawValue(active ? active->blendMode : LayerBlendMode::normal));
}

void BlendModePicker::highlight(int index)
{
    // Qt 6.10 signals too, as the closed menu follows.
    if (!m_tracking)
        return;
    const std::optional<LayerBlendMode> mode = layerBlendMode(itemText(index));
    if (mode)
        m_highlighted = mode;
    m_session.previewBlendMode(mode, m_layerID);
}

void BlendModePicker::choose(int index)
{
    // The layer the menu opened on; closed, the active one.
    const std::optional<QUuid> target = m_layerID ? m_layerID : m_session.activeLayerID();
    const std::optional<LayerBlendMode> mode = m_highlighted ? m_highlighted : layerBlendMode(itemText(index));
    m_highlighted = std::nullopt;
    m_layerID = std::nullopt;
    if (!mode || m_session.activeLayerID() != target) {
        synchronize();
        return;
    }
    m_session.setLayerBlendMode(*mode);
    setCurrentText(rawValue(*mode));
}
